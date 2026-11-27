/* Unit tests for the ladder fixes (fax_ladders.c), against tests/movement/
 * model.h: the hero's update as bank 15 has it, with the hook sites where the
 * ROM has them. Each case plays the same input with the feature on and off,
 * and compares what the hero does. */
#include "fax_ladders.c"
#include "mod_savestate.h"
#include "../movement/model.h"

enum { FRAMES = 240 };
typedef struct {
    Frame f[FRAMES];
} Trace;

/* A ladder hanging in the air: its top block at row 5, its foot at row 9, two
 * blocks above the floor. Nothing over it. */
static const char *const LADDER[13] = {
    "................", "................", "................", "................", "................",
    "........H.......", "........H.......", "........H.......", "........H.......", "........H.......",
    "................", "................", "################",
};
/* The same, under a solid block. */
static const char *const CEILING[13] = {
    "................", "................", "................", "................", "........#.......",
    "........H.......", "........H.......", "........H.......", "........H.......", "........H.......",
    "................", "................", "################",
};
enum {
    LADDER_X = 0x80,
    TOP_Y = 5 * 16 - 0x1F, /* the highest Y with his feet in the top block: 0x31 */
    START_Y = 0x80,        /* head at row 8, feet at row 9: on the ladder */
};

static void feature(bool on) {
    reset_cb();
    if (on) activate_cb();
}

/* On the ladder, climbing, then each frame's buttons from `input`. */
static Trace play(const char *const *rows, bool on, uint8_t status, uint8_t (*input)(int frame)) {
    load_screen(rows);
    place(LADDER_X, START_Y);
    ram[M_FLAGS] = F_CAN_CLIMB | F_CLIMBING;
    ram[M_STATUS] = status;
    feature(on);
    Trace t;
    for (int i = 0; i < FRAMES; i++) t.f[i] = step(input(i));
    return t;
}
static uint8_t hold_up(int frame) {
    (void)frame;
    return B_UP;
}
static int release_at;
static uint8_t up_then_jump(int frame) { return frame < release_at ? B_UP : frame == release_at + 1 ? B_A : 0; }
static uint8_t fly_up(int frame) { return frame < 100 ? B_UP | B_A : 0; }
static uint8_t hold_down(int frame) {
    (void)frame;
    return B_DOWN;
}

static bool same(const Trace *a, const Trace *b, int from, int to) {
    for (int i = from; i < to; i++)
        if (memcmp(&a->f[i], &b->f[i], sizeof a->f[i])) return false;
    return true;
}
static int drops(const Trace *t) { /* updates he fell on */
    int n = 0;
    for (int i = 0; i < FRAMES; i++) n += (t->f[i].flags & F_FALLING) != 0;
    return n;
}
static int first_at(const Trace *t, uint8_t y) {
    for (int i = 0; i < FRAMES; i++)
        if (t->f[i].y == y) return i;
    return -1;
}

/* The original: Up held at the top lifts his feet out of the ladder, he drops
 * 8 pixels and is caught, again and again. */
static void the_original(void) {
    Trace off = play(LADDER, false, 0, hold_up);
    CHECK(drops(&off) >= 8);
    int top = first_at(&off, TOP_Y - 1);
    CHECK(top > 0); /* one pixel past the last on the ladder */
    CHECK(off.f[top + 1].y == TOP_Y - 1 + 8 && (off.f[top + 1].flags & F_FALLING));
    CHECK(!(off.f[top + 1].flags & (F_CAN_CLIMB | F_CLIMBING)));
    CHECK((off.f[top + 2].flags & (F_CAN_CLIMB | F_CLIMBING)) == (F_CAN_CLIMB | F_CLIMBING)); /* caught */
    printf("ok: the original bobs at a ladder's top: %d drops of 8 pixels in %d updates of Up\n", drops(&off), FRAMES);
}

/* With the fix: he climbs as before until the top, then stays there, on the
 * ladder, never falling, for as long as Up is held. */
static void stays_at_the_top(void) {
    Trace off = play(LADDER, false, 0, hold_up), on = play(LADDER, true, 0, hold_up);
    int top = first_at(&on, TOP_Y);
    CHECK(top > 0 && top == first_at(&off, TOP_Y));
    CHECK(same(&on, &off, 0, top)); /* the climb is the game's */
    CHECK(drops(&on) == 0);
    CHECK(on.f[top].body == 7); /* climbing up to it */
    for (int i = top + 1; i < FRAMES; i++) {
        CHECK(on.f[i].y == TOP_Y);
        CHECK((on.f[i].flags & (F_CAN_CLIMB | F_CLIMBING | F_FALLING)) == F_CAN_CLIMB); /* holding on, not climbing */
        CHECK(on.f[i].body == 0); /* the pose the original's catch leaves */
    }
    CHECK(tops == (unsigned)(FRAMES - 1 - top));
    printf("ok: with the fix he stops at Y=%d at the ladder's top, never falling, for %d updates of Up\n", TOP_Y,
           FRAMES - top);
}

