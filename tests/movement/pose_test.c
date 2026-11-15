/* Unit tests for the falling pose (fax_falling.c), against model.h: the
 * hero's update and both pose routines as the ROM has them, with the hook
 * sites where the ROM has them. */
#include "fax_falling.c"
#include "model.h"

enum { FRAMES = 120, JUMP = 3, CLIMB = 7 };
static const char *const LEDGE[13] = {
    "................", "................", "................", "................", "................",
    "................", "####............", "................", "................", "................",
    "................", "................", "################",
};
static const char *const LADDER[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "................", "................",
    "................", "................", "################",
};
static const char *const HIGH[13] = {      /* a ledge high enough that a jump's arc ends in the air */
    "................", "................", "................", "................", "................",
    "####............", "................", "................", "................", "................",
    "................", "................", "################",
};

static void feature(bool on) {
    reset_cb();
    if (on) activate_cb();
}
typedef struct { Frame f[FRAMES]; } Trace;
typedef uint8_t (*Input)(int frame);
static uint8_t start_iframes;                       /* Player_InvincibilityPhase at the start */

/* The same play with the feature off and on. Whatever the feature does, the
 * hero goes the same way: it only answers the pose routines. */
static void play(const char *const *rows, uint8_t x, uint8_t y, uint8_t flags, uint8_t status, Input input,
                 Trace *off, Trace *on) {
    for (int pass = 0; pass < 2; pass++) {
        Trace *t = pass ? on : off;
        load_screen(rows);
        place(x, y);
        ram[M_FLAGS] = flags;
        ram[M_STATUS] = status;
        ram[M_IFRAMES] = start_iframes;
        feature(pass);
        for (int i = 0; i < FRAMES; i++) t->f[i] = step(input(i));
    }
    for (int i = 0; i < FRAMES; i++)
        CHECK(on->f[i].x == off->f[i].x && on->f[i].y == off->f[i].y && on->f[i].flags == off->f[i].flags);
}
/* In the air without a jump, by the hero's own movement: falling this frame,
 * and not on the block yet. */
static bool falls(const Trace *t, int i) {
    return (t->f[i].flags & F_FALLING) && i + 1 < FRAMES && (t->f[i + 1].flags & F_FALLING) &&
           t->f[i + 1].y > t->f[i].y;
}

static void the_rule(void) {
    /* Every flag combination, both routines: the jump pose exactly when he is
     * falling, not jumping, attacking or climbing, and not just landed;
     * otherwise the game's own answer. */
    load_screen(LEDGE);
    unsigned changed = 0;
    for (unsigned flags = 0; flags < 256; flags++)
        for (int aligned = 0; aligned < 2; aligned++)
            for (int wing = 0; wing < 2; wing++)
                for (int held = 0; held < 2; held++)
                    for (int on_block = 0; on_block < 2; on_block++) {
                        place(aligned ? 0x40 : 0x48, 0x40);
                        ram[M_FLAGS] = (uint8_t)flags;
                        ram[M_STATUS] = wing ? S_WING_BOOTS : 0;
                        ram[M_JOY] = held ? B_A : 0;
                        ram[M_TICK] = 0x13;
                        ram[M_ATTACK_FRAME] = 1;
                        feature(false);
                        uint8_t body = body_frame(), gear = gear_frame();
                        CHECK(body == gear);                       /* the two copies agree */
                        feature(true);
                        landed = on_block;
                        bool climbing = (flags & F_CAN_CLIMB) && (aligned || !(flags & F_FALLING)) && (flags & F_CLIMBING);
                        bool rule = (flags & F_FALLING) && !(flags & (F_JUMPING | F_ATTACKING)) && !climbing && !on_block;
                        cpu.p = 0x27;
                        uint8_t hooked_body = body_frame();
                        if (rule) CHECK(!(cpu.p & (FLAG_N | FLAG_Z)) && (cpu.p & FLAG_C));   /* as LDA #$03 leaves them */
                        uint8_t hooked_gear = gear_frame();
                        CHECK(hooked_body == (rule ? JUMP : body) && hooked_gear == (rule ? JUMP : gear));
                        if (rule && body != JUMP) ++changed;
                    }
    CHECK(changed > 0);
}

static uint8_t walk_right(int i) { (void)i; return B_RIGHT; }
static int W;                                       /* walking right from x=8, the first frame off the ledge */
static void walking_off_a_ledge(void) {
    Trace off, on;
    play(LEDGE, 8, 64, F_RIGHT, 0, walk_right, &off, &on);
    for (W = 0; off.f[W].y == 64; W++) {}
    CHECK(W > 10 && off.f[W].y == 72);
    int fell = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (falls(&off, i)) {
            ++fell;
            CHECK(off.f[i].body != JUMP && off.f[i].gear != JUMP);  /* the original: mid-step, on air */
            CHECK(on.f[i].body == JUMP && on.f[i].gear == JUMP);
        } else {
            CHECK(on.f[i].body == off.f[i].body && on.f[i].gear == off.f[i].gear);
        }
    }
    CHECK(fell == 11);                                              /* 96 pixels, 8 a frame; the 12th lands */
    /* The landing frame still has PLAYER_FALLING_OFF, and gets the game's pose */
    int land = 0;
    while (!(off.f[land].y == 160)) land++;
    CHECK((on.f[land].flags & F_FALLING) && on.f[land].body == off.f[land].body && on.f[land].body != JUMP);
    CHECK(landed);
    CHECK(body >= 11 && gear >= 22);
}

