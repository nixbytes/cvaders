/*
 * ===========================================================================
 * test_game.c  --  The unit tests for the cvanders C core
 * ===========================================================================
 *
 * HOW TO READ THIS FILE (for new C programmers)
 *
 * 1. A single "test" is a function called `test_...`. Each one builds a
 *    GameState, drives it through the game logic, then CHECKS the result
 *    with TEST_ASSERT_*.
 *
 * 2. If ANY assert fails, the function returns immediately and Unity marks
 *    it failed. If the function runs to the end, it counts as PASS. That is
 *    why every body starts with BEGIN_TEST() -- it tells the counter "one
 *    test is running now".
 *
 * 3. `main()` at the bottom just calls every test function one after
 *    another, then hands the result back to the OS via UNITY_END().
 *
 * 4. The tests deliberately cover the tricky bits of the original game:
 *    - collision math (T1/T2)
 *    - player clamping (T3)
 *    - alien wall bounce (T4)
 *    - game-over trigger (T5)
 *    - respawn + score on a hit (T6)
 *    - laser recycling (T7)
 *    - RNG ranges: exclusive spawn vs inclusive respawn (T8)
 *    - initial state (T9)
 *    - fire semantics (T10): firing arms the laser, which is the correct
 *      value-based behaviour -- no string-identity subtleties.
 *    - NULL-pointer safety (T11)
 */
#include "unity.h"
#include "cvaders.h"
#include <math.h>

/*
 * BEGIN_TEST is a `do { ... } while (0)` "statement wrapper" -- a common C
 * idiom that makes a multi-statement macro behave like ONE statement so it
 * reads cleanly inside `if`/`while` blocks. Here it simply bumps the
 * running counter once at the top of every test.
 */
#define BEGIN_TEST() do { unity_state.tests_run++; } while (0)

/* --- T1/T2: collision math --- */
void test_collision_true_within_radius(void)
{
    BEGIN_TEST();
    TEST_ASSERT_TRUE(is_collision(0.0, 0.0, 0.0, 0.0));
    TEST_ASSERT_TRUE(is_collision(10.0, 10.0, 0.0, 0.0)); /* 14.14 < 27 */
    TEST_ASSERT_FALSE(is_collision(20.0, 20.0, 0.0, 0.0)); /* 28.28 > 27 */
    TEST_ASSERT_FALSE(is_collision(25.0, 25.0, 0.0, 0.0));
}

void test_collision_boundary_at_radius(void)
{
    BEGIN_TEST();
    TEST_ASSERT_FALSE(is_collision(COLLISION_RADIUS, 0.0, 0.0, 0.0));
    TEST_ASSERT_TRUE(is_collision(COLLISION_RADIUS - 1, 0.0, 0.0, 0.0));
}

void test_collision_equivalent_to_original_sqrt_form(void)
{
    BEGIN_TEST();
    double ax = 11.3, ay = 17.7, lx = 5.5, ly = 8.2;
    double original = sqrt((ax - lx) * (ax - lx) + (ay - ly) * (ay - ly)) < 27.0;
    TEST_ASSERT_TRUE(is_collision(ax, ay, lx, ly) == original);
}

/* --- T3: player clamping --- */
void test_player_clamps_to_zero(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->player_x = 0.0;
    set_player_direction(g, -0.5);
    update_player(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, g->player_x);
    game_free(g);
}

void test_player_clamps_to_max(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->player_x = 737.0;
    set_player_direction(g, 0.5);
    update_player(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, PLAYER_MAX_X, g->player_x);
    game_free(g);
}

void test_player_moves_when_unclamped(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->player_x = 100.0;
    set_player_direction(g, 0.5);
    update_player(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.5, g->player_x);
    game_free(g);
}

/* --- T4: alien wall bounce --- */
void test_alien_bounces_left_wall(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    Alien *a = &g->aliens[0];
    a->x = 0.0; a->y = 50.0; a->x_change = -1.0;
    update_aliens(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, a->x_change);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50.0 + a->y_change, a->y);
    game_free(g);
}

