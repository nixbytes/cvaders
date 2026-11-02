"""Unit tests for the pure PyVaders game logic (src/game.py).

These mirror the C test suite and lock down the behaviour of the original
src/main.py. They exercise only the pure logic -- no pygame import required.

The suite is pytest-compatible but degrades gracefully: if pytest is not
installed it can be run directly (``python3 -m pytest`` is not required):

    python3 tests/test_game.py

It discovers every ``test_*`` function and runs them with a small stdlib
fallback runner, keeping the same assertions in both modes.
"""

import math
import random
import sys
import os

# Make the project root importable so ``import src.game`` resolves when run
# directly (pytest uses conftest.py instead).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    import pytest
    from pytest import approx
except Exception:  # pragma: no cover - fallback when pytest absent
    pytest = None

    class _Approx:
        def __init__(self, expected, rel=1e-9, abs=1e-9):
            self.expected = expected
            self.rel = rel
            self.abs = abs

        def __eq__(self, other):
            return abs(self.expected - other) <= max(self.abs,
                                                     self.rel * abs(self.expected))

    approx = lambda x, **k: _Approx(x, **k)

from src.game import (
    init_game,
    update_player,
    update_aliens,
    update_laser,
    fire_laser,
    set_player_direction,
    is_collision,
    respawn_coords,
    spawn_enemy,
    NUM_ENEMIES,
    PLAYER_MAX_X,
    LASER_START_Y,
    LASER_HIT_Y,
    GAME_OVER_Y,
    COLLISION_RADIUS,
)


# --- T1 / T2: collision math ---
def test_collision_true_within_radius():
    assert is_collision(0.0, 0.0, 0.0, 0.0) is True
    assert is_collision(10.0, 10.0, 0.0, 0.0) is True   # 14.14 < 27
    assert is_collision(20.0, 20.0, 0.0, 0.0) is False   # 28.28 > 27
    assert is_collision(25.0, 25.0, 0.0, 0.0) is False


def test_collision_boundary_at_radius():
    assert is_collision(COLLISION_RADIUS, 0.0, 0.0, 0.0) is False
    assert is_collision(COLLISION_RADIUS - 1, 0.0, 0.0, 0.0) is True


def test_collision_equivalent_to_original_sqrt_form():
    ax, ay, lx, ly = 11.3, 17.7, 5.5, 8.2
    original = math.sqrt((ax - lx) ** 2 + (ay - ly) ** 2) < 27
    assert is_collision(ax, ay, lx, ly) == original


# --- T3: player clamping ---
def test_player_clamps_to_zero():
    s = init_game(seed=1)
    s.player_x = 0
    set_player_direction(s, -0.5)
    update_player(s)
    assert s.player_x == 0


def test_player_clamps_to_max():
    s = init_game(seed=1)
    s.player_x = 737
    set_player_direction(s, 0.5)
    update_player(s)
    assert s.player_x == PLAYER_MAX_X


def test_player_moves_when_unclamped():
    s = init_game(seed=1)
    s.player_x = 100
    set_player_direction(s, 0.5)
    update_player(s)
    assert s.player_x == approx(100.5)


# --- T4: alien wall bounce ---
def test_alien_bounces_left_wall():
    s = init_game(seed=1)
    a = s.aliens[0]
    a.x = 0
    a.y = 50
    a.x_change = -1
    update_aliens(s)
    assert a.x_change == 1
    assert a.y == 50 + a.y_change


def test_alien_bounces_right_wall():
    s = init_game(seed=1)
    a = s.aliens[0]
    a.x = PLAYER_MAX_X
    a.y = 50
    a.x_change = 1
    update_aliens(s)
    assert a.x_change == -1
    assert a.y == 50 + a.y_change


# --- T5: game over ---
def test_game_over_when_alien_descends():
    s = init_game(seed=1)
    s.aliens[0].y = GAME_OVER_Y + 1
    update_aliens(s)
    assert s.game_over is True
    assert all(a.y == 2000 for a in s.aliens)


