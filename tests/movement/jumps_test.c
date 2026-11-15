/* Unit tests for forgiving jumps (fax_jumps.c), against model.h: the hero's
 * update as bank 15 has it, with the hook sites where the ROM has them. Each
 * case plays the same input with the feature on and off, and compares what
 * the hero does. */
#include "fax_jumps.c"
#include "model.h"

/* Block (fax_block.c), as fax_jumps.c asks it: up or not. */
static bool block_up;
bool fax_block_raised(void) { return block_up; }

enum { FRAMES = 120 };
typedef struct { Frame f[FRAMES]; } Trace;

/* A ledge two blocks wide at row 6 (the hero stands on it at Y=64), open air
 * to the right, and the floor at row 12 (Y=160): a 96-pixel drop. */
static const char *const LEDGE[13] = {
    "................", "................", "................", "................", "................",
    "................", "####............", "................", "................", "................",
    "................", "................", "################",
};
/* The same, with a ceiling right over the ledge's edge. */
static const char *const LOW_LEDGE[13] = {
    "................", "................", "................", "..###...........", "................",
    "................", "####............", "................", "................", "................",
    "................", "................", "################",
};
/* A ladder hanging in the air, its foot two blocks above the floor. */
static const char *const LADDER[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "................", "................",
    "................", "................", "################",
};
/* A step one block high. */
static const char *const STEP[13] = {
    "................", "................", "................", "................", "................",
    "................", "................", "................", "................", "................",
    "................", "####............", "################",
};
/* A flat floor. */
static const char *const FLOOR[13] = {
    "................", "................", "................", "................", "................",
    "................", "................", "................", "................", "................",
    "................", "................", "################",
};

static void feature(bool on) {
    reset_cb();
    if (on) activate_cb();
}

/* Walk right from (8, 64) holding Right, with A pressed (held one frame) on
 * the frames in `presses` (-1 ends the list). */
static Trace walk_off(const char *const *rows, bool on, const int *presses, uint8_t status) {
    load_screen(rows);
    place(8, 64);
    ram[M_STATUS] = status;
    feature(on);
    Trace t;
    for (int i = 0; i < FRAMES; i++) {
        uint8_t buttons = B_RIGHT;
        for (const int *p = presses; *p >= 0; p++)
            if (*p == i) buttons |= B_A;
        t.f[i] = step(buttons);
    }
    return t;
}
static const int NONE[] = {-1};
static int first(const Trace *t, bool (*test)(const Frame *)) {
    for (int i = 0; i < FRAMES; i++)
        if (test(&t->f[i])) return i;
    return -1;
}
static bool airborne(const Frame *f) { return f->y > 64; }
static bool jumping(const Frame *f) { return f->flags & F_JUMPING; }
static bool same(const Trace *a, const Trace *b, int from, int to) {
    for (int i = from; i < to; i++)
        if (memcmp(&a->f[i], &b->f[i], sizeof a->f[i])) return false;
    return true;
}
static uint8_t peak(const Trace *t, int from) {
    uint8_t top = 0xFF;
    for (int i = from; i < FRAMES; i++)
        if (t->f[i].y < top) top = t->f[i].y;
    return top;
}

static int W;                                    /* the first frame in the air, walking off the ledge */
static Trace still, best;                        /* no A; and A on the last frame on the ledge, the original's best */

static void the_original(void) {
    still = walk_off(LEDGE, false, NONE, 0);
    W = first(&still, airborne);
    CHECK(W > 10 && still.f[W - 1].y == 64 && still.f[W].y == 72 && (still.f[W].flags & F_FALLING));
    CHECK(still.f[W + 11].y == 160 && (still.f[W + 11].flags & F_FALLING));     /* the landing frame */
    CHECK(still.f[W + 12].y == 160 && !(still.f[W + 12].flags & F_FALLING));
    CHECK(first(&still, jumping) < 0);
    best = walk_off(LEDGE, false, (int[]){W - 1, -1}, 0);
    CHECK(first(&best, jumping) == W - 1 && best.f[W - 1].y == 56);
    CHECK(peak(&best, 0) == 32);                    /* a jump rises 32 pixels */
    for (int k = 0; k < 12; k++) {               /* one frame late, or more: lost */
        Trace late = walk_off(LEDGE, false, (int[]){W + k, -1}, 0);
        CHECK(same(&late, &still, 0, FRAMES));
    }
    /* x(t) is the same with or without a jump: no steering in the air */
    for (int i = 0; i < FRAMES; i++) CHECK(best.f[i].x == still.f[i].x);
}

