/*
 * ===========================================================================
 * main.c  --  PyVaders entry point
 * ===========================================================================
 *
 * The whole game lives in just two things:
 *
 *   1. A call to renderer_create() that opens the window and loads every
 *      asset and sound once.
 *   2. A loop that calls renderer_step() until it returns 0 (user quit or
 *      game over).
 *
 * Every resource is created in renderer_create and freed in renderer_destroy,
 * which is what makes this program memory-safe: no allocation outlives the
 * program, and no per-frame allocation is forgotten.
 *
 * To run:
 *     make all        # builds c/build/pyvaders (needs SDL2, SDL_ttf,
 *                     # SDL_mixer installed on the system)
 *     ./c/build/pyvaders
 */

#include <stdio.h>
#include <stdlib.h>

#include "render.h"

/*
 * main() is the C entry point -- the OS calls it when the program starts.
 * Its return value is the program's exit code: 0 = success, non-zero =
 * failure. CI and shells use this to decide whether the program worked.
 */
int main(void)
{
    /*
     * Step 1: build the renderer. If anything fatal goes wrong (SDL will
     * not init, an asset is missing) it prints a message and returns NULL.
     */
    Renderer *r = renderer_create();
    if (r == NULL) {
        fprintf(stderr, "fatal: could not start game\n");
        return 1;   /* non-zero exit tells the shell the program failed */
    }

    /*
     * Step 2: the game loop. Each call to renderer_step handles input,
     * advances the simulation, and draws a frame. It returns 0 when the
     * user closes the window or a game-over is reached, at which point we
     * stop.
     */
    while (renderer_step(r) != 0) {
        /* keep looping */
    }

    /*
     * Step 3: clean up EVERYTHING. This releases the textures, sounds,
     * font, game state and the SDL subsystems. Because every Free is
     * NULL-safe and runs unconditionally, there is no branch where a leak
     * can hide.
     */
    renderer_destroy(r);

    return 0;
}