static uint8_t jump_off(int i) { return (uint8_t)(B_RIGHT | (i == 30 ? B_A : 0)); }
static void jumping(void) {
    /* A jump looks as it did; past the end of its curve he falls on, and now
     * keeps the jump pose down to the floor. */
    Trace off, on;
    play(HIGH, 8, 48, F_RIGHT, 0, jump_off, &off, &on);
    int arc = 0, after = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (off.f[i].flags & F_JUMPING) {
            ++arc;
            CHECK(on.f[i].body == JUMP && off.f[i].body == JUMP && on.f[i].gear == off.f[i].gear);
        } else if (falls(&off, i)) {
            ++after;
            CHECK(off.f[i].body != JUMP && on.f[i].body == JUMP);
        } else {
            CHECK(on.f[i].body == off.f[i].body);
        }
    }
    CHECK(arc == 32 && after > 0);
}

static uint8_t step_off_ladder(int i) { return i < 40 ? B_RIGHT : 0; }
static uint8_t climb_up_then_off(int i) { return i < 30 ? B_UP : i < 60 ? B_RIGHT : 0; }
static void ladders(void) {
    /* Climbing: frame 7 as before. Stepping off: a fall, in the jump pose. */
    Trace off, on;
    play(LADDER, 128, 80, F_RIGHT | F_CAN_CLIMB | F_CLIMBING, 0, step_off_ladder, &off, &on);
    int climbing = 0, fell = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (off.f[i].body == CLIMB) ++climbing;
        if (falls(&off, i)) { ++fell; CHECK(on.f[i].body == JUMP); }
        else CHECK(on.f[i].body == off.f[i].body && on.f[i].gear == off.f[i].gear);
    }
    CHECK(climbing > 5 && fell == 9);
    play(LADDER, 128, 80, F_RIGHT | F_CAN_CLIMB | F_CLIMBING, 0, climb_up_then_off, &off, &on);
    climbing = 0;
    for (int i = 0; i < 30; i++) {
        CHECK(off.f[i].body == CLIMB && on.f[i].body == CLIMB && on.f[i].gear == CLIMB);
        climbing += off.f[i].y < 80;
    }
    CHECK(climbing > 0);
}

static uint8_t attack_in_air(int i) { return (uint8_t)(B_RIGHT | (i == W + 2 ? B_B : 0)); }
static void attacking(void) {
    /* Swinging while falling: the attack frames, as before. */
    Trace off, on;
    play(LEDGE, 8, 64, F_RIGHT, 0, attack_in_air, &off, &on);
    int swings = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (off.f[i].flags & F_ATTACKING) {
            ++swings;
            CHECK(on.f[i].body == off.f[i].body && on.f[i].body >= 4 && on.f[i].body <= 6);
        }
    }
    CHECK(swings > 10 && (off.f[W + 2].flags & F_FALLING) && (off.f[W + 2].flags & F_ATTACKING));
}

static uint8_t hover(int i) { return (uint8_t)(B_RIGHT | (i >= W + 2 && i < W + 26 ? B_A : 0)); }
static void wing_boots(void) {
    /* Flying with A held: the game's own jump pose. Falling without A: a fall. */
    Trace off, on;
    play(LEDGE, 8, 64, F_RIGHT, S_WING_BOOTS, hover, &off, &on);
    int flying = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (i >= W + 2 && i < W + 26 && (off.f[i].flags & F_FALLING)) { ++flying; CHECK(off.f[i].body == JUMP && on.f[i].body == JUMP); }
        else if (falls(&off, i)) CHECK(on.f[i].body == JUMP);
        else CHECK(on.f[i].body == off.f[i].body);
    }
    CHECK(flying > 10);
}

static uint8_t nothing(int i) { (void)i; return 0; }
static void knockback(void) {
    /* Knocked back on the ground: as before. Knocked off a ledge: a fall. A
     * hit starts the invincibility phase at $3C; the push lasts 3 frames. */
    Trace off, on;
    start_iframes = 0x3C;
    play(LEDGE, 20, 64, 0, S_KNOCKBACK, nothing, &off, &on);
    for (int i = 0; i < FRAMES; i++) CHECK(on.f[i].body == off.f[i].body && on.f[i].gear == off.f[i].gear);
    play(LEDGE, 52, 64, 0, S_KNOCKBACK, nothing, &off, &on);
    int fell = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (falls(&off, i)) { ++fell; CHECK(on.f[i].body == JUMP); }
        else CHECK(on.f[i].body == off.f[i].body);
    }
    CHECK(fell > 0 && off.f[1].y > 64 && off.f[3].x > off.f[1].x);   /* the push drops PLAYER_FALLING_OFF itself */
    start_iframes = 0;
}

static void save_states(void) {
    uint8_t record[8];
    feature(true);
    landed = true;
    CHECK(save_get(record, sizeof record) == 1 && record[0] == 1 && save_validate(record, 1));
    landed = false;
    CHECK(save_set(record, 1) && landed);
    CHECK(save_set(record, 0) && !landed);
    record[0] = 2;
    CHECK(!save_validate(record, 1) && !save_set(record, 1) && !save_validate(record, 2));
    CHECK(!strcmp(save_id, "faxanadu.falling-pose"));
}

static void off_means_off(void) {
    feature(true);
    walking_off_a_ledge();
    CHECK(enabled && body > 0);
    reset_cb();
    CHECK(!enabled && body == 0 && gear == 0 && !landed);
    for (unsigned i = 0; i < nsites; i++) CHECK(!sites[i].on);
    CHECK(nsites == 4 && !strcmp(plugin_id, "faxanadu.falling-pose"));
}

int main(void) {
    the_rule();
    walking_off_a_ledge();
    jumping();
    ladders();
    attacking();
    wing_boots();
    knockback();
    save_states();
    off_means_off();
    printf("pose_test: %u checks passed\n", checks);
    return 0;
}
