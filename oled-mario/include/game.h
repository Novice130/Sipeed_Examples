// ==============================================================================
//  game.h - side-scrolling plumber platformer (fan tribute), 240x160 framebuffer
//
//  Pure C++ with no Arduino dependencies: the firmware (src/main.cpp) and the
//  desktop simulator (tools/sim.cpp) both run exactly this code.
//
//  The plumber always walks right on his own. Three on-screen buttons:
//    JUMP  hold longer = jump higher       FIRE  throw a fireball (Fire form: two at once)
//    RUN   toggle walk / run
//  DEMO: a built-in player drives the same buttons (silent). Any button press hands
//  control to you on the spot; 5 minutes without a press gives it back to the demo.
// ==============================================================================
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"

#define TICK_HZ 30
#define IDLE_TO_DEMO_TICKS (300 * TICK_HZ)  // 5 minutes without a button -> demo

struct Input {
    bool jumpHeld;     // finger is on JUMP (variable jump height)
    bool jumpPressed;  // JUMP went down since the last tick (latched, so short taps count)
    bool firePressed;
    bool runPressed;   // toggles walk / run
    bool autoFire;     // touch panel is faulty: throw fireballs at enemies by itself
};

enum SfxId : uint8_t {
    SFX_JUMP, SFX_JUMP_BIG, SFX_COIN, SFX_STOMP, SFX_BUMP, SFX_BREAK, SFX_SPROUT, SFX_POWERUP,
    SFX_POWERDOWN, SFX_FIRE, SFX_KICK, SFX_ONEUP, SFX_DEATH, SFX_FLAG, SFX_CLEAR, SFX_GAMEOVER,
    SFX_MODE, SFX_TALLY, SFX_RUN
};

// platform hooks (firmware / simulator)
void plat_sfx(uint8_t id, uint16_t param);
void plat_led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);

// ------------------------------------------------------------------ tuning
// positions and speeds are fixed point: 16 units = 1 pixel
#define FP          16
#define WALK_SPEED  32    // 2.0 px/tick
#define RUN_SPEED   48    // 3.0 px/tick
#define JUMP_VEL    112   // 7.0 px/tick launch
#define JUMP_RUN    8     // extra launch when running
#define GRAV_HOLD   5     // gravity while JUMP is held (rising)
#define GRAV        16    // normal gravity
#define MAX_FALL    112
#define HOLD_TICKS  12    // longest a held JUMP keeps the low gravity
#define STOMP_VEL   80
#define ITEM_SPEED  56    // power-ups slide after the player at 3.5 px/tick
#define FIRE_LIFE   30    // a fireball fizzles after this many ticks (~150 px)
#define GROUND_Y    128   // top of the ground rows in pixels
#define COYOTE      4     // a jump still works this many ticks after walking off an edge
#define RUNWAY      4     // flat tiles to walk on after respawning before the danger

#define MAX_W       208   // widest level
#define MAX_ENTS    32

// ------------------------------------------------------------------ tiles
enum Tile : uint8_t {
    T_EMPTY, T_GROUND, T_BRICK, T_BRICK_COINS, T_BRICK_STAR, T_BRICK_1UP, T_QCOIN, T_QPOWER, T_USED,
    T_HARD, T_PIPE_TL, T_PIPE_TR, T_PIPE_L, T_PIPE_R, T_TREE,  // ^ solid
    T_TRUNK, T_LAVA, T_COIN
};

static uint8_t tmap[LEVEL_ROWS][MAX_W];
static int lvW = 0;

static inline bool tileSolid(uint8_t t) { return t >= T_GROUND && t <= T_TREE; }

static inline uint8_t tileAt(int col, int row) {
    if (row < 0 || row >= LEVEL_ROWS) return T_EMPTY;
    if (col < 0) return T_HARD;  // invisible wall behind the start
    if (col >= lvW) return row >= 8 ? T_GROUND : T_EMPTY;
    return tmap[row][col];
}

static inline bool solidPx(int px, int py) { return tileSolid(tileAt(px >> 4, py >> 4)); }

static uint8_t tileFromChar(char c) {
    switch (c) {
        case '#': return T_GROUND;
        case 'B': return T_BRICK;
        case 'b': return T_BRICK_COINS;
        case 's': return T_BRICK_STAR;
        case 'u': return T_BRICK_1UP;
        case '?': return T_QCOIN;
        case 'M': return T_QPOWER;
        case 'X': return T_HARD;
        case '[': return T_PIPE_TL;
        case ']': return T_PIPE_TR;
        case '{': return T_PIPE_L;
        case '}': return T_PIPE_R;
        case 'T': return T_TREE;
        case '|': return T_TRUNK;
        case '~': return T_LAVA;
        case 'o': return T_COIN;
        default: return T_EMPTY;
    }
}

// ------------------------------------------------------------------ bodies
struct Body {
    int32_t x, y;    // top-left of the hitbox, fixed point
    int16_t vx, vy;
    uint8_t w, h;    // hitbox in pixels
    bool onGround;
};

static inline int bx(const Body& b) { return b.x >> 4; }
static inline int by(const Body& b) { return b.y >> 4; }

static bool overlap(const Body& a, const Body& b) {
    int ax = bx(a), ay = by(a), cx = bx(b), cy = by(b);
    return ax < cx + b.w && cx < ax + a.w && ay < cy + b.h && cy < ay + a.h;
}

static bool colBlocked(int px, int top, int bot) {
    for (int r = top >> 4; r <= bot >> 4; r++)
        if (tileSolid(tileAt(px >> 4, r))) return true;
    return false;
}

static bool rowBlocked(int py, int left, int right) {
    for (int c = left >> 4; c <= right >> 4; c++)
        if (tileSolid(tileAt(c, py >> 4))) return true;
    return false;
}

enum { MV_WALL = 1, MV_LAND = 2, MV_HEAD = 4 };

// moves one axis at a time against the tile map; reports what was hit
static int moveBody(Body& b, int* headCol = nullptr, int* headRow = nullptr) {
    int flags = 0;
    if (b.vx) {
        b.x += b.vx;
        int px = bx(b), py = by(b);
        if (b.vx > 0) {
            int ex = px + b.w - 1;
            if (colBlocked(ex, py, py + b.h - 1)) {
                b.x = ((ex >> 4) * 16 - b.w) * FP;
                flags |= MV_WALL;
            }
        } else if (colBlocked(px, py, py + b.h - 1)) {
            b.x = ((px >> 4) + 1) * 16 * FP;
            flags |= MV_WALL;
        }
    }
    b.y += b.vy;
    int px = bx(b), py = by(b);
    if (b.vy > 0) {
        int bot = py + b.h - 1;
        if (rowBlocked(bot, px, px + b.w - 1)) {
            b.y = ((bot >> 4) * 16 - b.h) * FP;
            b.vy = 0;
            flags |= MV_LAND;
        }
    } else if (b.vy < 0 && py >= 0 && rowBlocked(py, px, px + b.w - 1)) {
        int row = py >> 4;
        int mid = (px + b.w / 2) >> 4;
        int col = tileSolid(tileAt(mid, row)) ? mid : (tileSolid(tileAt(px >> 4, row)) ? px >> 4 : (px + b.w - 1) >> 4);
        if (headCol) *headCol = col;
        if (headRow) *headRow = row;
        b.y = (row + 1) * 16 * FP;
        b.vy = 0;
        flags |= MV_HEAD;
    }
    px = bx(b);
    py = by(b);
    b.onGround = b.vy >= 0 && rowBlocked(py + b.h, px, px + b.w - 1);
    return flags;
}

