/*
 * ===========================================================================
 * game.c  --  Implementation of the cvanders game logic
 * ===========================================================================
 *
 * NEW-COMER ROADMAP THROUGH THIS FILE
 *   1. Top of file: #include lines and the private "static" helpers
 *      (the random-number machinery).
 *   2. The public functions declared in cvaders.h, defined in the SAME
 *      ORDER they appear in the header, one block at a time.
 *   3. Each function does ONE thing and reads like its header comment.
 *
 * KEY MEMORY-SAFETY IDEAS USED HERE
 *   - NULL checks: nearly every function begins with `if (... == NULL)
 *     return;`. In C you can pass a "null" pointer (an address pointing
 *     at nothing) and the program will CRASH if you use it. Checking up
 *     front is cheap insurance and is the single most common C fix.
 *   - Fixed-size arrays in structs: `Alien aliens[NUM_ENEMIES]`. There is
 *     no dynamic resizing and no parallel arrays, so there is nothing to
 *     go out of sync.
 *   - Exactly one allocation: game_init() calls calloc() ONCE and
 *     game_free() frees it. No allocation inside the hot loop means no
 *     per-frame leak risk.
 *   - `static` functions (xorshift32, sample01, ffract, fint_incl) are
 *     "file-private": they are only visible to THIS .c file. That is how
 *     you hide implementation details and avoid name clashes.
 */

#include "cvaders.h"   /* our own public interface            */
#include <stdlib.h>     /* calloc, free                        */

/* --- Seedable random-number generator ----------------------------------
 *
 * We use our own deterministic PRNG (xorshift32): for a given seed it
 * produces the EXACT same sequence every run, which is what matters for
 * testing. A standard library generator would not guarantee reproducibility
 * across runs/platforms.
 *
 * `static` in front of the function type means "private to this file".
 * */