static void coyote_time(void) {
    for (unsigned k = 0; k < COYOTE_FRAMES; k++) {
        Trace t = walk_off(LEDGE, true, (int[]){W + (int)k, -1}, 0);
        CHECK(coyote_jumps == 1 && buffered_jumps == 0);
        CHECK(same(&t, &still, 0, W + (int)k));
        CHECK(first(&t, jumping) == W + (int)k);
        /* this update's fall step and the jump's first step cancel */
        CHECK(t.f[W + k].y == (k ? still.f[W + k - 1].y : 64));
        /* the best original jump's arc, one update later and 8(k+1) pixels lower: never above it, never beyond it */
        for (int i = 0; i < FRAMES; i++) {
            CHECK(t.f[i].x == best.f[i].x);
            CHECK(t.f[i].y >= best.f[i].y);
        }
        CHECK(peak(&t, W) == 32 + 8 * (k + 1));
    }
    /* After the window, he falls as before. This drop is long enough that a
     * press on the next update is outside the buffer too. */
    Trace t = walk_off(LEDGE, true, (int[]){W + COYOTE_FRAMES, -1}, 0);
    CHECK(same(&t, &still, 0, FRAMES) && coyote_jumps == 0);
    /* One coyote jump per ledge: a second press in the window does nothing more */
    Trace twice = walk_off(LEDGE, true, (int[]){W, W + 2, -1}, 0);
    CHECK(coyote_jumps == 1);
    Trace once = walk_off(LEDGE, true, (int[]){W, -1}, 0);
    CHECK(same(&twice, &once, 0, FRAMES));
}

static void not_from_a_ceiling(void) {
    /* The original cannot jump at the edge here, so neither can a coyote jump. */
    Trace none = walk_off(LOW_LEDGE, false, NONE, 0);
    int w = first(&none, airborne);
    CHECK(w == W);
    Trace edge = walk_off(LOW_LEDGE, false, (int[]){w - 1, -1}, 0);
    CHECK(same(&edge, &none, 0, FRAMES));
    unsigned calls = isolated_calls;
    for (unsigned k = 0; k < COYOTE_FRAMES; k++) {
        Trace t = walk_off(LOW_LEDGE, true, (int[]){w + (int)k, -1}, 0);
        CHECK(same(&t, &none, 0, FRAMES));
    }
    CHECK(isolated_calls == calls + COYOTE_FRAMES);   /* asked the game each time, in a scratch copy */
    CHECK(!isolated);
}

static void jump_buffering(void) {
    /* A in the air, 1-6 updates before the first on the ground (W+12), jumps
     * there; the jump is the one the original makes with A pressed then. */
    Trace on_time = walk_off(LEDGE, false, (int[]){W + 12, -1}, 0);
    CHECK(first(&on_time, jumping) == W + 12);
    for (int j = 1; j <= (int)BUFFER_FRAMES; j++) {
        Trace t = walk_off(LEDGE, true, (int[]){W + 12 - j, -1}, 0);
        CHECK(buffered_jumps == 1 && coyote_jumps == 0);
        CHECK(same(&t, &still, 0, W + 12));
        CHECK(same(&t, &on_time, W + 12, FRAMES));
    }
    Trace early = walk_off(LEDGE, true, (int[]){W + 12 - BUFFER_FRAMES - 1, -1}, 0);
    CHECK(same(&early, &still, 0, FRAMES) && buffered_jumps == 0);

    /* From a jump on flat ground: the press counts from the jump's last frames. */
    load_screen(FLOOR);
    for (int j = 0; j <= (int)BUFFER_FRAMES + 2; j++) {
        Trace t[2];
        for (int on = 0; on < 2; on++) {
            place(40, 160);
            feature(on);
            for (int i = 0; i < FRAMES; i++) {
                uint8_t buttons = 0;
                if (i == 2) buttons = B_A;                          /* jump */
                if (i == 2 + 32 - j) buttons = B_A;                 /* he lands on frame 2 + 31 */
                t[on].f[i] = step(buttons);
            }
        }
        CHECK(t[0].f[33].y == 160 && !(t[0].f[33].flags & F_JUMPING));      /* the landing frame */
        bool buffered = j >= 1 && j <= (int)BUFFER_FRAMES;
        CHECK(same(&t[0], &t[1], 0, 34));
        CHECK(buffered == !same(&t[0], &t[1], 34, FRAMES));
        if (buffered) CHECK(t[1].f[34].y == 152 && (t[1].f[34].flags & F_JUMPING));
    }
}