// ------------------------------------------------------------------ entities
enum EKind : uint8_t {
    K_NONE, K_GOOMBA, K_KOOPA, K_SHELL, K_PIRANHA,               // enemies
    K_MUSHROOM, K_ONEUP, K_FLOWER, K_STAR,                       // items
    K_FIREBALL, K_FLAT, K_KNOCKED, K_COINPOP, K_DEBRIS, K_SCORE, K_PUFF  // effects
};

struct Ent {
    uint8_t kind;
    uint8_t sub;   // item: 1 = still sprouting from its block; piranha: phase
    int16_t t;     // general timer
    int16_t aux;   // score value / piranha base y / knocked sprite / shell grace
    Body b;
};

static Ent ents[MAX_ENTS];

static Ent* spawn(uint8_t kind, int px, int py, int w, int h) {
    for (auto& e : ents) {
        if (e.kind != K_NONE) continue;
        memset(&e, 0, sizeof(e));
        e.kind = kind;
        e.b.x = px * FP;
        e.b.y = py * FP;
        e.b.w = w;
        e.b.h = h;
        return &e;
    }
    return nullptr;
}

static inline bool isEnemy(uint8_t k) { return k >= K_GOOMBA && k <= K_PIRANHA; }

// ------------------------------------------------------------------ player
enum MState : uint8_t { M_PLAY, M_GROW, M_SHRINK, M_DYING, M_FLAG, M_WALKOFF, M_INCASTLE };
enum Form : uint8_t { F_SMALL, F_BIG, F_FIRE };

struct Mario {
    Body b;
    uint8_t form, fromForm, shownForm, state;
    bool run, visible;
    int16_t speed, st, jumpHold, jumpBuf, coyote, invuln, star, fireCd, chain;
    int32_t anim;
};

static Mario m;

// ------------------------------------------------------------------ game state
enum Scene : uint8_t { SC_INTRO, SC_PLAY, SC_GAMEOVER };

static uint32_t tick = 0;
static uint32_t rng = 1;
static uint8_t scene = SC_INTRO;
static int sceneT = 0;
static int li = 0;  // level index into LEVELS (4 per world)
static int lives = 3, coins = 0;
static uint32_t score = 0;
static int timeLeft = 300, timeSub = 0;
static int camX = 0;
static int nextSpawn = 0;
static int lastGroundCol = 0;  // column the plumber last stood on
static int respawnFrom = -1;   // after a death: restart a few tiles before this column
static int flagY = 0, tallyWait = 0;
static const LevelDef* LV = &LEVELS[0];

static bool playMode = false;
static uint32_t lastInputTick = 0;
static int bannerT = 0;

// visible button state (also shows the demo player's presses)
static int btnJumpT = 0, btnFireT = 0, btnRunT = 0;
static bool autoFireOn = false;  // FIRE ring stays gold while it shoots by itself

struct Bump { int16_t col, row, t; };
static Bump bumps[4];
struct MultiCoin { int16_t col, row, n; };
static MultiCoin multi[4];

static uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int rndInt(int n) { return (int)(rnd() % (uint32_t)n); }

static void sfx(uint8_t id, uint16_t p = 0) {
    if (playMode) plat_sfx(id, p);  // DEMO is silent - it's a display
}

// floating points label; value -1 = "1UP"
static void popup(int value, int px, int py) {
    Ent* e = spawn(K_SCORE, px, py, 1, 1);
    if (e) { e->aux = value; e->t = 24; }
}

static void addScore(int pts, int px, int py) {
    score += pts;
    popup(pts, px, py);
}

static void oneUp(int px, int py) {
    lives++;
    sfx(SFX_ONEUP);
    plat_led(0, 255, 0, 300);
    popup(-1, px, py);
}

static void addCoin() {
    coins++;
    score += 200;
    sfx(SFX_COIN);
    plat_led(255, 200, 0, 90);
    if (coins >= 100) {
        coins -= 100;
        oneUp(bx(m.b), by(m.b) - 8);
    }
}

// ------------------------------------------------------------------ level setup
static int floorRowAt(int col) {
    for (int r = 2; r < LEVEL_ROWS; r++)
        if (tileSolid(tileAt(col, r))) return r;
    return 8;
}

static void setMarioForm(uint8_t f) {
    int bottom = by(m.b) + m.b.h;
    m.form = m.shownForm = f;
    m.b.h = f == F_SMALL ? 15 : 30;
    m.b.y = (bottom - m.b.h) * FP;
}

// top row of the lowest walkable surface in a column (-1: hole / lava)
static int surfaceRow(int col) {
    for (int r = LEVEL_ROWS - 1; r >= 5; r--)
        if (tileSolid(tileAt(col, r)) && !tileSolid(tileAt(col, r - 1)) && !tileSolid(tileAt(col, r - 2)) &&
            tileAt(col, r - 1) != T_LAVA)
            return r;
    return -1;
}

// a start column a few tiles before `col` with a flat RUNWAY in front of it, so the
// plumber gets a run-up to whatever got him
static int respawnCol(int col) {
    for (int c = col - RUNWAY; c > 2; c--) {
        int r = surfaceRow(c);
        bool ok = r >= 0;
        for (int i = 1; i < RUNWAY && ok; i++) ok = surfaceRow(c + i) == r;
        if (ok) return c;
    }
    return 2;
}

static void loadLevel() {
    LV = &LEVELS[li];
    lvW = LV->w;
    for (int r = 0; r < LEVEL_ROWS; r++)
        for (int c = 0; c < lvW; c++) tmap[r][c] = tileFromChar(LV->rows[r][c]);
    memset(ents, 0, sizeof(ents));
    memset(bumps, 0, sizeof(bumps));
    memset(multi, 0, sizeof(multi));

    int startCol = respawnFrom >= 0 ? respawnCol(respawnFrom) : 2;
    respawnFrom = -1;
    lastGroundCol = startCol;
    uint8_t form = m.form;
    memset(&m, 0, sizeof(m));
    m.b.w = 12;
    m.b.h = 15;
    m.visible = true;
    m.b.x = (startCol * 16 + 2) * FP;
    int floorRow = surfaceRow(startCol);
    m.b.y = ((floorRow >= 0 ? floorRow : floorRowAt(startCol)) * 16 - 15) * FP;
    setMarioForm(form);
    m.speed = WALK_SPEED;

    camX = startCol * 16 - 64;
    if (camX < 0) camX = 0;
    nextSpawn = 0;
    while (nextSpawn < LV->nents && LV->ents[nextSpawn].col < startCol + 8) nextSpawn++;  // breathing room
    timeLeft = LV->time;
    timeSub = 0;
    flagY = 24;
    tallyWait = 0;
}

static void startIntro() {
    scene = SC_INTRO;
    sceneT = respawnFrom >= 0 ? 45 : 70;  // quicker retry after a death
}

// after a game over you continue from the first level of the world you were in
static void newGame(int level = 0) {
    lives = 3;
    score = 0;
    coins = 0;
    li = level - level % 4;
    respawnFrom = -1;
    m.form = F_SMALL;
    startIntro();
}

static void spawnLevelEnemies() {
    while (nextSpawn < LV->nents && LV->ents[nextSpawn].col * 16 < camX + FB_W + 8) {
        const LevelEnt& le = LV->ents[nextSpawn++];
        int x = le.col * 16, floorY = (le.row + 1) * 16;
        Ent* e = nullptr;
        switch (le.kind) {
            case 'g':
                e = spawn(K_GOOMBA, x + 1, floorY - 15, 14, 15);
                if (e) e->b.vx = -12;
                break;
            case 'k':
                e = spawn(K_KOOPA, x + 1, floorY - 22, 14, 22);
                if (e) e->b.vx = -12;
                break;
            case 'p':
                // sits inside the pipe whose top-left tile is (col, row)
                e = spawn(K_PIRANHA, x + 11, le.row * 16, 10, 0);
                if (e) { e->aux = le.row * 16; e->sub = 0; e->t = 30; }
                break;
        }
    }
}

