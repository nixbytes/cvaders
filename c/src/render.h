/*
 * ===========================================================================
 * render.h  --  SDL frontend for PyVaders
 * ===========================================================================
 *
 * This is the "frontend" half of the C port. It wraps SDL2 for window,
 * text and sound, and drives the pure logic in `game.c` from a game loop.
 *
 * Why split the core from the frontend?
 *   - The core is testable WITHOUT a display/audio device.
 *   - The core is platform-agnostic; the frontend owns every OS/SDL
 *     resource.
 *   - The frontend is where memory-leak risk lives (SDL textures and
 *     sound clips are HEAP allocations). It releases them ALL on exit,
 *     which is what makes the SDL half memory-safe.
 */
#ifndef RENDER_H
#define RENDER_H

#include "cvaders.h"

/* Opaque handle: the rest of the program never sees SDL types. This is a
 * "data hiding" trick that keeps the heavy SDL dependency out of our other
 * source files. */
typedef struct Renderer Renderer;

/*
 * Create a renderer: open a window, load every image/sound/font.
 *
 * Returns a Renderer* on success, NULL on failure (e.g. an asset file is
 * missing). On failure the caller MUST NOT call renderer_destroy with the
 * NULL it got -- the helper is written to be NULL-safe anyway.
 */
Renderer *renderer_create(void);

/* Free everything: destroy textures, release sounds, close the font, tear
 * down SDL. Safe to call with NULL. */
void renderer_destroy(Renderer *r);

/* Run one full frame: drain events, update the game, and draw.
 * Returns 1 if the game loop should keep running, 0 if the user quit or
 * the match is over. */
int renderer_step(Renderer *r);

/* Render one static frame. */
void renderer_present(Renderer *r);

#endif /* RENDER_H */