static uint32_t xorshift32(uint32_t *state)
{
    /*
     * xorshift32 is a tiny, fast pseudo-random generator. It mixes up the
     * bits of `*state` and stores the result back. The `*state = x` line
     * persists the new value so the NEXT call continues from it.
     *
     * `^=` is "xor-assign": x ^= y means x = x ^ y.
     * `<<` / `>>` shift bits left / right.
     */
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* A sample in the half-open range [0.0, 1.0) using full 32-bit resolution.
 * A C `double` holds integers up to 2^53 exactly, so dividing a 32-bit
 * value by 2^32 is exact -- this is what lets inclusive respawns reach the
 * upper bound. */
static double sample01(uint32_t *state)
{
    return (double)xorshift32(state) / 4294967296.0;
}

/* A float in the half-open range [min, max). Used for the initial enemy
 * spawn, whose upper bound is EXCLUSIVE. */
static double ffract(uint32_t *state, double min, double max)
{
    return min + sample01(state) * (max - min);
}

/* An integer in the CLOSED range [lo, hi]. Used for respawns, which are
 * INCLUSIVE on both ends. We use exact 64-bit arithmetic so the full range
 * -- including `hi` -- is reachable. */
static double fint_incl(uint32_t *state, double lo, double hi)
{
    /*
     * `span` is how many distinct integer values we can return. +1u because
     * the upper bound is inclusive. The `u` suffix makes it an *unsigned*
     * integer literal, which is what we want for bit maths.
     */
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    if (span == 0u) return lo;                 /* guard degenerate lo > hi */

    /*
     * Pick the top 32 bits of (raw * span). This "multiply-high" trick
     * maps the uniform raw sample onto the [0, span) range with negligible
     * bias -- good enough for a game.
     */
    uint64_t m = ((uint64_t)xorshift32(state)) * span;
    uint32_t pick = (uint32_t)(m >> 32);       /* top 32 bits of product */
    if (pick >= span) pick = span - 1u;        /* clamp the rare overflow */
    return lo + (double)pick;
}

/* ----------------------------------------------------------------------- *
 *  PUBLIC FUNCTIONS (defined to match the prototypes in cvaders.h)
 * ----------------------------------------------------------------------- */

/*
 * game_init: build a fresh GameState on the heap and hand its address back
 * through *out_state.
 *
 * Note the `**` (pointer-to-pointer): the caller has a local
 * `GameState *g;` and we pass `&g` so we can write the new pointer INTO
 * that local variable. This is C's way of "returning two things".
 */
int game_init(uint32_t seed, GameState **out_state)
{
    /* Defensive check: a null output pointer we cannot fill. */
    if (out_state == NULL) return -1;

    /*
     * calloc allocates ONE GameState and zeroes every byte of it.
     * This is safer than malloc() because it ensures all fields (like
     * pointers or booleans) start at 0/false instead of containing
     * "garbage" values from previous memory use.
     *
     * `sizeof(GameState)` asks the compiler "how many bytes is this
     * struct?", so the size is always correct even if the struct changes.
     *
     * We MUST check the result: allocation can fail under memory pressure.
     * Using a NULL pointer afterwards would be undefined behaviour (a crash).
     * That `if (g == NULL) return -1;` line is what makes this safe.
     */
    GameState *g = (GameState *)calloc(1, sizeof(GameState));
    if (g == NULL) return -1;

    g->rng_state = seed ? seed : 0x1234abcdu;

    /* Player starts at the documented start position, not moving. */
    g->player_x = PLAYER_START_X;
    g->player_y = PLAYER_START_Y;
    g->player_x_change = 0.0;

    /* Laser starts off the bottom, ready to fire. `.` is how we reach a
     * nested struct field when we have the struct itself (not a pointer). */
    g->laser.x = LASER_START_X;
    g->laser.y = LASER_START_Y;
    g->laser.x_change = 0.0;
    g->laser.y_change = LASER_SPEED;
    g->laser.state = LASER_READY;

    g->score = 0;
    g->game_over = false;

    /*
     * `for (int i = 0; ...)` declares i inside the loop (a C99 feature).
     * `&g->aliens[i]` is a pointer to that enemy; we then use the `->`
     * operator, which is "dereference then access a field" and is the
     * preferred C idiom when working with a pointer to a struct.
     */
    for (int i = 0; i < NUM_ENEMIES; i++) {
        Alien *a = &g->aliens[i];
        /* Initial spawn uses EXCLUSIVE upper bounds (randrange). */
        a->x = ffract(&g->rng_state, ENEMY_SPAWN_X_MIN, ENEMY_SPAWN_X_MAX);
        a->y = ffract(&g->rng_state, ENEMY_SPAWN_Y_MIN, ENEMY_SPAWN_Y_MAX);
        a->x_change = ENEMY_X_CHANGE_INIT;
        a->y_change = ENEMY_Y_CHANGE;
    }

    /* Hand the freshly built pointer back to the caller. */
    *out_state = g;
    return 0;
}

/* Free the single block created by game_init. Safe to call with NULL:
 * `free(NULL)` is defined by C to do nothing, so callers can always call
 * this without first testing the pointer. */
void game_free(GameState *state)
{
    free(state);
}

/* Place a freshly sized enemy into `*out`. Used by game_init() for the
 * first wave. */
void spawn_enemy(GameState *state, Alien *out)
{
    if (state == NULL || out == NULL) return;    /* NULL safety */
    out->x = ffract(&state->rng_state, ENEMY_SPAWN_X_MIN, ENEMY_SPAWN_X_MAX);
    out->y = ffract(&state->rng_state, ENEMY_SPAWN_Y_MIN, ENEMY_SPAWN_Y_MAX);
    out->x_change = ENEMY_X_CHANGE_INIT;
    out->y_change = ENEMY_Y_CHANGE;
}

/* Choose respawn coordinates for a destroyed enemy and write them into
 * *out_x / *out_y. Uses INCLUSIVE bounds (randint), unlike initial spawn. */
void respawn_coords(GameState *state, double *out_x, double *out_y)
{
    if (state == NULL || out_x == NULL || out_y == NULL) return;
    *out_x = fint_incl(&state->rng_state, ENEMY_SPAWN_X_MIN, ENEMY_SPAWN_X_MAX);
    *out_y = fint_incl(&state->rng_state, ENEMY_SPAWN_Y_MIN, ENEMY_SPAWN_Y_MAX);
}

/*
 * is_collision: is the alien close enough to the laser to count as a hit?
 *
 * The mathematically equivalent form is `sqrt(dx*dx + dy*dy) < 27`.
 * Squaring both sides gives `dx*dx + dy*dy < 27*27`, which is identical
 * but avoids the `sqrt()`. We use it for two reasons: it is faster, and it
 * sidesteps a rounding edge case at exactly the boundary (27.0).
 *
 * For a beginner: every `if` in this file follows the `NULL`-check-or-return
 * pattern -- the single most important habit in C memory safety.
 */
bool is_collision(double alien_x, double alien_y,
                  double laser_x, double laser_y)
{
    double dx = alien_x - laser_x;
    double dy = alien_y - laser_y;
    double r2 = COLLISION_RADIUS * COLLISION_RADIUS;
    return (dx * dx + dy * dy) < r2;
}

/* Set the player's horizontal velocity. The frontend calls this with
 * +0.5 / -0.5 on key press and 0.0 on key release. */
void set_player_direction(GameState *state, double x_change)
{
    if (state == NULL) return;
    state->player_x_change = x_change;
}

/*
 * update_player: one frame of player motion.
 *
 * We advance by the velocity, then CLAMP to the screen edges. The clamp
 * bounds are `PLAYER_MAX_X == SCREEN_W - PLAYER_WIDTH`, matching the
 * original design's `if x <= 0: ... elif x >= 736: ...` rule.
 */
void update_player(GameState *state)
{
    if (state == NULL) return;
    state->player_x += state->player_x_change;
    if (state->player_x <= 0.0) {
        state->player_x = 0.0;
    } else if (state->player_x >= PLAYER_MAX_X) {
        state->player_x = PLAYER_MAX_X;
    }
}

/* Arm the laser from the player's current x and mark it as in flight. */
void fire_laser(GameState *state)
{
    if (state == NULL) return;
    state->laser.x = state->player_x;
    state->laser.state = LASER_FIRE;
}

/*
 * update_aliens: one frame of enemy motion. The heaviest function, so it
 * is broken up with comments into the sub-steps of the original design.
 */
void update_aliens(GameState *state)
{
    /* NULL or already-over guard. */
    if (state == NULL || state->game_over) return;

    /*
     * For each enemy: check game-over condition, advance, bounce, and test
     * for a laser hit. The `break`/`return` after a game-over stops the
     * frame early -- no point iterating the rest of the aliens once the
     * match is over.
     */
    for (int i = 0; i < NUM_ENEMIES; i++) {
        Alien *a = &state->aliens[i];

        if (a->y > GAME_OVER_Y) {
            /* Push every enemy off the bottom of the screen. */
            for (int j = 0; j < NUM_ENEMIES; j++) {
                state->aliens[j].y = LASER_OFFSCREEN_Y;
            }
            state->game_over = true;
            return;
        }

        /* Horizontal motion. */
        a->x += a->x_change;

        /* Bounce off the left / right walls and step down by y_change.
         * These are the "wall hit" rules the player sees as the invader
         * shifting one row down each time it reaches the screen edge. */
        if (a->x <= 0.0) {
            a->x_change = 1.0;
            a->y += a->y_change;
        } else if (a->x >= PLAYER_MAX_X) {
            a->x_change = -1.0;
            a->y += a->y_change;
        }

        /* Hit detection. On a hit: add a point, reset the laser, and
         * respawn the enemy at a fresh random position. */
        if (is_collision(a->x, a->y, state->laser.x, state->laser.y)) {
            state->score += 1;
            state->laser.y = LASER_HIT_Y;
            state->laser.state = LASER_READY;
            respawn_coords(state, &a->x, &a->y);
        }
    }
}

/*
 * update_laser: one frame of laser motion.
 *
 * Two rules, in the order of the original design:
 *   1. If a laser has reached the top of the screen, recycle it to the
 *      start position.
 *   2. If a laser is currently in flight, move it up by y_change.
 *
 * The order matters: if the laser just arrived at y==0 we recycle first,
 * THEN the second block might move it up again, but state is no longer
 * FIRE so it does not. This preserves the original sequencing exactly.
 */
void update_laser(GameState *state)
{
    if (state == NULL) return;
    if (state->laser.y <= 0.0) {
        state->laser.y = LASER_START_Y;
        state->laser.state = LASER_READY;
    }
    if (state->laser.state == LASER_FIRE) {
        state->laser.y -= state->laser.y_change;
    }
}