/* Block takes A away while it is up (fax_block_input). A press kept from the
 * air just before Block went up does not jump on landing: not with Block held
 * through the landing, nor let go again before it. Without Block, the same
 * press jumps (jump_buffering). */
static void block_takes_the_press(void) {
    load_screen(FLOOR);
    for (int j = 1; j <= (int)BUFFER_FRAMES; j++) {
        int press = 2 + 32 - j;                                    /* he lands on frame 2 + 31 */
        const int let_go_at[] = { press + 2, 41 };                 /* one frame up; held past the landing */
        for (unsigned k = 0; k < 2; k++) {
            int let_go = let_go_at[k];
            Trace t[2];
            for (int on = 0; on < 2; on++) {
                place(40, 160);
                feature(on);
                for (int i = 0; i < FRAMES; i++) {
                    block_up = i > press && i < let_go;             /* up from the frame after the press */
                    uint8_t buttons = 0;
                    if (i == 2) buttons = B_A;                      /* jump */
                    if (i == press) buttons = B_A;                  /* kept: he is in the air */
                    if (i == press + 1) CHECK(!on || buffered);
                    t[on].f[i] = step(buttons);
                }
                block_up = false;
            }
            CHECK(same(&t[0], &t[1], 0, FRAMES) && buffered_jumps == 0 && !buffered);
            CHECK(t[1].f[40].y == 160 && !(t[1].f[40].flags & F_JUMPING));
        }
    }
}

/* On the ladder in LADDER, stepping off it to the right, or climbing down
 * off its foot; A pressed on frame `press`. */
enum { STEP_OFF, CLIMB_OFF };
static Trace ladder(int how, bool on, int press) {
    load_screen(LADDER);
    place(128, how == STEP_OFF ? 80 : 96);
    ram[M_FLAGS] = F_RIGHT | F_CAN_CLIMB | F_CLIMBING;
    feature(on);
    Trace t;
    bool off = false;
    for (int i = 0; i < FRAMES; i++) {
        uint8_t buttons = how == STEP_OFF ? (i < 40 ? B_RIGHT : 0) : (off ? 0 : B_DOWN);
        t.f[i] = step((uint8_t)(buttons | (i == press ? B_A : 0)));
        off |= (t.f[i].flags & F_FALLING) != 0;
    }
    return t;
}
static bool falling(const Frame *f) { return f->flags & F_FALLING; }
static void ladders(void) {
    /* Leaving a ladder is a fall without a ledge: no coyote time. A press on
     * the ladder or early in the fall does what it did; one within
     * BUFFER_FRAMES of landing is the original's jump on landing. */
    for (int how = STEP_OFF; how <= CLIMB_OFF; how++) {
        Trace none = ladder(how, false, -1);
        int drop = first(&none, falling), land = drop;
        while (none.f[land].flags & F_FALLING) land++;
        CHECK(drop > 5 && land > drop + 2 && none.f[land].y == 160);
        CHECK(none.f[drop - 1].body == 7 || how == CLIMB_OFF);
        Trace on_time = ladder(how, false, land);
        CHECK(first(&on_time, jumping) == land);
        for (int p = drop - 4; p < land; p++) {
            Trace a = ladder(how, false, p), b = ladder(how, true, p);
            CHECK(coyote_jumps == 0);
            if (p > drop && land - p <= (int)BUFFER_FRAMES)
                CHECK(buffered_jumps == 1 && same(&b, &a, 0, land) && same(&b, &on_time, land, FRAMES));
            else
                CHECK(buffered_jumps == 0 && same(&a, &b, 0, FRAMES));
        }
    }
}

