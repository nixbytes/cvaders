# Building PyVaders in C: a Beginner's Walkthrough

A hands-on guide to how this repository was converted from a one-file
Python `pygame` game into a **memory-safe**, **unit-tested** C program.
It assumes you have *never* written C before. By the end you should be able
to read every file in the `c/` folder and explain *why* it is written the
way it is.

> **The central idea.** The original `src/main.py` is a 200-line script
> that *runs the game the moment you import it* — it opens a window and
> plays sound at module load time, so you cannot test any piece of it in
> isolation. Our job was to pull the **rules of the game** out of the
> **window-drawing code**, test the rules with zero SDL, and then hang a
> thin rendering layer back on top.

---

## 0. The starting point

The original `src/main.py` did everything at the top level:

```python
pygame.init()
screen = pygame.display.set_mode((800, 600))
background = pygame.image.load("assests/...")   # crashes if file missing
player_x = 370
...
while running:
    ...
    for i in range(num_of_enemies):
        if isCollision(alien_x[i], alien_y[i], laser_x, laser_y):
            explosion = mixer.Sound("assests/Explosion.wav")  # created EVERY hit
            ...
```

We noticed **five problems** that would bite us in C:

| # | Problem (Python) | Why it matters in C |
|---|------------------|---------------------|
| 1 | Four *parallel* lists: `alien_x[i]`, `alien_y[i]`, `alien_x_change[i]`, `alien_y_change[i]`. They must agree on the same `i`. | C has zero array bounds checking. A single off-by-one is a **buffer overflow**. One struct per enemy removes the whole class of bug. |
| 2 | `mixer.Sound("...")` is created **on every SPACE press and every explosion**. | In C that is a heap allocation you must `free`. Forget the free → a **memory leak every frame**. |
| 3 | `pygame.image.load("Explosion.wav")` but the file on disk is `explosion.wav`. | In Python the missing file is a runtime error you can survive; in C an unchecked `SD` load returns `NULL` and the program **crashes**. |
| 4 | `if laser_state is "fire":` uses *identity*, which only works because CPython interns the literal. | C has no interning. We use an explicit integer flag. |
| 5 | `game_over()` only redraws a banner but never stops the loop. | We turn it into a `bool` the loop can check. |

**Strategy:**

1. Pull the rules into a pure module → `src/game.py` (Python) and
   `c/src/game.c` (C). No SDL, no audio.
2. Write unit tests for those rules → `tests/test_game.py` and
   `c/test/test_game.c`.
3. Re-attach rendering as a thin frontend → `c/src/render.c` +
   `c/src/main.c`.

---

## 1. Anatomy of a C project

```
c/
├── include/
│   └── pyvaders.h      # the "menu": types + function prototypes
├── src/
│   ├── game.c          # the "kitchen": the logic, no SDL
│   ├── render.h        # frontend interface
│   ├── render.c        # the "restaurant": window + sound + draw
│   └── main.c          # "open the door": the entry point
├── test/
│   └── test_game.c     # the "inspector": checks the kitchen
├── third_party/
│   └── unity/
│       ├── unity.h     # a tiny test framework
│       └── unity.c     # its backing store
└── Makefile            # the "recipe": how to compile & test
```

### 1.1 The three-part "contract": `.h`, `.c`, and the build

In C a **header (`.h`)** is a promise of *what* exists — types and function
**names** with no bodies. The **source (`.c`)** is the *implementation*.
The **compiler** is told "where to find headers" with `-Iinclude` and "where
to find the code" with the list of `.c` files. The **linker** joins the
compiled pieces into one binary.

`pyvaders.h` says:

```c
bool is_collision(double ax, double ay, double lx, double ly);
```

`game.c` *implements* it. Any file that `#include "pyvaders.h"` may *call*
it. This separation is what lets the tests include the header and link only
`game.c` — the tests never touch SDL and never need a display.

### 1.2 The include guard