// ------------------------------------------------------------------ player events
static void setFormAnimated(uint8_t f, uint8_t state) {
    m.fromForm = m.form;
    setMarioForm(f);
    m.shownForm = m.fromForm;
    m.state = state;
    m.st = 24;
}

static void killMario() {
    respawnFrom = lastGroundCol;
    m.state = M_DYING;
    m.st = 0;
    m.b.vx = m.b.vy = 0;
    m.star = 0;
    sfx(SFX_DEATH);
    plat_led(255, 0, 0, 600);
}

static void hurtMario() {
    if (m.invuln > 0 || m.star > 0 || m.state != M_PLAY) return;
    if (m.form != F_SMALL) {
        setFormAnimated(F_SMALL, M_SHRINK);
        m.invuln = 90;
        sfx(SFX_POWERDOWN);
        plat_led(255, 0, 0, 200);
        return;
    }
    killMario();
}

static void knockOut(Ent& e, int pts) {
    int px = bx(e.b), py = by(e.b);
    addScore(pts, px, py - 8);
    if (e.kind == K_PIRANHA) {
        e.kind = K_PUFF;
        e.t = 10;
        return;
    }
    e.aux = (e.kind == K_GOOMBA) ? 0 : (e.kind == K_KOOPA ? 1 : 2);
    e.kind = K_KNOCKED;
    e.b.vx = e.b.vx > 0 ? 16 : -16;
    e.b.vy = -80;
}

static void collectItem(Ent& e) {
    int px = bx(e.b), py = by(e.b);
    switch (e.kind) {
        case K_MUSHROOM:
            addScore(1000, px, py);
            if (m.form == F_SMALL) setFormAnimated(F_BIG, M_GROW);
            sfx(SFX_POWERUP);
            plat_led(0, 255, 0, 300);
            break;
        case K_FLOWER:
            addScore(1000, px, py);
            if (m.form != F_FIRE) setFormAnimated(F_FIRE, M_GROW);
            sfx(SFX_POWERUP);
            plat_led(255, 80, 0, 300);
            break;
        case K_STAR:
            addScore(1000, px, py);
            m.star = 300;
            sfx(SFX_POWERUP);
            break;
        case K_ONEUP:
            oneUp(px, py);
            break;
    }
    e.kind = K_NONE;
}

static void sprout(uint8_t kind, int col, int row) {
    Ent* e = spawn(kind, col * 16, row * 16, 16, 16);
    if (!e) return;
    e->sub = 1;
    e->t = 16;
    sfx(SFX_SPROUT);
}

static void bumpTile(int col, int row) {
    for (auto& b : bumps)
        if (b.t <= 0) { b.col = col; b.row = row; b.t = 8; break; }
    // enemies standing on the bumped tile get knocked out, items hop
    for (auto& e : ents) {
        if (e.kind == K_NONE) continue;
        int ex = bx(e.b), ebot = by(e.b) + e.b.h;
        if (ebot != row * 16 || ex + e.b.w <= col * 16 || ex >= col * 16 + 16) continue;
        if (e.kind == K_GOOMBA || e.kind == K_KOOPA || e.kind == K_SHELL) knockOut(e, 100);
        else if (e.kind == K_MUSHROOM || e.kind == K_ONEUP || e.kind == K_STAR) e.b.vy = -64;
    }
    // a coin sitting on top gets collected
    if (tileAt(col, row - 1) == T_COIN) {
        tmap[row - 1][col] = T_EMPTY;
        addCoin();
        Ent* c = spawn(K_COINPOP, col * 16 + 4, (row - 1) * 16, 8, 14);
        if (c) { c->b.vy = -96; c->t = 18; }
    }
}

static void coinPop(int col, int row) {
    addCoin();
    Ent* c = spawn(K_COINPOP, col * 16 + 4, row * 16 - 14, 8, 14);
    if (c) { c->b.vy = -96; c->t = 18; }
}

static void hitBlock(int col, int row) {
    uint8_t& t = tmap[row][col];
    switch (t) {
        case T_QCOIN:
            t = T_USED;
            coinPop(col, row);
            bumpTile(col, row);
            break;
        case T_QPOWER:
            t = T_USED;
            sprout(m.form == F_SMALL ? K_MUSHROOM : K_FLOWER, col, row);
            bumpTile(col, row);
            break;
        case T_BRICK_STAR:
            t = T_USED;
            sprout(K_STAR, col, row);
            bumpTile(col, row);
            break;
        case T_BRICK_1UP:
            t = T_USED;
            sprout(K_ONEUP, col, row);
            bumpTile(col, row);
            break;
        case T_BRICK_COINS: {
            MultiCoin* mc = nullptr;
            for (auto& q : multi)
                if (q.n && q.col == col && q.row == row) mc = &q;
            if (!mc)
                for (auto& q : multi)
                    if (!q.n) { mc = &q; q.col = col; q.row = row; q.n = 6; break; }
            coinPop(col, row);
            if (!mc || --mc->n <= 0) { t = T_USED; if (mc) mc->n = 0; }
            bumpTile(col, row);
            break;
        }
        case T_BRICK:
            if (m.form != F_SMALL) {
                t = T_EMPTY;
                bumpTile(col, row);
                for (int i = 0; i < 4; i++) {
                    Ent* d = spawn(K_DEBRIS, col * 16 + (i & 1) * 8 + 2, row * 16 + (i >> 1) * 8, 6, 5);
                    if (d) { d->b.vx = (i & 1) ? 24 : -24; d->b.vy = (i >> 1) ? -64 : -96; d->t = 40; }
                }
                score += 50;
                sfx(SFX_BREAK);
                plat_led(255, 120, 40, 80);
            } else {
                bumpTile(col, row);
                sfx(SFX_BUMP);
            }
            break;
        default:
            sfx(SFX_BUMP);
            break;
    }
}

// ------------------------------------------------------------------ demo player
// The demo player presses the same three buttons a person would and has no special
// powers. Each tick on the ground it plays a few candidate jumps forward through the
// real physics (tile collision, enemies moving in straight lines) and takes the
// latest jump that still comes out alive - so it jumps at the edge of holes, hops
// onto enemies and climbs pipes and stairs.
#define AI_HORIZON 40

struct Ai { int hold, cooldown, fireCd, runCd; };
static Ai ai;

static bool hasFloor(int col, int feetRow) {
    for (int r = feetRow; r < LEVEL_ROWS; r++)
        if (tileSolid(tileAt(col, r))) return true;
    return false;
}

struct Plan { int survive; int progress; int wallT; };  // wallT: first tick pressed against a wall

struct SimFoe { Body b; bool alive, still; };