static void wing_boots_and_knockback(void) {
    /* Wing Boots: A is the flying button; a fall pressed into is a hover. */
    for (int k = 0; k < (int)COYOTE_FRAMES + 3; k++) {
        Trace t[2];
        for (int on = 0; on < 2; on++) {
            load_screen(LEDGE);
            place(8, 64);
            ram[M_STATUS] = S_WING_BOOTS;
            feature(on);
            for (int i = 0; i < FRAMES; i++) t[on].f[i] = step(B_RIGHT | (i >= W + k && i < W + k + 20 ? B_A : 0));
        }
        CHECK(same(&t[0], &t[1], 0, FRAMES));
        CHECK(t[0].f[W + k + 5].y == t[0].f[W + k + 4].y + 1);       /* hovering */
    }
    /* Knocked off the ledge: no coyote time; a press during the knockback does what it did. */
    for (int k = 0; k <= (int)COYOTE_FRAMES; k++) {
        Trace t[2];
        for (int on = 0; on < 2; on++) {
            load_screen(LEDGE);
            place(52, 64);
            ram[M_FLAGS] = 0;                                         /* facing left: pushed right */
            ram[M_STATUS] = S_KNOCKBACK;
            ram[M_IFRAMES] = 0x3C;
            feature(on);
            for (int i = 0; i < FRAMES; i++) t[on].f[i] = step(i == 1 + k ? B_A : 0);
            CHECK(t[on].f[0].y == 64 && t[on].f[1].y == 72);           /* off the ledge on the knockback's 2nd frame */
        }
        CHECK(coyote_jumps == 0);
        if (k < 3) CHECK(same(&t[0], &t[1], 0, FRAMES));
    }
    /* Knocked off a step: a press during the knockback is not kept for the landing. */
    for (int k = 1; k < 3; k++) {
        Trace t[2];
        for (int on = 0; on < 2; on++) {
            load_screen(STEP);
            place(52, 144);
            ram[M_FLAGS] = 0;
            ram[M_STATUS] = S_KNOCKBACK;
            ram[M_IFRAMES] = 0x3C;
            feature(on);
            for (int i = 0; i < FRAMES; i++) t[on].f[i] = step(i == k ? B_A : 0);
            CHECK(t[on].f[2].y == 160);                                 /* down on the knockback's 3rd frame */
        }
        CHECK(same(&t[0], &t[1], 0, FRAMES) && buffered_jumps == 0);
    }
}

static void screen_changes(void) {
    /* A press kept from the air does not survive a new screen. */
    load_screen(LEDGE);
    place(8, 64);
    feature(true);
    unsigned before = buffered_jumps;
    for (int i = 0; i < FRAMES; i++) {
        if (i == W + 8) ram[M_SCREEN] = 4;
        step(B_RIGHT | (i == W + 7 ? B_A : 0));
    }
    CHECK(buffered_jumps == before);
    /* Nor coyote time */
    place(8, 64);
    feature(true);
    before = coyote_jumps;
    for (int i = 0; i < FRAMES; i++) {
        if (i == W) ram[M_SCREEN] = 4;
        step(B_RIGHT | (i == W + 1 ? B_A : 0));
    }
    CHECK(coyote_jumps == before);
}

static void save_states(void) {
    /* A save state in coyote time carries it: restored, the press still jumps. */
    Trace want = walk_off(LEDGE, true, (int[]){W + 2, -1}, 0);
    uint8_t record[64];
    for (int restore = 0; restore < 2; restore++) {
        load_screen(LEDGE);
        place(8, 64);
        feature(true);
        Trace t;
        int len = 0;
        uint8_t machine[sizeof ram];
        for (int i = 0; i < FRAMES; i++) {
            if (i == W + 1) {                                         /* saved after W, loaded before W+1 */
                len = save_get(record, sizeof record);
                CHECK(len == STATE_LEN && save_validate(record, len));
                memcpy(machine, ram, sizeof ram);
                feature(true);                                        /* another session */
                memcpy(ram, machine, sizeof ram);
                CHECK(save_set(record, restore ? len : 0));
            }
            t.f[i] = step(B_RIGHT | (i == W + 2 ? B_A : 0));
        }
        CHECK(restore ? same(&t, &want, 0, FRAMES) : same(&t, &still, 0, FRAMES));
    }
    CHECK(!save_validate(record, 3));
    record[0] = 0xFF;
    CHECK(!save_validate(record, STATE_LEN) && !save_set(record, STATE_LEN));
    CHECK(!strcmp(save_id, "faxanadu.forgiving-jumps"));
}

static void off_means_off(void) {
    walk_off(LEDGE, true, (int[]){W + 1, -1}, 0);
    CHECK(enabled && coyote_jumps > 0);
    reset_cb();
    CHECK(!enabled && coyote_jumps == 0 && buffered_jumps == 0);
    for (unsigned i = 0; i < nsites; i++) CHECK(!sites[i].on);
    CHECK(!strcmp(plugin_id, "faxanadu.forgiving-jumps"));
    CHECK(nsites == 3 && site_on(0xE2C8) == false);
}

int main(void) {
    the_original();
    coyote_time();
    not_from_a_ceiling();
    jump_buffering();
    block_takes_the_press();
    ladders();
    wing_boots_and_knockback();
    screen_changes();
    save_states();
    off_means_off();
    printf("jumps_test: %u checks passed (coyote %u frames, buffer %u frames)\n", checks, COYOTE_FRAMES,
           BUFFER_FRAMES);
    return 0;
}