```c
#ifndef PYVADERS_H
#define PYVADERS_H
...
#endif
```

If two files both `#include "pyvaders.h"` and one of them also includes the
other, the text could be pasted in twice and the compiler would choke on
"redefinition". The guard is a one-time latch: the second time it is seen,
`PYVADERS_H` is already defined, so the whole body is skipped.

---

## 2. Data types: the struct

The single most important change is bundling the world into one struct:

```c
typedef struct {
    double x;
    double y;
    double x_change;
    double y_change;
} Alien;

typedef struct {
    double  player_x, player_y, player_x_change;
    Alien   aliens[NUM_ENEMIES];   // fixed array, 6 enemies
    Laser   laser;
    int     score;
    bool    game_over;
    uint32_t rng_state;            // seedable RNG so runs are reproducible
} GameState;
```

- **`struct`** glues related fields together. Instead of four lists that
  can drift out of sync, `aliens[i].x` and `aliens[i].y` are *physically
  next to each other*, so they can never disagree on which `i` they belong
  to.
- **`double`** is C's "float" (a number with a decimal point).
- **`bool` / `true` / `false`** come from `<stdbool.h>` and read like real
  booleans.
- **`uint32_t`** is a fixed-width 32-bit unsigned integer — used for the
  random-number generator so it behaves identically on every platform.

---

## 3. The most important C lesson: manage every byte of memory

Python frees memory for you. **C does not.** Every `malloc`/`calloc` must
have a matching `free`. A *memory leak* is memory you gave out and never
gave back — the program keeps working but slowly bleeds memory until it
dies.

Our design makes leaks impossible **by construction**:

- The core (`game.c`) makes **exactly one** allocation, for the whole
  `GameState`, and frees it once:

```c
int game_init(uint32_t seed, GameState **out_state)
{
    GameState *g = calloc(1, sizeof(GameState));  // THE one allocation
    if (g == NULL) return -1;                     // check it succeeded!
    ...
    *out_state = g;                               // hand the pointer back
    return 0;
}

void game_free(GameState *state) { free(state); } // and we free it
```

  Because there is one `calloc` and one `free`, there is nothing to forget.

- The frontend (`render.c`) loads each texture/sound **once** at startup and
  frees each one in `renderer_destroy()`:

```c
void renderer_destroy(Renderer *r)
{
    if (r == NULL) return;
    if (r->music)           Mix_FreeMusic(r->music);
    if (r->background)      SDL_DestroyTexture(r->background);
    ...
    free(r);
}
```

### 3.1 The three habits that make C safe

**1. Always check the result of a load / allocation.**

In `render.c` every `SDL_LoadBMP` / `Mix_LoadWAV` / `TTF_OpenFont` result
is checked for `NULL`. A missing asset file or out-of-memory condition now
prints a message instead of crashing:

```c
SDL_Surface *surf = SDL_LoadBMP(path);
if (surf == NULL) {
    fprintf(stderr, "image %s: %s\n", image_name, SDL_GetError());
    return NULL;
}
```

**2. NULL-safety at the boundary.**

Every public function starts with:

```c
if (state == NULL || out_x == NULL || out_y == NULL) return;
```

A `NULL` pointer is an address pointing at nothing; *using* it is undefined
behaviour (in practice, a crash). Checking first is cheap insurance.
`free(NULL)` is a legal no-op, so `renderer_destroy(NULL)` is safe too.

**3. Pair every create with a destroy, in reverse order.**

`renderer_create` builds `window → renderer → textures → sounds → font →
game`; `renderer_destroy` frees them the other way round. A child resource
(like a `SDL_Texture`) depends on its parent (the `SDL_Renderer`), so we
must not free the parent before the child.

---

## 4. The classic buffer-overflow guard

Building a file path by hand is a textbook crash. We use `snprintf`, which
writes at most `size - 1` characters plus a `\0`:

