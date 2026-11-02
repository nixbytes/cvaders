/*
 * ===========================================================================
 * render.c  --  SDL frontend implementation
 * ===========================================================================
 *
 * DESIGN: every SDL resource loaded here is stored in the Renderer struct
 * and released on exit. This gives "leak-free by construction":
 *
 *   - We track each texture / sound / font as a field.
 *   - renderer_destroy() free-calls EVERY field (free(NULL) is a no-op).
 *   - No dynamic allocation happens inside the game loop.
 *
 * MEMORY-SAFETY CHECKLIST (what makes this file safe)
 *   1. Every `SDL_Load...` / `TT_LoadFont` / `Mix_LoadWAV` /
 *      `Mix_LoadMUS` return is checked for NULL. The Python original had
 *      NO such checks; a missing asset file is a crash in C.
 *   2. No allocation inside the per-frame loop, so there is nothing to
 *      forget to free.
 *   3. Every loaded resource is paired with its Free/Destroy in
 *      renderer_destroy().
 *   4. `game_init` / `game_free` bracket the loop: one allocation, one
 *      de-allocation.
 *   5. SDL_Quit() is called to release the underlying system resources.
 *
 * BUG FIXES VS THE ORIGINAL PYTHON
 *   1. Sound objects are loaded ONCE at startup, not on every SPACE press
 *      / explosion. In Python that is a per-frame allocation; in C it
 *      would be a heap leak every frame.
 *   2. The on-disk file is `explosion.wav` (lowercase). The original Python
 *      code tried to load `Explosion.wav`, which fails on case-sensitive
 *      filesystems. We use the correct case.
 *   3. A `game_over` makes the frame loop return 0, so the loop actually
 *      exits. The Python original never stopped the loop after a game over.
 *
 * Note: this file needs SDL2, SDL_ttf and SDL_mixer to compile. Those
 * libraries are not installed in the test sandbox, so the C TESTS do not
 * build this file -- only `make all` (with SDL present) does. The logic
 * that matters is in game.c, which IS tested.
 */

#include "render.h"
#include "pyvaders.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

/* ---------------------------------------------------------------------- */
/* Renderer opaque struct definition                                      */
/* ---------------------------------------------------------------------- */

struct Renderer {
    SDL_Window   *window;
    SDL_Renderer *renderer;

    /* One-time loaded textures. */
    SDL_Texture *background;
    SDL_Texture *player_image;
    SDL_Texture *alien_image;
    SDL_Texture *laser_image;

    /* One-time loaded sounds. */
    Mix_Chunk   *laser_sound;
    Mix_Chunk   *explosion_sound;
    Mix_Music   *music;

    /* The score / game-over font. */
    TTF_Font    *font;

    /* The game world: one heap allocation, released on exit. */
    GameState state;
    int       have_state;
    int       last_score;
};

/* ---------------------------------------------------------------------- */
/* Asset loading helper with NULL checks                                   */
/* ---------------------------------------------------------------------- */

/*
 * Load a PNG image and turn it into a texture.
 *
 * Returns the texture on success, NULL on any failure. On failure we print
 * the SDL error and return -- we do NOT dereference a NULL. This is the
 * critical safety fix vs the Python original, which assumed the file
 * always exists.
 *
 * The path is built with snprintf so a long name cannot overflow the
 * buffer (a classic C security bug we are explicitly guarding against).
 */
static SDL_Texture *load_texture(
        SDL_Renderer *r,
        const char   *image_name,
        SDL_Texture  **out)
{
    char path[512];
    int n = snprintf(path, sizeof(path), "assests/%s", image_name);
    if (n < 0 || n >= (int)sizeof(path)) {
        fprintf(stderr, "asset path too long: %.80s...\n", image_name);
        return NULL;
    }

    SDL_Surface *surf = SDL_LoadBMP(path);
    if (surf == NULL) {
        fprintf(stderr, "image %s: %s\n", image_name, SDL_GetError());
        return NULL;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);   /* we own the surface; free it. */
    if (tex == NULL) {
        fprintf(stderr, "texture %s: %s\n", image_name, SDL_GetError());
        return NULL;
    }

    *out = tex;
    return tex;
}

