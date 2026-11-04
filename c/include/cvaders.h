#ifndef CVANDERS_H
#define CVANDERS_H

/*
 * ===========================================================================
 * cvaders.h  --  the public face of the game's brain
 * ===========================================================================
 *
 * 1) WHY A HEADER FILE?
 *    In C, a `.h` (header) file is a "contract" (or interface). It describes
 *    what a module can do -- its data types and function names -- WITHOUT
 *    showing the implementation. Other files `#include` this header to use
 *    the game logic. The matching implementation lives in `src/game.c`.
 *
 * 2) THE INCLUDE GUARD
 *    The `#ifndef / #define / #endif` at the very top and bottom makes sure
 *    the contents are only pasted in ONCE even if a file `#include`s this
 *    header indirectly several times. It prevents "redefinition" compile
 *    errors. Think of it as a "have I already been read?" latch.
 *
 * 3) DESIGN OF THIS PORT
 *    This is a C implementation of the classic Space-Invaders game. It
 *    contains NO rendering or audio code, so it can be unit-tested in
 *    isolation and reused by the SDL frontend (c/src/render.c, c/src/main.c).
 *
 *    Memory safety:
 *   - All mutable state lives in a single GameState struct.
 *   - Enemies are a fixed-size array of struct Alien (one struct per
 *     enemy), replacing the original game's four parallel arrays
 *     (alien_x, alien_y, ...), which risk running out of step with each
 *     other. One struct per enemy keeps x/y glued together.
 *   - The logic core uses exactly ONE heap allocation (the GameState).
 *     It is freed by game_free(). No other malloc/free means there is
 *     almost nothing to leak.
 *
 * 4) NEW-COMER TIPS
 *      - `double` is the C type for a 64-bit floating-point number.
 *      - `int` is a whole number, `bool` is true/false, `uint32_t` is an
 *        unsigned 32-bit integer (used by the random-number generator).
 *      - A line starting with `#` is a "preprocessor" directive that runs
 *        before the compiler even sees the code.
 */

#include <stddef.h>    /* provides the NULL pointer constant */
#include <stdint.h>    /* fixed-width ints like uint32_t */
#include <stdbool.h>   /* the true/false/bool keywords */

/* --- Constants --- */
#define SCREEN_W 800
#define SCREEN_H 600
#define NUM_ENEMIES 6
#define COLLISION_RADIUS 27.0
#define GAME_OVER_Y 200.0

#define PLAYER_START_X 370.0
#define PLAYER_START_Y 480.0
#define PLAYER_MAX_X 736.0  /* SCREEN_W - player image width */

#define LASER_START_X 0.0
#define LASER_START_Y 500.0
#define LASER_SPEED 5.0
#define LASER_HIT_Y 400.0
#define LASER_OFFSCREEN_Y 2000.0

#define ENEMY_X_CHANGE_INIT 0.3
#define ENEMY_Y_CHANGE 30.0

/* Init spawn uses [0,800) / [50,150) exclusive; respawn uses [0,800]/[50,150]
 * inclusive. Both ranges are preserved from the original design. */
#define ENEMY_SPAWN_X_MIN 0.0
#define ENEMY_SPAWN_X_MAX 800.0
#define ENEMY_SPAWN_Y_MIN 50.0
#define ENEMY_SPAWN_Y_MAX 150.0

/*
 * typedef enum { ... } Name;
 *
 * An 'enum' (enumeration) is a way to create a set of named integer constants.
 * It makes the code more readable: instead of using 0 or 1, we use
 * LASER_READY or LASER_FIRE.
 */
typedef enum {
    LASER_READY,
    LASER_FIRE
} LaserState;

/*
 * typedef struct { ... } Name;
 *
 * A 'struct' (structure) groups different variables together into a single
 * package. 'typedef' allows us to give this package a name (like Alien),
 * so we don't have to write 'struct Alien' every time we use it.
 */
typedef struct {
    double x;
    double y;
    double x_change;
    double y_change;
} Alien;