/* A at the top jumps once Up is let go. In the original only right after a
 * drop, which cleared climbing; mid-climb, Player_IsClimbing refuses the jump. */
static void jumps_from_the_top(void) {
    Trace held = play(LADDER, false, 0, hold_up);
    int drop = 0;
    for (int i = FRAMES / 2; i < FRAMES && !drop; i++)
        if (held.f[i].flags & F_FALLING) drop = i;
    CHECK(drop > 0);
    release_at = drop + 1; /* caught, Up let go: climbing stays clear */
    Trace off = play(LADDER, false, 0, up_then_jump);
    CHECK(off.f[release_at + 1].flags & F_JUMPING);
    release_at = drop + 5; /* mid-climb: climbing set */
    off = play(LADDER, false, 0, up_then_jump);
    for (int i = release_at; i < FRAMES; i++) CHECK(!(off.f[i].flags & F_JUMPING));
    for (int at = 150; at < 170; at++) { /* with the fix: whenever he lets go */
        release_at = at;
        Trace on = play(LADDER, true, 0, up_then_jump);
        CHECK(on.f[at].y == TOP_Y && !(on.f[at].flags & F_JUMPING));
        CHECK((on.f[at + 1].flags & F_JUMPING) && on.f[at + 1].y == TOP_Y - 8); /* the press's own update */
    }
    printf("ok: A at the top jumps whenever Up is let go; the original jumped only right after a drop\n");
}

/* What the fix leaves alone: a block over the ladder, the Wing Boots, and
 * climbing down off its foot. */
static void leaves_alone(void) {
    Trace off = play(CEILING, false, 0, hold_up), on = play(CEILING, true, 0, hold_up);
    CHECK(same(&on, &off, 0, FRAMES) && drops(&on) == 0);
    CHECK(on.f[FRAMES - 1].y == 0x50); /* his head under the block */
    off = play(LADDER, false, S_WING_BOOTS, fly_up);
    on = play(LADDER, true, S_WING_BOOTS, fly_up);
    CHECK(same(&on, &off, 0, FRAMES));
    CHECK(on.f[99].y < TOP_Y - 8); /* he flies on past the top */
    off = play(LADDER, false, 0, hold_down);
    on = play(LADDER, true, 0, hold_down);
    CHECK(same(&on, &off, 0, FRAMES));
    CHECK(on.f[FRAMES - 1].y == 0xA0); /* off its foot and onto the floor */
    printf("ok: a block over the ladder, flying with the Wing Boots and climbing down are the game's\n");
}

/* Trunk $28's event runs on Trunk $28 only. */
static void the_fountain(void) {
    feature(true);
    events = 0;
    ram[M_AREA] = 1;
    ram[0x63] = 0x28;
    ram[0x42E] = 0x00;                          /* SCREEN_EVENT_PATH_TO_MASCON */
    CHECK(!site(0xEF69) && ram[0x42E] == 0x00); /* its own screen: the game's event */
    ram[0x63] = 0x27;                           /* walked off to $27 this pass */
    CHECK(site(0xEF69) && ram[0x42E] == 0xFF);  /* skipped, as its "not yet" path does */
    ram[0x42E] = 0x00;
    ram[M_AREA] = 2;
    ram[0x63] = 0x28;
    CHECK(site(0xEF69) && ram[0x42E] == 0xFF);
    CHECK(events == 2);
    feature(false);
    ram[M_AREA] = 1;
    ram[0x63] = 0x27;
    ram[0x42E] = 0x00;
    CHECK(!site(0xEF69) && ram[0x42E] == 0x00); /* off: the original's */
    printf("ok: the fountain's event runs on Trunk $28 and is skipped anywhere else\n");
}

static void off_means_off(void) {
    feature(false);
    Trace off = play(LADDER, false, 0, hold_up);
    reset_cb();
    for (unsigned i = 0; i < nsites; i++) CHECK(!sites[i].on);
    CHECK(!save_get && !save_set && !save_validate && !save_id); /* no save-state record: nothing kept */
    CHECK(drops(&off) >= 8);
    printf("ok: off, no hook is on; it keeps no save-state record\n");
}

int main(void) {
    the_original();
    stays_at_the_top();
    jumps_from_the_top();
    leaves_alone();
    the_fountain();
    off_means_off();
    printf("ladders_test: %u checks passed\n", checks);
    return 0;
}