// Plays the look-ahead with up to two jumps: the first `delay` ticks from now, the
// second `delay2` ticks after landing from the first (delay2 < 0: no second jump).
static Plan simulateJump(int delay, int hold, int speed, int delay2 = -1, int hold2 = 0) {
    SimFoe foes[12];
    int nf = 0;
    for (auto& e : ents) {
        if (!isEnemy(e.kind) || nf >= 12) continue;
        if (e.kind == K_SHELL && e.b.vx == 0) continue;  // resting shell gets kicked, harmless
        SimFoe& f = foes[nf];
        f.b = e.b;
        f.alive = true;
        f.still = e.kind == K_PIRANHA;
        if (f.still) {
            if (e.sub == 0) continue;  // hidden; it won't come out with us this close
            f.b.h = 22;
            f.b.y = (e.aux - 22) * FP;
        }
        nf++;
    }
    Body b = m.b;
    int jumpHold = 0, jumpAt = delay, holdEnd = delay + hold, jumps = 0;
    bool wasAir = false;
    Plan p = {AI_HORIZON + 1, 0, AI_HORIZON + 1};
    int startX = bx(b), startFeet = by(b) + b.h;
    for (int t = 0; t < AI_HORIZON; t++) {
        if (t == jumpAt && holdEnd > jumpAt && b.onGround) {
            b.vy = -(JUMP_VEL + (speed > WALK_SPEED ? JUMP_RUN : 0));
            jumpHold = HOLD_TICKS;
            b.onGround = false;
            jumps++;
        }
        bool held = t >= jumpAt && t < holdEnd;
        if (jumpHold > 0) {
            if (held && b.vy < 0) jumpHold--;
            else jumpHold = 0;
        }
        b.vx = speed;
        b.vy += jumpHold > 0 ? GRAV_HOLD : GRAV;
        if (b.vy > MAX_FALL) b.vy = MAX_FALL;
        if ((moveBody(b) & MV_WALL) && p.wallT > AI_HORIZON) p.wallT = t;
        if (!b.onGround) wasAir = true;
        else if (wasAir && jumps == 1 && delay2 >= 0) {
            // landed from the first jump: schedule the second one
            wasAir = false;
            jumpAt = t + 1 + delay2;
            holdEnd = jumpAt + hold2;
            delay2 = -1;
        }
        int px = bx(b), py = by(b);
        if (py > FB_H + 8 || tileAt((px + b.w / 2) >> 4, (py + b.h - 2) >> 4) == T_LAVA) { p.survive = t; break; }
        bool dead = false;
        for (int i = 0; i < nf; i++) {
            SimFoe& f = foes[i];
            if (!f.alive) continue;
            if (!f.still) {  // same walking physics as updateEnemiesAndItems()
                f.b.vy += GRAV;
                if (f.b.vy > MAX_FALL) f.b.vy = MAX_FALL;
                if (moveBody(f.b) & MV_WALL) f.b.vx = -f.b.vx;
            }
            if (overlap(b, f.b)) {
                if (m.star > 0) { f.alive = false; continue; }
                if (!f.still && b.vy > 0 && py + b.h - by(f.b) <= 10) {  // piranhas can't be stomped
                    f.alive = false;
                    b.y = (by(f.b) - b.h) * FP;
                    b.vy = -STOMP_VEL;
                    jumpHold = 0;
                } else {
                    dead = true;
                }
            }
        }
        if (dead) { p.survive = t; break; }
    }
    if (p.survive > AI_HORIZON && !b.onGround && by(b) + b.h > startFeet) {
        // still dropping at the end of the look-ahead, below where we started: only
        // fine if there is something to land on
        int feetRow = (by(b) + b.h) >> 4;
        if (!hasFloor(bx(b) >> 4, feetRow) && !hasFloor((bx(b) + b.w - 1) >> 4, feetRow)) p.survive = AI_HORIZON;
    }
    p.progress = bx(b) - startX;
    return p;
}

static bool planOk(const Plan& p, int speed) {
    // alive for the whole look-ahead and not pinned against a wall
    return p.survive > AI_HORIZON && p.progress >= speed * AI_HORIZON / FP / 2;
}

static bool planBetter(const Plan& a, const Plan& b) {
    return a.survive > b.survive || (a.survive == b.survive && a.progress > b.progress);
}

static const int AI_HOLDS[5] = {2, 4, 7, 10, HOLD_TICKS};

// best first-jump hold for a jump starting `delay` ticks from now; with `twoJumps`
// a follow-up jump after landing is allowed too
static int bestJump(int delay, int speed, Plan* out, bool twoJumps = false) {
    static const int D2[5] = {0, 4, 8, 14, 20};
    static const int H2[3] = {3, 7, HOLD_TICKS};
    int best = -1;
    Plan bp = {-1, 0, 0};
    for (int h : AI_HOLDS) {
        Plan p = simulateJump(delay, h, speed);
        if (planBetter(p, bp)) { bp = p; best = h; }
        if (!twoJumps || planOk(bp, speed)) continue;
        for (int d2 : D2)
            for (int h2 : H2) {
                Plan q = simulateJump(delay, h, speed, d2, h2);
                if (planBetter(q, bp)) { bp = q; best = h; }
            }
    }
    if (out) *out = bp;
    return best;
}

static bool blockAhead() {
    // a ? block (or special brick) a jump above the path in the next few tiles
    int mx = bx(m.b), feetRow = (by(m.b) + m.b.h) >> 4;
    int c = (mx + m.b.w / 2 + m.speed * 7 / FP) >> 4;
    for (int r = feetRow - 2; r >= feetRow - 5 && r >= 0; r--) {
        uint8_t t = tileAt(c, r);
        if (tileSolid(t))
            return t == T_QCOIN || t == T_QPOWER || t == T_BRICK_STAR || t == T_BRICK_1UP || t == T_BRICK_COINS;
    }
    return false;
}

// distance to the nearest enemy ahead at about our height (9999: none)
static int enemyAhead() {
    int front = bx(m.b) + m.b.w, my = by(m.b), enemyDx = 9999;
    for (auto& e : ents) {
        if (!isEnemy(e.kind) || (e.kind == K_PIRANHA && e.b.h < 6)) continue;
        int dx = bx(e.b) - front;
        if (dx < -8 || dx > 150) continue;
        if (e.kind != K_PIRANHA && (by(e.b) + e.b.h < my - 24 || by(e.b) > my + m.b.h + 24)) continue;
        if (dx < enemyDx) enemyDx = dx;
    }
    return enemyDx;
}

static Input aiThink() {
    Input in = {};
    if (scene != SC_PLAY || m.state != M_PLAY) { ai.hold = 0; return in; }
    if (ai.cooldown > 0) ai.cooldown--;
    if (ai.fireCd > 0) ai.fireCd--;
    if (ai.runCd > 0) ai.runCd--;

    if (ai.hold > 0) {
        in.jumpHeld = true;
        ai.hold--;
    }

    int enemyDx = enemyAhead();
    if (enemyDx < 120 && ai.fireCd == 0) {
        in.firePressed = true;
        ai.fireCd = m.form == F_FIRE ? 14 : 24;
    }

    if (!m.b.onGround || ai.hold > 0 || ai.cooldown > 0) return in;

    int speed = m.speed;
    Plan walk = simulateJump(0, 0, speed);
    bool walkSafe = walk.survive > AI_HORIZON;
    int hold = -1;
    if (!walkSafe) {
        // danger ahead: jump at the last moment a jump still gets us through
        Plan now, later;
        int hNow = bestJump(0, speed, &now);
        bestJump(2, speed, &later);
        bool safeNow = now.survive > AI_HORIZON, safeLater = later.survive > AI_HORIZON;
        if (safeNow && (!safeLater || walk.wallT <= 3)) {
            hold = hNow;
        } else if (!safeNow) {
            // one jump isn't enough: plan a jump plus a follow-up
            hNow = bestJump(0, speed, &now, true);
            bestJump(2, speed, &later, true);
            safeNow = now.survive > AI_HORIZON;
            safeLater = later.survive > AI_HORIZON;
            if (safeNow && !safeLater) hold = hNow;
            else if (!safeNow && !safeLater && walk.survive <= 4) hold = hNow;  // best effort
            else if (!safeNow && !safeLater && !m.run && ai.runCd == 0) {
                Plan fast;  // maybe a run-up makes it
                bestJump(4, RUN_SPEED, &fast, true);
                if (fast.survive > AI_HORIZON) { in.runPressed = true; ai.runCd = 45; }
            }
        }
    } else if (walk.wallT <= 3) {
        // a wall, pipe or step right in front: take the jump that gets furthest
        Plan now;
        int hNow = bestJump(0, speed, &now, true);
        if (now.survive > AI_HORIZON && now.progress > walk.progress) hold = hNow;
    } else if (blockAhead() && rndInt(4) != 0) {
        Plan p = simulateJump(0, 8, speed);
        if (planOk(p, speed)) hold = 8;
    }

    if (hold > 0) {
        in.jumpPressed = in.jumpHeld = true;
        ai.hold = hold - 1;
        ai.cooldown = 3;
    } else if (!in.runPressed && ai.runCd == 0) {
        // sprint now and then when nothing is coming, walk again once enemies show up
        bool wantRun = m.run;
        if (enemyDx < 200) wantRun = false;
        else if (rndInt(120) == 0) wantRun = !m.run;
        if (wantRun != m.run) {
            in.runPressed = true;
            ai.runCd = 60;
        }
    }
    return in;
}