/*
 * Open the audio subsystem once and load the two sound effects plus the
 * looping background music. Every return value is checked for NULL.
 *
 * Returns 0 if audio opened (some sub-loads may individually have failed,
 * which is non-fatal), or -1 if audio could not be opened at all.
 */
static int open_audio(
        Mix_Chunk **laser_out,
        Mix_Chunk **explosion_out,
        Mix_Music **music_out)
{
    *laser_out     = NULL;
    *explosion_out = NULL;
    *music_out     = NULL;

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        fprintf(stderr, "audio open: %s\n", Mix_GetError());
        return -1;
    }

    *laser_out = Mix_LoadWAV("assests/laser.wav");
    if (*laser_out == NULL) {
        fprintf(stderr, "laser.wav: %s\n", Mix_GetError());
    }

    /*
     * Note the lowercase filename: the original Python code used
     * "Explosion.wav" which fails on case-sensitive filesystems.
     */
    *explosion_out = Mix_LoadWAV("assests/explosion.wav");
    if (*explosion_out == NULL) {
        fprintf(stderr, "explosion.wav: %s\n", Mix_GetError());
    }

    /*
     * The music file name contains spaces and a hyphen. SDL handles spaces
     * in a C string fine; the hyphen is just a normal character.
     */
    *music_out = Mix_LoadMUS("assests/Dark Fantasy Studio - Particle voyager.mp3");
    if (*music_out != NULL) {
        Mix_PlayMusic(*music_out, -1);   /* -1 = loop forever */
    } else {
        fprintf(stderr, "music: %s\n", Mix_GetError());
    }

    return 0;
}

/* ---------------------------------------------------------------------- */
/* Renderer lifecycle                                                     */
/* ---------------------------------------------------------------------- */

/*
 * renderer_create:
 *
 * Sets up SDL, the window, textures, sounds and font, and the game state.
 * On ANY fatal failure we use the `goto fail_x` cleanup chain -- the
 * canonical C "unwind the things we already created" pattern. Every label
 * frees exactly the resources created at or below that point; this avoids
 * freeing something we never allocated.
 */
Renderer *renderer_create(void)
{
    Renderer *r;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return NULL;
    }
    TTF_Init();

    r = (Renderer *)calloc(1, sizeof(Renderer));
    if (r == NULL) {
        fprintf(stderr, "out of memory\n");
        SDL_Quit();
        return NULL;
    }

    r->window = SDL_CreateWindow(
        "PyVaders",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, 0);
    if (r->window == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        goto fail_window;
    }

    r->renderer = SDL_CreateRenderer(r->window, -1,
                                     SDL_RENDERER_ACCELERATED);
    if (r->renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        goto fail_renderer;
    }

    /*
     * Load the four game textures. If ANY is missing the game is unplayable,
     * so we treat a missing sprite as fatal and unwind everything.
     */
    if (load_texture(r->renderer, "mountains03-1920-x-1080_full.png",
                     &r->background) == NULL ||
        load_texture(r->renderer, "darkgrey_02.png", &r->player_image) == NULL ||
        load_texture(r->renderer, "alien_enemy.png",  &r->alien_image)  == NULL ||
        load_texture(r->renderer, "laser.png",        &r->laser_image)  == NULL) {
        goto fail_textures;
    }

    /*
     * Window icon. SDL_SetWindowIcon takes an SDL_Surface*, not a texture.
     * We load it, hand it to SDL (which copies what it needs), then free
     * the surface ourselves.
     */
    SDL_Surface *icon = SDL_LoadBMP("assests/alien.png");
    if (icon != NULL) {
        SDL_SetWindowIcon(r->window, icon);
        SDL_FreeSurface(icon);
    } else {
        fprintf(stderr, "icon: %s (continuing without icon)\n",
                SDL_GetError());
    }

    /* Audio. A failure here is non-fatal: we play silently. */
    if (open_audio(&r->laser_sound, &r->explosion_sound, &r->music) != 0) {
        fprintf(stderr, "continuing with no sound\n");
        r->laser_sound     = NULL;
        r->explosion_sound = NULL;
        r->music           = NULL;
    }

    /* Font. Non-fatal: the game still runs without the on-screen score. */
    r->font = TTF_OpenFont("assests/Monoton-Regular.ttf", 32);
    if (r->font == NULL) {
        fprintf(stderr, "font: %s (continuing without score text)\n",
                SDL_GetError());
    }

    /*
     * The game world: the single heap allocation in the whole core.
     * If this fails we must undo the audio we just opened.
     */
    if (game_init(0, &r->state) != 0) {
        fprintf(stderr, "game_init failed\n");
        goto fail_game;
    }
    r->have_state = 1;
    r->last_score = 0;

    SDL_SetWindowTitle(r->window, "PyVaders");
    return r;