def test_no_game_over_when_aliens_high():
    s = init_game(seed=1)
    for a in s.aliens:
        a.y = 50
    update_aliens(s)
    assert s.game_over is False


# --- T6: collision respawn + score ---
def test_collision_respawns_and_scores():
    s = init_game(seed=1)
    a = s.aliens[0]
    a.x = 100.0
    a.y = 50.0
    a.x_change = 0
    s.laser.x = 100.0
    s.laser.y = 50.0
    s.laser.state = "fire"
    score_before = s.score
    update_aliens(s)
    assert s.score == score_before + 1
    assert s.laser.state == "ready"
    assert s.laser.y == LASER_HIT_Y
    assert 0 <= a.x <= 800
    assert 50 <= a.y <= 150


# --- T7: laser reset off top ---
def test_laser_resets_when_off_top():
    s = init_game(seed=1)
    s.laser.y = 0
    s.laser.state = "fire"
    update_laser(s)
    assert s.laser.y == LASER_START_Y
    assert s.laser.state == "ready"


def test_laser_moves_up_when_firing():
    s = init_game(seed=1)
    s.laser.y = 300
    s.laser.state = "fire"
    update_laser(s)
    assert s.laser.y == 300 - s.laser.y_change


def test_laser_does_not_move_when_ready():
    s = init_game(seed=1)
    s.laser.y = 300
    s.laser.state = "ready"
    update_laser(s)
    assert s.laser.y == 300


# --- T8: spawn ranges (rng inclusivity) ---
def test_spawn_ranges_are_bounds():
    rng = random.Random(12345)
    for _ in range(2000):
        a = spawn_enemy(rng)
        assert 0 <= a.x < 800       # randrange exclusive upper
        assert 50 <= a.y < 150      # randrange exclusive upper


def test_respawn_uses_inclusive_bounds():
    rng = random.Random(98765)
    seen_x = set()
    seen_y = set()
    for _ in range(5000):
        x, y = respawn_coords(rng)
        seen_x.add(x)
        seen_y.add(y)
    assert 0 in seen_x and 800 in seen_x   # randint inclusive
    assert 50 in seen_y and 150 in seen_y


# --- T9: constants / initial state ---
def test_init_creates_six_enemies():
    s = init_game(seed=7)
    assert len(s.aliens) == NUM_ENEMIES == 6
    assert s.score == 0
    assert s.player_x == 370
    assert s.player_y == 480
    assert s.laser.state == "ready"


# --- T10: fire semantics (guard identity-vs-equality bug) ---
def test_fire_arms_laser_at_player():
    s = init_game(seed=3)
    s.player_x = 250
    fire_laser(s)
    assert s.laser.state == "fire"
    assert s.laser.x == 250


def test_fire_state_is_value_not_identity():
    s = init_game(seed=3)
    s.laser.y = 300
    s.laser.state = str("fire")  # not guaranteed interned
    update_laser(s)
    assert s.laser.y == 300 - s.laser.y_change


def test_seeded_runs_are_deterministic():
    a = init_game(seed=42)
    b = init_game(seed=42)
    assert [e.x for e in a.aliens] == [e.x for e in b.aliens]
    assert [e.y for e in a.aliens] == [e.y for e in b.aliens]


# --- Fallback runner (used only when pytest is unavailable) ---
def _fallback_run():
    import inspect
    import traceback

    tests = sorted(
        (n, f) for n, f in inspect.getmembers(sys.modules[__name__],
                                               inspect.isfunction)
        if n.startswith("test_")
    )
    passed = failed = 0
    for name, fn in tests:
        try:
            fn()
            passed += 1
            print("PASS %s" % name)
        except Exception:
            failed += 1
            print("FAIL %s" % name)
            traceback.print_exc()
    print("\n%d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    if pytest is not None:
        raise SystemExit(pytest.main([__file__, "-v"]))
    raise SystemExit(_fallback_run())