```c
char path[512];
int n = snprintf(path, sizeof(path), "assests/%s", image_name);
if (n < 0 || n >= (int)sizeof(path)) {
    return NULL;                 // the name was too long -- fail safe
}
```

The `n >= sizeof(path)` test catches the case where `snprintf` *truncated*
the name — it tells us the buffer was too small, so we abort instead of
loading the wrong file. This is the kind of check that turns a security
hole into a clean error message.

---

## 5. Doing math safely (and faster)

The collision test in Python was:

```python
distance = math.sqrt((ax-lx)**2 + (ay-ly)**2)
return distance < 27
```

In C we skip the `sqrt`:

```c
bool is_collision(double ax, double ay, double lx, double ly)
{
    double dx = ax - lx, dy = ay - ly;
    double r2 = COLLISION_RADIUS * COLLISION_RADIUS;  // 27*27 = 729
    return (dx*dx + dy*dy) < r2;
}
```

`dx² + dy² < 27²` is **algebraically identical** to `√(dx² + dy²) < 27`
(both sides are non-negative, so squaring preserves the inequality). We
avoid `sqrt` because it is faster and because it removes a rounding error
right *at* the boundary (a point at exactly distance 27 could round to 26.999
or 27.001, flipping the answer).

---

## 6. A predictable "random" number generator

`random.Random(seed)` in Python is not bit-for-bit reproducible in C, so we
do not try to match Python's sequence. Instead we ship a small,
self-contained, **deterministic** generator (an *xorshift32*):

```c
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
```

For a given seed it always produces the same sequence — which is exactly
what a *test* needs. The `^=` and `<<`/`>>` lines just shuffle the bits;
the stored `*state = x` lets the next call continue from where we left off.

This is also why a couple of the tests loop many times with a fixed seed
rather than expecting a specific value: the test checks **invariants**
(e.g. "every coordinate is inside the allowed range") instead of brittle
exact numbers.

---

## 7. How the tests are written

`c/test/test_game.c` is a program that checks the core. Each test is a
function named `test_...`:

```c
void test_player_clamps_to_zero(void)
{
    BEGIN_TEST();
    GameState *g = NULL;
    TEST_ASSERT_EQUAL_INT(0, game_init(1, &g));

    g->player_x = 0.0;
    set_player_direction(g, -0.5);   // try to move left
    update_player(g);                // the clamp should hold it at 0

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, g->player_x);
    game_free(g);
}
```

Read it as English: *init a game; try to move left off the screen; the
player's x must still be 0.*

The assertions come from our tiny `unity.h`. The key beginner lesson lives
in `TEST_ASSERT_EQUAL_DOUBLE`: **never compare two floats for exact
equality.** Binary floating point has rounding error, so two numbers that
"look" equal can differ in the last bit. Instead we ask "are they within a
tiny tolerance?".

```c
#define TEST_ASSERT_DOUBLE_WITHIN(tolerance, expected, actual) do { \
    double _e = expected, _a = actual; \
    double _d = _e - _a; if (_d < 0) _d = -_d; \
    if (_d > tolerance) { /* record failure, return */ } \
} while (0)
```

The `do { ... } while (0)` wrapper is a C idiom that glues the whole block
together so the macro behaves like **one statement** even when used inside
an `if`/`else`.

---

## 8. Building and running

From the `c/` directory:

```sh
make test       # build + run the logic tests (no SDL needed)
make test-asan  # same, under AddressSanitizer + UndefinedBehaviourSanitizer
make valgrind   # same, under valgrind (reports leaks)
make all        # build the actual game (needs SDL2, SDL_ttf, SDL_mixer)
./build/pyvaders
```

### 8.1 What the sanitizer flags do

The C standard gives the compiler warning switches (`-Wall -Wextra
-Werror -pedantic`): `-Wall` turns on the usual checks, `-Wextra` a few
more, `-Werror` makes *every* warning a build failure (so you can never
silently ship a warning), and `-pedantic` enforces the standard strictly.