// ------------------------------------------------------------------ updates
static void updateMarioPlay(const Input& in) {
    Body& b = m.b;
    if (in.runPressed) {
        m.run = !m.run;
        sfx(SFX_RUN, m.run);
    }
    int target = m.run ? RUN_SPEED : WALK_SPEED;
    if (m.speed < target) m.speed += 2;
    else if (m.speed > target) m.speed -= 2;
    b.vx = m.speed;

    if (in.jumpPressed) m.jumpBuf = 5;  // a tap just before landing still counts
    if (m.jumpBuf > 0) m.jumpBuf--;
    if (b.onGround) m.coyote = COYOTE;
    else if (m.coyote > 0) m.coyote--;
    if (m.jumpBuf > 0 && (b.onGround || m.coyote > 0)) {
        m.jumpBuf = 0;
        m.coyote = 0;
        b.vy = -(JUMP_VEL + (m.speed > WALK_SPEED ? JUMP_RUN : 0));
        m.jumpHold = HOLD_TICKS;
        b.onGround = false;
        sfx(m.form == F_SMALL ? SFX_JUMP : SFX_JUMP_BIG);
    }
    if (m.jumpHold > 0) {
        if (in.jumpHeld && b.vy < 0) m.jumpHold--;
        else m.jumpHold = 0;
    }
    b.vy += m.jumpHold > 0 ? GRAV_HOLD : GRAV;
    if (b.vy > MAX_FALL) b.vy = MAX_FALL;

    // every form can throw; the Fire Flower allows two fireballs at once and a faster rate
    if (in.firePressed && m.fireCd == 0) {
        int n = 0;
        for (auto& e : ents) n += e.kind == K_FIREBALL;
        if (n < (m.form == F_FIRE ? 2 : 1)) {
            Ent* f = spawn(K_FIREBALL, bx(b) + b.w, by(b) + 8, 8, 8);
            if (f) { f->b.vx = 80; f->b.vy = 32; }
            m.fireCd = m.form == F_FIRE ? 6 : 10;
            sfx(SFX_FIRE);
            plat_led(255, 90, 0, 80);
        }
    }
    if (m.fireCd > 0) m.fireCd--;

    int hc = 0, hr = 0;
    int f = moveBody(b, &hc, &hr);
    if (f & MV_HEAD) hitBlock(hc, hr);
    if (b.onGround) {
        m.chain = 0;
        m.anim += m.speed;
        lastGroundCol = (bx(b) + b.w / 2) >> 4;
    }

    // coins in the map
    int px = bx(b), py = by(b);
    for (int r = py >> 4; r <= (py + b.h - 1) >> 4; r++)
        for (int c = px >> 4; c <= (px + b.w - 1) >> 4; c++)
            if (tileAt(c, r) == T_COIN) {
                tmap[r][c] = T_EMPTY;
                addCoin();
            }

    // camera follows forward only
    if (px - 80 > camX) camX = px - 80;
    if (camX > lvW * 16 - FB_W) camX = lvW * 16 - FB_W;

    // falling into a hole / lava
    int feetC = (px + b.w / 2) >> 4;
    if (py > FB_H + 8 || tileAt(feetC, (py + b.h - 2) >> 4) == T_LAVA) {
        killMario();
        return;
    }

    if (m.invuln > 0) m.invuln--;
    if (m.star > 0) {
        m.star--;
        if (m.star % 4 == 0) plat_led(rndInt(2) * 255, rndInt(2) * 255, 255, 120);
    }

    // timer
    if (++timeSub >= 12) {
        timeSub = 0;
        if (timeLeft > 0 && --timeLeft == 0) killMario();
    }

    // flagpole
    int poleX = LV->flag * 16 + 7;
    if (px + b.w >= poleX) {
        m.state = M_FLAG;
        m.st = 0;
        b.x = (poleX - b.w + 3) * FP;
        b.vx = b.vy = 0;
        int height = 112 - (py + b.h);
        int pts = height >= 80 ? 5000 : height >= 56 ? 2000 : height >= 40 ? 800 : height >= 20 ? 400 : 100;
        addScore(pts, poleX + 6, py);
        sfx(SFX_FLAG);
        plat_led(0, 255, 0, 400);
    }
}

static void updateEnemiesAndItems() {
    spawnLevelEnemies();
    for (auto& e : ents) {
        if (e.kind == K_NONE) continue;
        Body& b = e.b;
        int px = bx(b);
        if (px < camX - 64 || px > camX + FB_W + 96) { e.kind = K_NONE; continue; }
        switch (e.kind) {
            case K_GOOMBA:
            case K_KOOPA:
            case K_SHELL: {
                b.vy += GRAV;
                if (b.vy > MAX_FALL) b.vy = MAX_FALL;
                int f = moveBody(b);
                if (f & MV_WALL) {
                    b.vx = -b.vx;
                    if (e.kind == K_SHELL && px < camX + FB_W) sfx(SFX_BUMP);
                }
                if (e.aux > 0 && e.kind == K_SHELL) e.aux--;
                if (by(b) > FB_H + 16) e.kind = K_NONE;
                break;
            }
            case K_PIRANHA: {
                // phases: 0 hidden, 1 rising, 2 out, 3 sinking. Hides when the player is close.
                int pipeMid = px + 5;
                int dist = bx(m.b) + m.b.w / 2 - pipeMid;
                if (dist < 0) dist = -dist;
                bool near = dist < 36;
                int h = b.h;
                if (near && e.sub != 0) e.sub = 3;
                switch (e.sub) {
                    case 0: if (--e.t <= 0 && !near) e.sub = 1; break;
                    case 1: if (++h >= 22) { h = 22; e.sub = 2; e.t = 40; } break;
                    case 2: if (--e.t <= 0) e.sub = 3; break;
                    case 3: h -= near ? 2 : 1; if (h <= 0) { h = 0; e.sub = 0; e.t = 40; } break;
                }
                b.h = h;
                b.y = (e.aux - h) * FP;
                break;
            }
            case K_MUSHROOM:
            case K_ONEUP:
            case K_STAR:
            case K_FLOWER:
                if (e.sub) {  // rising out of the block
                    b.y -= FP;
                    if (--e.t <= 0) {
                        // the plumber never stops, so power-ups chase him and hop obstacles
                        e.sub = 0;
                        b.vx = ITEM_SPEED;
                        b.vy = -48;
                    }
                    break;
                }
                b.vy += e.kind == K_STAR ? 8 : GRAV;
                if (b.vy > MAX_FALL) b.vy = MAX_FALL;
                {
                    int f = moveBody(b);
                    if ((f & MV_WALL) && b.onGround) b.vy = -96;
                    if (e.kind == K_STAR && (f & MV_LAND)) b.vy = -88;
                }
                if (by(b) > FB_H + 16) e.kind = K_NONE;
                break;
            case K_FIREBALL: {
                b.vy += 8;
                if (b.vy > 80) b.vy = 80;
                int f = moveBody(b);
                if (f & MV_LAND) b.vy = -56;
                if ((f & MV_WALL) || by(b) > FB_H || px > camX + FB_W || ++e.t > FIRE_LIFE) {
                    e.kind = K_PUFF;
                    e.t = 8;
                }
                break;
            }
            case K_FLAT:
            case K_PUFF:
                if (--e.t <= 0) e.kind = K_NONE;
                break;
            case K_KNOCKED:
            case K_DEBRIS:
                b.x += b.vx;
                b.y += b.vy;
                b.vy += e.kind == K_DEBRIS ? 10 : 12;
                if (by(b) > FB_H + 24) e.kind = K_NONE;
                break;
            case K_COINPOP:
                b.y += b.vy;
                b.vy += 12;
                if (--e.t <= 0) {
                    e.kind = K_NONE;
                    popup(200, px, by(b));  // points were added by addCoin()
                }
                break;
            case K_SCORE:
                b.y -= FP / 2;
                if (--e.t <= 0) e.kind = K_NONE;
                break;
        }
    }

    // fireballs and moving shells vs enemies
    for (auto& a : ents) {
        bool fire = a.kind == K_FIREBALL;
        bool shell = a.kind == K_SHELL && a.b.vx != 0;
        if (!fire && !shell) continue;
        for (auto& e : ents) {
            if (&e == &a || !isEnemy(e.kind)) continue;
            if (e.kind == K_PIRANHA && e.b.h < 4) continue;
            if (!overlap(a.b, e.b)) continue;
            knockOut(e, fire ? 200 : 500);
            sfx(SFX_KICK);
            if (fire) { a.kind = K_PUFF; a.t = 8; break; }
        }
    }
}

