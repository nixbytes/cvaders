"""Pure game logic for PyVaders (no pygame dependency).

This module mirrors the behaviour of the original ``src/main.py`` but with
no side effects on import, so the logic can be exercised by unit tests
independently of the SDL/pygame frontend.

Design notes
------------
* All mutable state lives in a single :class:`GameState` struct. The original
  used ~15 module-level globals plus four *parallel* alien lists
  (``alien_x``/``alien_y``/``alien_x_change``/``alien_y_change``); these are
  combined here into a :class:`Alien` per-enemy struct to remove the
  index-desync hazard of parallel arrays.
* A seedable RNG (:class:`random.Random`) replaces the module-level
  ``random`` calls so runs are reproducible for testing.
* Behaviourally faithful notes (preserve the original quirks):
  - Spawning on init uses ``randrange`` (exclusive upper bound) while a
    post-collision respawn uses ``randint`` (inclusive upper bound). This
    asymmetry exists in the original and is kept on purpose.
  - On a hit the laser resets to ``y = 400``; when off the top of the screen
    it resets to ``y = 500``. Two different reset values, both preserved.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional
import random

# --- Constants (from the original main.py) ---
SCREEN_W = 800
SCREEN_H = 600
NUM_ENEMIES = 6
COLLISION_RADIUS = 27
GAME_OVER_Y = 200

PLAYER_START_X = 370
PLAYER_START_Y = 480
PLAYER_MAX_X = 736  # SCREEN_W - player image width

LASER_START_X = 0
LASER_START_Y = 500
LASER_SPEED = 5
LASER_HIT_Y = 400  # laser y on a successful hit

ENEMY_X_CHANGE_INIT = 0.3
ENEMY_Y_CHANGE = 30

# Init spawn ranges use randrange (exclusive end).
ENEMY_SPAWN_X = (0, 800)
ENEMY_SPAWN_Y = (50, 150)


@dataclass
class Alien:
    x: float
    y: float
    x_change: float = ENEMY_X_CHANGE_INIT
    y_change: float = ENEMY_Y_CHANGE


@dataclass
class Laser:
    x: float = LASER_START_X
    y: float = LASER_START_Y
    x_change: float = 0.0
    y_change: float = LASER_SPEED
    state: str = "ready"


@dataclass
class GameState:
    player_x: float = PLAYER_START_X
    player_y: float = PLAYER_START_Y
    player_x_change: float = 0.0
    aliens: list = field(default_factory=list)
    laser: Laser = field(default_factory=Laser)
    score: int = 0
    game_over: bool = False
    rng: random.Random = field(default_factory=lambda: random.Random(0))


def init_game(seed: Optional[int] = None) -> GameState:
    """Create a fresh game with ``NUM_ENEMIES`` enemies placed at random."""
    rng = random.Random(seed)
    state = GameState()
    state.rng = rng
    for _ in range(NUM_ENEMIES):
        state.aliens.append(spawn_enemy(rng))
    return state


def spawn_enemy(rng: random.Random) -> Alien:
    """Place a new enemy. Init spawn uses randrange (exclusive upper bound)."""
    return Alien(
        x=rng.randrange(*ENEMY_SPAWN_X),
        y=rng.randrange(*ENEMY_SPAWN_Y),
    )


def respawn_coords(rng: random.Random):
    """Return respawn x, y for a destroyed enemy.

    The original used ``randint`` (inclusive upper bound) for respawns,
    distinct from init spawning. Preserved on purpose.
    """
    return rng.randint(*ENEMY_SPAWN_X), rng.randint(*ENEMY_SPAWN_Y)


def is_collision(alien_x: float, alien_y: float,
                 laser_x: float, laser_y: float) -> bool:
    """Euclidean distance collision test (squared form, no sqrt)."""
    dx = alien_x - laser_x
    dy = alien_y - laser_y
    return dx * dx + dy * dy < COLLISION_RADIUS * COLLISION_RADIUS


def set_player_direction(state: GameState, x_change: float) -> None:
    """Set the player's horizontal velocity (±0.5 for left/right, 0 to stop)."""
    state.player_x_change = x_change


def update_player(state: GameState) -> None:
    """Apply the player's velocity and clamp to the screen edges [0, 736]."""
    state.player_x += state.player_x_change
    if state.player_x <= 0:
        state.player_x = 0
    elif state.player_x >= PLAYER_MAX_X:
        state.player_x = PLAYER_MAX_X


def fire_laser(state: GameState) -> None:
    """Arm the laser from the player's current position."""
    state.laser.x = state.player_x
    state.laser.state = "fire"


def update_aliens(state: GameState) -> None:
    """Move enemies, bounce them off the walls, and resolve collisions.

    Sets ``state.game_over`` and relocates all enemies off-screen when any
    enemy descends past ``GAME_OVER_Y`` (the original drew a "GAME OVER"
    banner but never stopped the loop; the flag now lets the frontend stop
    cleanly).
    """
    if state.game_over:
        return

    for alien in state.aliens:
        if alien.y > GAME_OVER_Y:
            for other in state.aliens:
                other.y = 2000
            state.game_over = True
            return

        alien.x += alien.x_change

        if alien.x <= 0:
            alien.x_change = 1
            alien.y += alien.y_change
        elif alien.x >= PLAYER_MAX_X:
            alien.x_change = -1
            alien.y += alien.y_change

        if is_collision(alien.x, alien.y, state.laser.x, state.laser.y):
            state.score += 1
            state.laser.y = LASER_HIT_Y
            state.laser.state = "ready"
            alien.x, alien.y = respawn_coords(state.rng)


def update_laser(state: GameState) -> None:
    """Move a firing laser upward and recycle it when it leaves the screen."""
    if state.laser.y <= 0:
        state.laser.y = LASER_START_Y
        state.laser.state = "ready"

    if state.laser.state == "fire":
        state.laser.y -= state.laser.y_change