void test_alien_bounces_right_wall(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    Alien *a = &g->aliens[0];
    a->x = PLAYER_MAX_X; a->y = 50.0; a->x_change = 1.0;
    update_aliens(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, a->x_change);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50.0 + a->y_change, a->y);
    game_free(g);
}

/* --- T5: game over --- */
void test_game_over_when_alien_descends(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->aliens[0].y = GAME_OVER_Y + 1.0;
    update_aliens(g);
    TEST_ASSERT_TRUE(g->game_over);
    for (int i = 0; i < NUM_ENEMIES; i++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, LASER_OFFSCREEN_Y, g->aliens[i].y);
    }
    game_free(g);
}

void test_no_game_over_when_aliens_high(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    for (int i = 0; i < NUM_ENEMIES; i++) g->aliens[i].y = 50.0;
    update_aliens(g);
    TEST_ASSERT_FALSE(g->game_over);
    game_free(g);
}

/* --- T6: collision respawn + score --- */
void test_collision_respawns_and_scores(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    Alien *a = &g->aliens[0];
    a->x = 100.0; a->y = 50.0; a->x_change = 0.0;
    g->laser.x = 100.0; g->laser.y = 50.0; g->laser.state = LASER_FIRE;
    int before = g->score;
    update_aliens(g);
    TEST_ASSERT_EQUAL_INT(before + 1, g->score);
    TEST_ASSERT(g->laser.state == LASER_READY);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LASER_HIT_Y, g->laser.y);
    TEST_ASSERT(a->x >= 0.0 && a->x <= ENEMY_SPAWN_X_MAX);
    TEST_ASSERT(a->y >= ENEMY_SPAWN_Y_MIN && a->y <= ENEMY_SPAWN_Y_MAX);
    game_free(g);
}

/* --- T7: laser reset off top --- */
void test_laser_resets_when_off_top(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->laser.y = 0.0; g->laser.state = LASER_FIRE;
    update_laser(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LASER_START_Y, g->laser.y);
    TEST_ASSERT(g->laser.state == LASER_READY);
    game_free(g);
}

void test_laser_moves_up_when_firing(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->laser.y = 300.0; g->laser.state = LASER_FIRE;
    update_laser(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300.0 - g->laser.y_change, g->laser.y);
    game_free(g);
}

void test_laser_does_not_move_when_ready(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));
    g->laser.y = 300.0; g->laser.state = LASER_READY;
    update_laser(g);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300.0, g->laser.y);
    game_free(g);
}

/* --- T8: spawn ranges --- */
void test_spawn_ranges_are_bounds(void)
{
    BEGIN_TEST();
    for (int i = 0; i < 2000; i++) {
        GameState *g = NULL;
        TEST_ASSERT_EQUAL_INT(0, game_init((uint32_t)(i + 1), &g));
        double a = g->aliens[0].x, b = g->aliens[0].y;
        TEST_ASSERT(a >= ENEMY_SPAWN_X_MIN && a < ENEMY_SPAWN_X_MAX);
        TEST_ASSERT(b >= ENEMY_SPAWN_Y_MIN && b < ENEMY_SPAWN_Y_MAX);
        game_free(g);
    }
}

