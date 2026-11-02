"""PyVaders pygame frontend.

A thin rendering/input layer around the pure logic in :mod:`src.game`.
All gameplay state and rules live in ``game.py`` (unit-tested without
pygame); this module only owns the SDL window, image loading, sound and
event handling.

Behavioural fixes vs the original script:
  * Sound objects are loaded once at startup, not every frame (the original
    constructed ``mixer.Sound(...)`` on every SPACE press / explosion).
  * The asset case bug is fixed: the on-disk file is ``explosion.wav``
    (lowercase); the original code loaded ``Explosion.wav``.
  * Game-over now stops the loop via ``game.game_over`` (the original drew
    the banner but kept running).
"""

import os
import random

import pygame
from pygame import mixer

from src import game

# The original repo ships this directory misspelled as ``assests``. We keep
# the historical name so existing assets resolve, but centralise the path
# here so it is easy to correct later.
ASSET_DIR = "assests"


def asset_path(name):
    return os.path.join(ASSET_DIR, name)


class Game:
    """Owns the pygame surface, textures, sounds and the game loop."""

    def __init__(self, seed=0):
        pygame.init()
        self.screen = pygame.display.set_mode((game.SCREEN_W, game.SCREEN_H))

        self.background = self._load_image("mountains03-1920-x-1080_full.png")
        self.player_image = self._load_image("darkgrey_02.png")
        self.alien_image = self._load_image("alien_enemy.png")
        self.laser_image = self._load_image("laser.png")
        self.icon = self._load_image("alien.png")

        self.laser_sound = self._load_sound("laser.wav")
        self.explosion_sound = self._load_sound("explosion.wav")

        self.font = game_font()
        self.over_font = game_font()

        self.state = game.init_game(seed=seed)
        self.running = True

        pygame.display.set_caption("PyVaders")
        pygame.display.set_icon(self.icon)

        # Background music (loaded once, on failure we continue silently).
        if mixer.init():
            mixer.music.load(asset_path("Dark Fantasy Studio - Particle voyager.mp3"))
            mixer.music.play(-1)

    # --- resource helpers (one-time load, no per-frame allocation) ---
    def _load_image(self, name):
        img = pygame.image.load(asset_path(name)).convert()
        return img

    def _load_sound(self, name):
        return mixer.Sound(asset_path(name))

    # --- input ---
    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_LEFT:
                    game.set_player_direction(self.state, -0.5)
                elif event.key == pygame.K_RIGHT:
                    game.set_player_direction(self.state, 0.5)
                elif event.key == pygame.K_SPACE:
                    if self.laser_sound:
                        self.laser_sound.play()
                    game.fire_laser(self.state)
            elif event.type == pygame.KEYUP and event.key in (
                pygame.K_LEFT,
                pygame.K_RIGHT,
            ):
                game.set_player_direction(self.state, 0.0)

    # --- update ---
    def update(self):
        game.update_player(self.state)
        game.update_aliens(self.state)
        game.update_laser(self.state)
        self._play_explosion_on_hit()

    def _play_explosion_on_hit(self):
        # A hit is detected by score change; keep sound decoupled from logic.
        if getattr(self, "_last_score", -1) != self.state.score:
            if self.explosion_sound:
                self.explosion_sound.play()
            self._last_score = self.state.score

    # --- render ---
    def render(self):
        self.screen.blit(self.background, (0, 0))
        self.screen.blit(self.player_image,
                         (self.state.player_x, self.state.player_y))
        for alien in self.state.aliens:
            self.screen.blit(self.alien_image, (alien.x, alien.y))
        self.screen.blit(self.laser_image,
                         (self.state.laser.x + 9, self.state.laser.y + 6))
        s_disp = self.font.render("Score : " + str(self.state.score),
                                  True, (255, 0, 0))
        self.screen.blit(s_disp, (game.SCREEN_W - s_disp.get_width() - 6, 6))
        if self.state.game_over:
            over = self.over_font.render("GAME OVER", True, (255, 0, 0))
            self.screen.blit(over, (250, 250))
        pygame.display.update()

    def run(self):
        while self.running:
            self.handle_events()
            self.update()
            self.render()
            if self.state.game_over:
                break


def game_font():
    return pygame.font.Font(asset_path("Monoton-Regular.ttf"), 32)


def main():
    Game().run()


if __name__ == "__main__":
    main()