/*
 * struct Laser -- the player's projectile.
 *
 * x_change is 0.0 because the laser only ever travels UP, so its x never
 * changes. y_change is the upward speed (LASER_SPEED). `state` records
 * whether the laser is "ready" (waiting to be fired) or "fire" (in flight).
 */
typedef struct {
    double x;
    double y;
    double x_change;
    double y_change;
    LaserState state;
} Laser;

/*
 * struct GameState -- the single container for everything the game knows,
 * at every instant.
 *
 * In the original design these were ~15 independent module-level
 * variables. Bundling them into one struct is the idiomatic C fix: you pass
 * the whole world around by pointer (GameState *) instead of juggling global
 * variables. This also makes the game testable: each test can hand a fresh
 * GameState to the functions.
 */
typedef struct {
    double player_x;
    double player_y;
    double player_x_change;   /* horizontal velocity, set by keyboard */
    Alien aliens[NUM_ENEMIES];
    Laser laser;
    int score;
    bool game_over;
    /* Seedable RNG state so logic runs are reproducible for tests. A fixed
     * seed produces the exact same sequence of "random" positions every run. */
    uint32_t rng_state;
} GameState;

/*
 * ===========================================================================
 * PUBLIC FUNCTIONS
 * ===========================================================================
 *
 * The "output parameter" pattern:
 *   Many of these functions take a pointer like `GameState **out_state`
 *   or `double *out_x`. C functions return only ONE value by their return
 *   type, so to hand back more than one thing we pass the ADDRESS of where
 *   the caller wants the result stored, and write into it. The `*` means
 *   "pointer to"; writing `*ptr = value` means "store value in the location
 *   ptr points at". A `**` (pointer to a pointer) is used for `out_state` so
 *   the function can hand back a newly allocated GameState to its caller.
 */

/* Initialise a fresh game with NUM_ENEMIES enemies placed at random.
 * If seed == 0 a default seed is used. The function ALLOCATES a GameState
 * on the heap and stores its address into *out_state; the caller MUST
 * eventually free it with game_free(). Returns 0 on success, -1 on failure
 * (e.g. out of memory, or a NULL out_state). */
int game_init(uint32_t seed, GameState **out_state);

/* Free a state created by game_init(). It is safe to pass NULL -- a common
 * C idiom that lets callers write `game_free(g)` without a preceding check. */
void game_free(GameState *state);

/* Place a freshly allocated/sized enemy into the Alien pointed to by `out`.
 * init_game() calls this for the first wave. */
void spawn_enemy(GameState *state, Alien *out);

/* Choose respawn coordinates for a destroyed enemy and write them into
 * *out_x / *out_y. Uses INCLUSIVE bounds (randint), unlike init spawn. */
void respawn_coords(GameState *state, double *out_x, double *out_y);

/* Collision test: true when the straight-line distance between the alien and
 * the laser is less than COLLISION_RADIUS. Implemented with squared
 * distances so we never have to call sqrt() (faster and avoids a rounding
 * edge case). */
bool is_collision(double alien_x, double alien_y,
                  double laser_x, double laser_y);

/* Set the player's horizontal velocity. Called by the frontend on key press
 * (+0.5 / -0.5) and on key release (0.0). */
void set_player_direction(GameState *state, double x_change);

/* Advance the player by one frame: apply velocity, then clamp to screen
 * edges [0, PLAYER_MAX_X]. */
void update_player(GameState *state);

/* Arm the laser from the player's current position and mark it "fire". */
void fire_laser(GameState *state);

/* Advance all enemies one frame: move, bounce off walls, then test each for
 * a collision with the laser. On a hit it bumps the score and respawns the
 * enemy. If any enemy has descended past GAME_OVER_Y it sets game_over and
 * pushes every enemy off-screen. */
void update_aliens(GameState *state);

/* Advance the laser one frame. If a firing laser reaches the top it recycles
 * to the start position. A "ready" laser does not move. */
void update_laser(GameState *state);

#endif /* PYVADERS_H */