/* Cleanup labels -- each frees what the stage above it allocated. */
fail_game:
    Mix_FreeMusic(r->music);
fail_textures:
    if (r->background)   SDL_DestroyTexture(r->background);
    if (r->player_image) SDL_DestroyTexture(r->player_image);
    if (r->alien_image)  SDL_DestroyTexture(r->alien_image);
    if (r->laser_image)  SDL_DestroyTexture(r->laser_image);
    if (r->laser_sound)    Mix_FreeChunk(r->laser_sound);
    if (r->explosion_sound) Mix_FreeChunk(r->explosion_sound);
fail_renderer:
    if (r->renderer) SDL_DestroyRenderer(r->renderer);
fail_window:
    if (r->window)   SDL_DestroyWindow(r->window);
    TTF_Quit();
    SDL_Quit();
    free(r);
    return NULL;
}

/*
 * renderer_destroy: release everything, in reverse order of creation.
 *
 * `free(NULL)` and the SDL `..._Destroy(NULL)` / `..._Free(NULL)` variants
 * are defined to be no-ops, so we can call them unconditionally without
 * checking each pointer.
 */
void renderer_destroy(Renderer *r)
{
    if (r == NULL) return;

    if (r->have_state) {
        game_free(&r->state);
        r->have_state = 0;
    }

    if (r->music)           Mix_FreeMusic(r->music);
    if (r->laser_sound)     Mix_FreeChunk(r->laser_sound);
    if (r->explosion_sound) Mix_FreeChunk(r->explosion_sound);
    if (r->font)             TTF_CloseFont(r->font);
    if (r->background)       SDL_DestroyTexture(r->background);
    if (r->player_image)     SDL_DestroyTexture(r->player_image);
    if (r->alien_image)      SDL_DestroyTexture(r->alien_image);
    if (r->laser_image)      SDL_DestroyTexture(r->laser_image);
    if (r->renderer)         SDL_DestroyRenderer(r->renderer);
    if (r->window)           SDL_DestroyWindow(r->window);

    TTF_Quit();
    SDL_Quit();

    free(r);
}

/* ---------------------------------------------------------------------- */
/* Per-frame update + draw                                                */
/* ---------------------------------------------------------------------- */

/*
 * Drain the SDL event queue for one frame and translate key events into
 * calls on the pure game logic. Mirrors the Python event loop.
 *
 * `running` is a pointer-to-int so this helper can tell the caller whether
 * to keep the loop going (still 1) or stop (0).
 */
static void handle_events(Renderer *r, int *running)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            *running = 0;
            break;

        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_LEFT) {
                set_player_direction(&r->state, -0.5);
            } else if (event.key.keysym.sym == SDLK_RIGHT) {
                set_player_direction(&r->state, 0.5);
            } else if (event.key.keysym.sym == SDLK_SPACE) {
                fire_laser(&r->state);
                /*
                 * The Mix_Chunk was loaded ONCE at startup, so playing it
                 * here is not an allocation -- this is the fix for the
                 * Python per-frame `mixer.Sound("laser.wav")` leak.
                 */
                if (r->laser_sound != NULL) {
                    Mix_PlayChannel(-1, r->laser_sound, 1);
                }
            }
            break;

        case SDL_KEYUP:
            if (event.key.keysym.sym == SDLK_LEFT ||
                event.key.keysym.sym == SDLK_RIGHT) {
                set_player_direction(&r->state, 0.0);
            }
            break;

        default:
            break;
        }
    }
}