static void marioVsThings() {
    if (m.state != M_PLAY) return;
    for (auto& e : ents) {
        if (e.kind == K_NONE || !overlap(m.b, e.b)) continue;
        if (e.kind >= K_MUSHROOM && e.kind <= K_STAR) {
            if (!e.sub) collectItem(e);
            continue;
        }
        if (!isEnemy(e.kind)) continue;
        if (e.kind == K_PIRANHA && e.b.h < 4) continue;
        if (m.star > 0) {
            knockOut(e, 200);
            sfx(SFX_KICK);
            continue;
        }
        int mBottom = by(m.b) + m.b.h, eTop = by(e.b);
        bool stomp = e.kind != K_PIRANHA && m.b.vy > 0 && mBottom - eTop <= 10;
        if (stomp) {
            static const int CHAIN[] = {100, 200, 400, 500, 800, 1000, 2000, 4000, 5000, 8000};
            int pts = CHAIN[m.chain < 9 ? m.chain : 9];
            if (m.chain >= 10) oneUp(bx(e.b), eTop - 8);
            else addScore(pts, bx(e.b), eTop - 8);
            m.chain++;
            m.b.vy = -STOMP_VEL;
            m.coyote = 0;
            m.jumpHold = 8;  // holding JUMP while stomping bounces higher
            m.b.y = (eTop - m.b.h) * FP;
            sfx(SFX_STOMP);
            plat_led(255, 255, 255, 60);
            if (e.kind == K_GOOMBA) {
                e.kind = K_FLAT;
                e.t = 15;
            } else if (e.kind == K_KOOPA) {
                e.kind = K_SHELL;
                e.b.y += 8 * FP;
                e.b.h = 14;
                e.b.vx = 0;
            } else if (e.kind == K_SHELL) {
                if (e.b.vx) e.b.vx = 0;
                else { e.b.vx = 80; e.aux = 10; sfx(SFX_KICK); }
            }
            continue;
        }
        if (e.kind == K_SHELL) {
            if (e.b.vx == 0) {
                e.b.vx = (bx(m.b) + 6 < bx(e.b) + 7) ? 80 : -80;
                e.aux = 10;  // grace ticks: the kick can't hurt us
                addScore(400, bx(e.b), by(e.b) - 8);
                sfx(SFX_KICK);
                continue;
            }
            if (e.aux > 0) continue;
        }
        hurtMario();
    }
}

static void nextLevel() {
    li = (li + 1) % LEVEL_COUNT;
    respawnFrom = -1;
    startIntro();
}

static void loseLife() {
    lives--;
    m.form = F_SMALL;
    if (lives <= 0) {
        scene = SC_GAMEOVER;
        sceneT = 100;
        sfx(SFX_GAMEOVER);
        return;
    }
    startIntro();
}

static void updateMario(const Input& in) {
    switch (m.state) {
        case M_PLAY:
            updateMarioPlay(in);
            break;
        case M_GROW:
        case M_SHRINK:
            // the world freezes while the old and new forms flicker
            if (m.st % 4 == 0) m.shownForm = m.shownForm == m.form ? m.fromForm : m.form;
            if (--m.st <= 0) {
                m.state = M_PLAY;
                m.shownForm = m.form;
            }
            break;
        case M_DYING:
            m.st++;
            if (m.st == 14) m.b.vy = -128;
            if (m.st > 14) {
                m.b.y += m.b.vy;
                m.b.vy += 10;
            }
            if (m.st > 75) loseLife();
            break;
        case M_FLAG: {
            m.st++;
            int bottom = by(m.b) + m.b.h;
            if (bottom < 112) m.b.y += 3 * FP;
            if (by(m.b) + m.b.h > 112) m.b.y = (112 - m.b.h) * FP;
            if (flagY < 98) flagY += 3;
            if (m.st > 36 && bottom >= 112) {
                m.state = M_WALKOFF;
                m.b.x = (LV->flag * 16 + 12) * FP;
                m.b.vx = WALK_SPEED;
                m.run = false;
                m.speed = WALK_SPEED;
                sfx(SFX_CLEAR);
            }
            break;
        }
        case M_WALKOFF: {
            m.b.vy += GRAV;
            if (m.b.vy > MAX_FALL) m.b.vy = MAX_FALL;
            moveBody(m.b);
            m.anim += m.speed;
            int px = bx(m.b);
            if (px - 80 > camX && camX < lvW * 16 - FB_W) camX = px - 80;
            if (camX > lvW * 16 - FB_W) camX = lvW * 16 - FB_W;
            if (px + m.b.w / 2 >= LV->castle * 16 + 40) {
                m.state = M_INCASTLE;
                m.visible = false;
                tallyWait = 40;
            }
            break;
        }
        case M_INCASTLE:
            if (timeLeft > 0) {
                int d = timeLeft >= 3 ? 3 : timeLeft;
                timeLeft -= d;
                score += 50 * d;
                if (tick % 3 == 0) sfx(SFX_TALLY);
            } else if (--tallyWait <= 0) {
                nextLevel();
            }
            break;
    }
}

static void updatePlay(const Input& in) {
    for (auto& b : bumps)
        if (b.t > 0) b.t--;
    bool frozen = m.state == M_GROW || m.state == M_SHRINK || m.state == M_DYING;
    updateMario(in);
    if (scene != SC_PLAY) return;
    if (!frozen) {
        updateEnemiesAndItems();
        marioVsThings();
    }
}

static void setPlayMode(bool on) {
    if (playMode == on) return;
    playMode = on;
    if (on) {
        bannerT = 75;
        m.run = false;
        if (lives < 3) lives = 3;
        if (scene == SC_GAMEOVER) newGame(li);
        sfx(SFX_MODE);
        plat_led(0, 120, 255, 250);
        printf("[mode] PLAY\n");
    } else {
        memset(&ai, 0, sizeof(ai));
        printf("[mode] DEMO\n");
    }
}

// ------------------------------------------------------------------ API
static void game_init(uint32_t seed) {
    rng = seed ? seed : 1;
    playMode = false;
    newGame();
}