The *sanitizers* are runtime checks the compiler weaves into the binary:

- **AddressSanitizer (`-fsanitize=address`)** catches use-after-free,
  buffer overflows and leaks — the big three of C bugs.
- **UndefinedBehaviourSanitizer (`-fsanitize=undefined`)** catches things
  C doesn't define a result for (signed overflow, misaligned access, …).
- **valgrind** is a separate tool that inspects every memory byte the
  program touches.

We always run `make test` clean, then `make test-asan`, and treat a clean
run of all three as "the memory is safe".

### 8.2 Why the tests pass here but the game doesn't build

The **logic core** (`game.c`) has no dependency on SDL, so the tests build
with *just a C compiler*. The **front end** (`render.c`, `main.c`) needs
SDL2/SDL_ttf/SDL_mixer. That split is deliberate: the part we can *prove*
correct is the part that is easy to test, and it is entirely independent of
the operating system.

---

## 9. Putting it all together: one frame

The whole game is the loop in `main.c`:

```c
Renderer *r = renderer_create();      // window + assets + game state
if (r == NULL) return 1;             // fatal: report and exit

while (renderer_step(r) != 0) { }    // input → update → draw, repeat

renderer_destroy(r);                 // free EVERYTHING
return 0;                            // success
```

`renderer_step` does, in order:

```c
handle_events(r, &running);          // 1. keyboard / quit
update_player(&r->state);            // 2. move + clamp the ship
update_aliens(&r->state);           // 3. move + bounce + collisions
update_laser(&r->state);            // 4. move the laser
renderer_present(r);                // 5. draw the frame
if (r->state.game_over) return 0;   // 6. stop if the match ended
```

Each of steps 2–4 is a pure function from `game.c` that the unit tests
already cover. Step 5 is the only place SDL touches a pixel. Because the
rules and the pixels live in different files, we can change the game's
behaviour without touching the rendering, and we can test the behaviour
without a screen.

---

## 10. Glossary (for first-timers)

| Term | Plain meaning |
|------|---------------|
| **Header / source** | `.h` is the contract (names), `.c` is the implementation. |
| **Struct** | A labelled bag of related fields (e.g. an `Alien`). |
| **Pointer** | A variable holding a memory address; `*p` reads/changes what it points to. |
| **Heap / `calloc` / `free`** | Memory you borrow from the OS and must return. |
| **Leak** | Borrowed memory you forgot to return. |
| **Buffer overflow** | Writing past the end of a fixed-size array — a crash / security bug. |
| **Undefined behaviour** | C says "the standard does not promise any result". |
| **Sanitizer** | A compiler add-on that turns these bugs into loud crashes during testing. |
| **Opaque handle** | A pointer to a struct whose fields you cannot see outside its own file — a clean boundary. |
| **`goto` cleanup** | The C idiom of unwinding several resources on error, in reverse order. |

---

## 11. What a beginner should try next

1. Open `c/include/pyvaders.h` and read it top-to-bottom; every constant
   has a one-line comment.
2. Change `NUM_ENEMIES` from 6 to 3, rebuild with `make test`, and watch
   the `test_init_creates_six_enemies` test *fail* — then fix the test.
   This shows you how the tests *pin down* behaviour.
3. Run `make test-asan`. It already runs clean; try removing a `free` from
   `renderer_destroy` and rebuild the game to see what the sanitizer
   reports.
4. Read `c/src/render.c`'s `renderer_create` cleanup labels and trace which
   label frees which resource.

That loop — **change something, the test fails, fix, repeat** — is how
software stays correct over time.

---

*Files in this project and their jobs: `src/game.py` and `c/src/game.c`
are the same logic in two languages; `tests/test_game.py` and
`c/test/test_game.c` are the same checks in two languages; `c/src/render.c`
and `c/src/main.c` are the SDL front end that only exists in C.*