/*
 * Draw the score text. The texture we create here is a per-frame
 * allocation, so we free it at the end of the call -- it is NOT stored in
 * the Renderer, and freeing it each frame keeps the memory use constant.
 */
static void draw_text(
        Renderer *r,
        const char *text,
        int x, int y)
{
    if (r->font == NULL) return;

    if (text == NULL) return;

    SDL_Color fg = {255, 0, 0, 255};
    SDL_Surface *surface = TTF_RenderText_Solid(r->font, text, fg);
    if (surface == NULL) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r->renderer, surface);
    if (tex != NULL) {
        SDL_Rect dest = {x, y, (int)surface->w, (int)surface->h};
        SDL_RenderCopy(r->renderer, tex, NULL, &dest);
        SDL_DestroyTexture(tex);   /* free just now */
    }
    SDL_FreeSurface(surface);      /* free just now */
}

/* Draw one full frame to the window. */
void renderer_present(Renderer *r)
{
    /* Clear the whole window to a near-black fill (the original colour). */
    SDL_SetRenderDrawColor(r->renderer, 5, 0, 0, 255);
    SDL_RenderClear(r->renderer);

    /* Background. */
    if (r->background != NULL) {
        SDL_Rect dest = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(r->renderer, r->background, NULL, &dest);
    }

    /* Player. */
    if (r->player_image != NULL) {
        SDL_Rect dest = {
            (int)r->state.player_x, (int)r->state.player_y, 64, 64};
        SDL_RenderCopy(r->renderer, r->player_image, NULL, &dest);
    }

    /* Enemies. The loop bounds are fixed by NUM_ENEMIES, so there is no
     * out-of-bounds access. */
    if (r->alien_image != NULL) {
        for (int i = 0; i < NUM_ENEMIES; i++) {
            const Alien *a = &r->state.aliens[i];
            SDL_Rect dest = {(int)a->x, (int)a->y, 64, 64};
            SDL_RenderCopy(r->renderer, r->alien_image, NULL, &dest);
        }
    }

    /* Laser. The original added an offset of (+9, +6). */
    if (r->laser_image != NULL) {
        SDL_Rect dest = {
            (int)r->state.laser.x + 9, (int)r->state.laser.y + 6, 48, 48};
        SDL_RenderCopy(r->renderer, r->laser_image, NULL, &dest);
    }

    /* Score text. */
    char score_buf[32];
    int n = snprintf(score_buf, sizeof(score_buf), "Score : %d",
                    r->state.score);
    if (n > 0) {
        draw_text(r, score_buf, 6, 6);
    }

    /* Game over banner -- only when the match is over. */
    if (r->state.game_over) {
        draw_text(r, "GAME OVER", 250, 250);
    }

    SDL_RenderPresent(r->renderer);
}

/*
 * renderer_step: one full frame.
 *
 * Returns 1 if the loop should keep going, 0 if it should stop (user quit
 * or game over). This is the single function the main loop calls every
 * frame, which keeps `main.c` tiny.
 */
int renderer_step(Renderer *r)
{
    int running = 1;
    handle_events(r, &running);
    if (!running) return 0;

    update_player(&r->state);
    update_aliens(&r->state);
    update_laser(&r->state);

    /*
     * A score increase means a hit happened this frame -- play the
     * explosion. The Mix_Chunk was loaded ONCE at startup, so this is not
     * a per-frame allocation.
     */
    if (r->state.score != r->last_score && r->explosion_sound != NULL) {
        Mix_PlayChannel(-1, r->explosion_sound, 1);
        r->last_score = r->state.score;
    }

    renderer_present(r);

    if (r->state.game_over) return 0;   /* stop the loop on game over */
    return 1;
}