void test_respawn_uses_inclusive_bounds(void)
{
    BEGIN_TEST();
    /*
     * Respawn is inclusive on [min, max] (randint), unlike init spawn which
     * is exclusive on [min, max) (randrange). To observe the maximum without
     * relying on flaky per-iteration reseeds, draw many coordinates from a
     * single deterministic RNG stream and require both extremes to appear.
     */
    int saw_lo_x = 0, saw_hi_x = 0, saw_lo_y = 0, saw_hi_y = 0;
    GameState *g = NULL;
    game_init(1234, &g);
    for (int i = 0; i < 60000; i++) {
        double x, y;
        respawn_coords(g, &x, &y);
        /* No value may escape the inclusive bounds. */
        TEST_ASSERT(x >= ENEMY_SPAWN_X_MIN && x <= ENEMY_SPAWN_X_MAX);
        TEST_ASSERT(y >= ENEMY_SPAWN_Y_MIN && y <= ENEMY_SPAWN_Y_MAX);
        if (x == ENEMY_SPAWN_X_MIN) saw_lo_x = 1;
        if (x == ENEMY_SPAWN_X_MAX) saw_hi_x = 1;
        if (y == ENEMY_SPAWN_Y_MIN) saw_lo_y = 1;
        if (y == ENEMY_SPAWN_Y_MAX) saw_hi_y = 1;
    }
    game_free(g);
    TEST_ASSERT_TRUE(saw_lo_x);
    TEST_ASSERT_TRUE(saw_hi_x);
    TEST_ASSERT_TRUE(saw_lo_y);
    TEST_ASSERT_TRUE(saw_hi_y);
}

/* --- T9: initial state --- */
void test_init_creates_six_enemies(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(7, &g));
    TEST_ASSERT_EQUAL_INT(NUM_ENEMIES, 6);
    TEST_ASSERT_EQUAL_INT(0, g->score);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, PLAYER_START_X, g->player_x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, PLAYER_START_Y, g->player_y);
    TEST_ASSERT(g->laser.state == LASER_READY);
    game_free(g);
}

/* --- T10: fire semantics --- */
void test_fire_arms_laser_at_player(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(3, &g));
    g->player_x = 250.0;
    fire_laser(g);
    TEST_ASSERT(g->laser.state == LASER_FIRE);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 250.0, g->laser.x);
    game_free(g);
}

void test_seeded_runs_are_deterministic(void)
{
    BEGIN_TEST();
    GameState *a = NULL, *b = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(42, &a));
    TEST_ASSERT_EQUAL_INT(0, game_init(42, &b));
    for (int i = 0; i < NUM_ENEMIES; i++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, a->aliens[i].x, b->aliens[i].x);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, a->aliens[i].y, b->aliens[i].y);
    }
    game_free(a);
    game_free(b);
}

/* --- T11: NULL-safety of every core function --- */
void test_null_safety(void)
{
    BEGIN_TEST();
    double dummy_x = 0.0, dummy_y = 0.0;
    Alien dummy;
    spawn_enemy(NULL, &dummy);
    respawn_coords(NULL, &dummy_x, &dummy_y);
    set_player_direction(NULL, 1.0);
    update_player(NULL);
    fire_laser(NULL);
    update_aliens(NULL);
    update_laser(NULL);
    /* No crash == pass. */
    TEST_ASSERT_TRUE(1);
}

/*
 * main() is the program entry point (C has exactly one; the OS calls it).
 * We run every test and return non-zero to the shell if any failed, so
 * `make test` exits 0 only when everything passes -- which is exactly what
 * a CI pipeline or the `make valgrind` target rely on.
 */
int main(void)
{
    test_collision_true_within_radius();
    test_collision_boundary_at_radius();
    test_collision_equivalent_to_original_sqrt_form();
    test_player_clamps_to_zero();
    test_player_clamps_to_max();
    test_player_moves_when_unclamped();
    test_alien_bounces_left_wall();
    test_alien_bounces_right_wall();
    test_game_over_when_alien_descends();
    test_no_game_over_when_aliens_high();
    test_collision_respawns_and_scores();
    test_laser_resets_when_off_top();
    test_laser_moves_up_when_firing();
    test_laser_does_not_move_when_ready();
    test_spawn_ranges_are_bounds();
    test_respawn_uses_inclusive_bounds();
    test_init_creates_six_enemies();
    test_fire_arms_laser_at_player();
    test_seeded_runs_are_deterministic();
    test_null_safety();

    return UNITY_END();
}