static void game_tick(const Input& touch) {
    tick++;
    bool pressed = touch.jumpPressed || touch.firePressed || touch.runPressed;
    if (pressed) {
        lastInputTick = tick;
        setPlayMode(true);
    }
    if (playMode && tick - lastInputTick > IDLE_TO_DEMO_TICKS) setPlayMode(false);
    Input in = playMode ? touch : aiThink();
    autoFireOn = playMode && touch.autoFire;
    if (playMode && touch.autoFire && scene == SC_PLAY && m.state == M_PLAY && enemyAhead() < 120)
        in.firePressed = true;  // throw rate is limited by fireCd and the fireball count

    // button highlight state for the overlay
    if (in.jumpHeld || in.jumpPressed) btnJumpT = 3;
    else if (btnJumpT > 0) btnJumpT--;
    if (in.firePressed) btnFireT = 6;
    else if (btnFireT > 0) btnFireT--;
    if (in.runPressed) btnRunT = 6;
    else if (btnRunT > 0) btnRunT--;
    if (bannerT > 0) bannerT--;

    switch (scene) {
        case SC_INTRO:
            if (--sceneT <= 0) {
                loadLevel();
                scene = SC_PLAY;
            }
            break;
        case SC_PLAY:
            updatePlay(in);
            break;
        case SC_GAMEOVER:
            if (--sceneT <= 0) newGame(li);
            break;
    }
}

// ------------------------------------------------------------------ rendering
static const uint16_t WHITE = 0xFFFF;
static const uint16_t SHADOW = 0x0841;

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static void textC(int cx, int y, const char* s, uint16_t ink) { text(cx - textWidth(s) / 2, y, s, ink, SHADOW); }

static void drawBackground(int theme) {
    fillRect(0, 0, FB_W, FB_H, SKY[theme]);
    if (theme == THEME_CAVE) return;
    if (theme == THEME_NIGHT) {
        for (int i = 0; i < 40; i++) {
            uint32_t h = hash32(i * 977 + 13);
            int sx = (int)(h % 480) - (camX / 8) % 480;
            if (sx < 0) sx += 480;
            int sy = 18 + (h >> 12) % 70;
            bool twinkle = ((tick + (h >> 20)) / 20) % 7 == 0;
            if (sx < FB_W) pset(sx, sy, twinkle ? RGB(120, 120, 170) : RGB(230, 230, 255));
        }
    }
    // far layer at half speed: hills, then clouds
    int far = camX / 2;
    for (int band = far / 150 - 1; band <= (far + FB_W) / 150 + 1; band++) {
        if (band < 0) continue;
        uint32_t h = hash32(band * 31 + 7);
        int hx = band * 150 + (int)(h % 60) - far;
        const Sprite& hs = *SC_HILL[theme];
        blit(hs, hx, GROUND_Y - hs.h);
    }
    for (int band = far / 110 - 1; band <= (far + FB_W) / 110 + 1; band++) {
        if (band < 0) continue;
        uint32_t h = hash32(band * 131 + 3);
        int cx = band * 110 + (int)(h % 50) - far;
        int cy = 22 + (int)((h >> 8) % 30);
        blit(*SC_CLOUD[theme], cx, cy);
    }
    // bushes on solid ground at full speed
    for (int band = camX / 128 - 1; band <= (camX + FB_W) / 128; band++) {
        if (band < 0) continue;
        uint32_t h = hash32(band * 71 + 5);
        if (h % 3 == 0) continue;
        int wx = band * 128 + (int)((h >> 4) % 72);
        bool ok = true;
        for (int c = wx >> 4; c <= (wx + 47) >> 4; c++)
            if (tileAt(c, 8) != T_GROUND || tileAt(c, 7) != T_EMPTY) ok = false;
        if (ok) blit(*SC_BUSH[theme], wx - camX, GROUND_Y - SC_BUSH[theme]->h);
    }
    // castle at the end of the level
    int cx = LV->castle * 16 - camX;
    if (cx < FB_W && cx > -80) blit(*SC_CASTLE[theme], cx, GROUND_Y - 80);
}

static const Sprite* tileSprite(int theme, int col, int row, uint8_t t) {
    const Sprite* const* T = TILESPR[theme];
    switch (t) {
        case T_GROUND: return T[TS_GROUND];
        case T_BRICK:
        case T_BRICK_COINS:
        case T_BRICK_STAR:
        case T_BRICK_1UP: return T[TS_BRICK];
        case T_QCOIN:
        case T_QPOWER: {
            static const uint8_t SEQ[4] = {TS_Q0, TS_Q1, TS_Q2, TS_Q1};
            return T[SEQ[(tick / 6) % 4]];
        }
        case T_USED: return T[TS_USED];
        case T_HARD: return T[TS_HARD];
        case T_PIPE_TL: return T[TS_PIPE_TL];
        case T_PIPE_TR: return T[TS_PIPE_TR];
        case T_PIPE_L: return T[TS_PIPE_L];
        case T_PIPE_R: return T[TS_PIPE_R];
        case T_TREE: {
            bool l = tileAt(col - 1, row) == T_TREE, r = tileAt(col + 1, row) == T_TREE;
            return T[l && r ? TS_TREE_M : (l ? TS_TREE_R : TS_TREE_L)];
        }
        case T_TRUNK: return T[TS_TRUNK];
        case T_LAVA: return T[(tick / 10) % 2 ? TS_LAVA1 : TS_LAVA0];
    }
    return nullptr;
}

static void drawTiles(int theme) {
    for (int col = camX >> 4; col <= (camX + FB_W - 1) >> 4; col++) {
        int sx = col * 16 - camX;
        for (int row = 0; row < LEVEL_ROWS; row++) {
            uint8_t t = tileAt(col, row);
            if (t == T_EMPTY) continue;
            int sy = row * 16;
            if (t == T_COIN) {
                blit(*COIN_SPIN[(tick / 5 + col) % 4], sx + 4, sy + 1);
                continue;
            }
            for (auto& b : bumps)
                if (b.t > 0 && b.col == col && b.row == row) sy -= b.t > 4 ? 8 - b.t : b.t;
            const Sprite* s = tileSprite(theme, col, row, t);
            if (s) blit(*s, sx, sy);
        }
    }
}

static void drawFlag(int theme) {
    int px = LV->flag * 16 + 7 - camX;
    if (px < -20 || px > FB_W + 4) return;
    fillRect(px, 22, 2, 90, RGB(214, 240, 214));
    pset(px + 1, 22, RGB(140, 180, 140));
    for (int y = 22; y < 112; y += 2) pset(px + 1, y, RGB(140, 196, 140));
    blit(*SC_FLAGBALL[theme], px - 3, 15);
    blit(*SC_FLAG[theme], px - 15, flagY);
}

static void drawEnt(const Ent& e, bool behindTiles) {
    int sx = bx(e.b) - camX, sy = by(e.b);
    bool behind = e.kind == K_PIRANHA || (e.sub && e.kind >= K_MUSHROOM && e.kind <= K_STAR);
    if (behind != behindTiles) return;
    BlitOpt o;
    switch (e.kind) {
        case K_GOOMBA:
            o.flip = (tick / 8) % 2;
            blitEx(SPR_GOOMBA, sx - 1, sy - 1, o);
            break;
        case K_FLAT:
            blit(SPR_GOOMBA_FLAT, sx - 1, sy + 15 - 8);
            break;
        case K_KOOPA:
            o.flip = e.b.vx > 0;
            blitEx(SPR_KOOPA, sx - 1, sy - 2, o);
            break;
        case K_SHELL:
            blit(SPR_SHELL, sx - 1, sy - 1);
            break;
        case K_PIRANHA:
            if (e.b.h > 0) blit((tick / 8) % 2 ? SPR_PIRANHA0 : SPR_PIRANHA1, sx - 3, e.aux - e.b.h);
            break;
        case K_KNOCKED: {
            o.vflip = true;
            const Sprite& s = e.aux == 0 ? SPR_GOOMBA : (e.aux == 1 ? SPR_KOOPA : SPR_SHELL);
            blitEx(s, sx - 1, sy, o);
            break;
        }
        case K_MUSHROOM: blit(SPR_MUSHROOM, sx, sy); break;
        case K_ONEUP: blit(SPR_ONEUP, sx, sy); break;
        case K_FLOWER: blit(SPR_FLOWER, sx, sy); break;
        case K_STAR: {
            static const uint16_t TINTS[4] = {0xFFFF, 0xFD20, 0x07FF, 0xF81F};
            o.mode = BM_TINT;
            o.tint = TINTS[(tick / 2) % 4];
            o.alpha = 70;
            blitEx(SPR_STAR, sx, sy, o);
            break;
        }
        case K_FIREBALL: blit(*FIREBALL[(e.t / 2) % 4], sx, sy); break;
        case K_PUFF:
            fillCircle(sx + 4, sy + 4, (10 - e.t) / 2 + 1, RGB(255, 240, 200));
            break;
        case K_COINPOP: blit(*COIN_SPIN[(e.t / 2) % 4], sx, sy); break;
        case K_DEBRIS: blit(SPR_DEBRIS, sx, sy); break;
        case K_SCORE: {
            char buf[8];
            if (e.aux < 0) snprintf(buf, sizeof(buf), "1UP");
            else snprintf(buf, sizeof(buf), "%d", e.aux);
            text(sx, sy, buf, WHITE, SHADOW);
            break;
        }
    }
}

static void drawMario() {
    if (!m.visible) return;
    if (m.invuln > 0 && m.state == M_PLAY && (tick / 2) % 2) return;
    int sx = bx(m.b) - camX, sy = by(m.b);
    if (m.state == M_DYING) {
        blit(SPR_MS_DEAD, sx - 2, sy - 1);
        return;
    }
    uint8_t form = m.shownForm;
    const Sprite* const* set = form == F_SMALL ? MARIO_SMALL : (form == F_BIG ? MARIO_BIG : MARIO_FIRE);
    int pose;
    if (m.state == M_FLAG) pose = 4;
    else if (!m.b.onGround && m.state != M_GROW && m.state != M_SHRINK) pose = 4;
    else if (m.state == M_GROW || m.state == M_SHRINK) pose = 0;
    else pose = 1 + (m.anim / (FP * (m.run ? 7 : 6))) % 3;
    const Sprite& s = *set[pose];
    // sprites are bottom-aligned to the hitbox
    int bottom = sy + m.b.h;
    BlitOpt o;
    if (m.star > 0) {
        static const uint16_t TINTS[4] = {0xFFE0, 0xF800, 0x07E0, 0x001F};
        o.mode = BM_TINT;
        o.tint = TINTS[(tick / 2) % 4];
        o.alpha = m.star > 60 ? 110 : ((tick / 4) % 2 ? 110 : 0);
    }
    blitEx(s, sx - 2, bottom - s.h, o);
}

static void drawHud() {
    char buf[24];
    text(10, 3, "MARIO", WHITE, SHADOW);
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)(score % 1000000));
    text(10, 12, buf, WHITE, SHADOW);
    // coin icon
    fillRect(73, 13, 4, 6, RGB(255, 210, 40));
    fillRect(74, 12, 2, 8, RGB(255, 210, 40));
    pset(74, 14, RGB(255, 250, 200));
    pset(74, 15, RGB(255, 250, 200));
    snprintf(buf, sizeof(buf), "x%02d", coins);
    text(80, 12, buf, WHITE, SHADOW);
    text(118, 3, "WORLD", WHITE, SHADOW);
    text(124, 12, LEVELS[li].name, WHITE, SHADOW);
    text(178, 3, "TIME", WHITE, SHADOW);
    if (scene == SC_PLAY) {
        snprintf(buf, sizeof(buf), "%3d", timeLeft);
        text(180, 12, buf, WHITE, SHADOW);
    }
}

// three translucent round buttons along the right edge
#define BTN_X 222
static const int BTN_Y[3] = {50, 92, 134};
#define BTN_R 15

static void drawButtons() {
    const uint16_t ink = WHITE, lit = RGB(255, 214, 60);
    for (int i = 0; i < 3; i++) {
        int cy = BTN_Y[i];
        bool pressed = (i == 0 && btnJumpT > 0) || (i == 1 && btnFireT > 0) || (i == 2 && btnRunT > 0);
        blendEllipse(BTN_X, cy, BTN_R, BTN_R, pressed ? RGB(255, 255, 255) : RGB(0, 0, 20), pressed ? 120 : 105);
        uint16_t ring = pressed ? lit : RGB(230, 230, 240);
        if (i == 2 && m.run) ring = lit;
        if (i == 1 && autoFireOn) ring = lit;
        circleOutline(BTN_X, cy, BTN_R, ring);
    }
    // JUMP: up arrow
    {
        int cx = BTN_X, cy = BTN_Y[0];
        uint16_t c = btnJumpT > 0 ? lit : ink;
        for (int r = 0; r < 7; r++) hline(cx - r, cx + r, cy - 7 + r, c);
        fillRect(cx - 2, cy, 5, 7, c);
    }
    // FIRE: fireball
    {
        BlitOpt o;
        o.scale = 384;
        blitEx(*FIREBALL[(tick / 4) % 4], BTN_X - 4, BTN_Y[1] - 2, o);
    }
    // RUN: double chevron, gold while running
    {
        int cx = BTN_X, cy = BTN_Y[2];
        uint16_t c = m.run ? lit : ink;
        for (int k = 0; k < 2; k++) {
            int ox = cx - 7 + k * 6;
            thickLine(ox, cy - 6, ox + 5, cy, c);
            thickLine(ox + 5, cy, ox, cy + 6, c);
        }
    }
}

static void drawIntro() {
    fillRect(0, 0, FB_W, FB_H, 0x0000);
    drawHud();
    char buf[24];
    snprintf(buf, sizeof(buf), "WORLD %s", LEVELS[li].name);
    textC(104, 56, buf, WHITE);
    blit(*MARIO_SMALL[0], 80, 76);
    snprintf(buf, sizeof(buf), "x  %d", lives);
    text(104, 81, buf, WHITE, SHADOW);
    if (!playMode) textC(104, 118, "DEMO", RGB(160, 160, 180));
}

static void game_render() {
    if (scene == SC_INTRO) {
        drawIntro();
        drawButtons();
        return;
    }
    if (scene == SC_GAMEOVER) {
        fillRect(0, 0, FB_W, FB_H, 0x0000);
        drawHud();
        textC(104, 72, "GAME OVER", WHITE);
        drawButtons();
        return;
    }
    int theme = LV->theme;
    drawBackground(theme);
    for (auto& e : ents)
        if (e.kind) drawEnt(e, true);
    drawTiles(theme);
    drawFlag(theme);
    for (auto& e : ents)
        if (e.kind && e.kind != K_SCORE) drawEnt(e, false);
    drawMario();
    for (auto& e : ents)
        if (e.kind == K_SCORE) drawEnt(e, false);
    if (theme == THEME_CAVE) blendRect(0, 0, FB_W, 21, 0x0000, 170);  // keep the HUD readable over the ceiling
    drawHud();
    if (!playMode) {
        if ((tick / 20) % 3 != 2) textC(104, 26, "DEMO - TAP A BUTTON TO PLAY", WHITE);
    } else if (bannerT > 0) {
        textC(104, 26, "YOUR TURN!", RGB(255, 230, 90));
    }
    drawButtons();
}
