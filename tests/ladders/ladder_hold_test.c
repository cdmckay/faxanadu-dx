/* Unit tests for holding on to ladders (fax_ladder_hold.c), against
 * tests/movement/model.h: the hero's update as bank 15 has it, with the hook
 * sites where the ROM has them. Each case takes the same hit with the feature
 * on and off (Player_SetDamagedBySprite's state: invincibility $3C and
 * knockback), and compares what the hero does. */
#include "fax_ladder_hold.c"
#include "mod_savestate.h"
#include "../movement/model.h"

enum { FRAMES = 40 };
typedef struct { Frame f[FRAMES]; } Trace;

/* A ladder hanging in the air, rows 3-7, its foot two blocks above the floor. */
static const char *const HANGING[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "................", "................",
    "................", "................", "################",
};
/* A ladder down to the floor. */
static const char *const TO_THE_FLOOR[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "........H.......", "........H.......",
    "........H.......", "........H.......", "################",
};
static const char *const FLOOR[13] = {
    "................", "................", "................", "................", "................",
    "................", "................", "................", "................", "................",
    "................", "................", "################",
};
enum { LADDER_X = 0x80, FLOOR_Y = 0xA0 };

static void feature(bool on) {
    reset_cb();
    if (on) activate_cb();
}

/* At (x, y) with `flags`, hit (facing the enemy, as the hit leaves him), then
 * `buttons` from frame `from`. */
static Trace hit(const char *const *rows, bool on, uint8_t x, uint8_t y, uint8_t flags, uint8_t buttons, int from) {
    load_screen(rows);
    place(x, y);
    ram[M_FLAGS] = flags;
    ram[M_JC] = flags & F_JUMPING ? 4 : 0x20;
    ram[M_STATUS] = S_KNOCKBACK;
    ram[M_IFRAMES] = 0x3C;
    feature(on);
    Trace t;
    for (int i = 0; i < FRAMES; i++) t.f[i] = step(i >= from ? buttons : 0);
    return t;
}
static bool same(const Trace *a, const Trace *b) { return !memcmp(a, b, sizeof *a); }

/* The original: knocked 8 pixels an update for 3 updates, every flag but his
 * facing dropped, off the ladder and down to the floor. */
static void the_original(void) {
    Trace off = hit(HANGING, false, LADDER_X, 0x50, F_CAN_CLIMB | F_CLIMBING | F_RIGHT, 0, FRAMES);
    CHECK(off.f[0].x == LADDER_X - 8 && off.f[1].x == LADDER_X - 16 && off.f[2].x == LADDER_X - 24);
    CHECK(!(off.f[1].flags & (F_CAN_CLIMB | F_CLIMBING)));
    CHECK(off.f[FRAMES - 1].y == FLOOR_Y);
    printf("ok: the original knocks him %d pixels off the ladder, and he falls to the floor\n",
           LADDER_X - off.f[2].x);
}

/* With the fix, on a ladder in the air he keeps his place and his hold, and
 * climbs on once the knockback is over (Up and Down climb during it too). */
static void holds_on(void) {
    static const uint8_t holds[] = {F_CAN_CLIMB | F_CLIMBING | F_RIGHT, F_CAN_CLIMB | F_CLIMBING,
                                    F_CAN_CLIMB | F_RIGHT, F_CAN_CLIMB};   /* climbing, or hanging after a jump */
    for (unsigned k = 0; k < sizeof holds; k++) {
        held = 0;
        Trace on = hit(HANGING, true, LADDER_X, 0x50, holds[k], 0, FRAMES);
        for (int i = 0; i < FRAMES; i++) {
            CHECK(on.f[i].x == LADDER_X && on.f[i].y == 0x50);
            CHECK(on.f[i].flags == holds[k]);
        }
        CHECK(held == 3);                                       /* the three updates of knockback */
        on = hit(HANGING, true, LADDER_X, 0x50, holds[k], B_UP, 3);
        CHECK(on.f[FRAMES - 1].y < 0x50 && (on.f[FRAMES - 1].flags & F_CLIMBING));
    }
    Trace on = hit(HANGING, true, LADDER_X, 0x50, F_CAN_CLIMB | F_CLIMBING, B_DOWN, 0);
    CHECK(on.f[2].x == LADDER_X && on.f[2].y > 0x50);           /* Down climbs during the knockback */
    printf("ok: with the fix a hit on a ladder in the air leaves him where he was, holding on\n");
}

/* What the fix leaves alone: on the ground (at a ladder's foot or not), and
 * in a jump past a ladder. */
static void leaves_alone(void) {
    held = 0;
    Trace off = hit(TO_THE_FLOOR, false, LADDER_X, FLOOR_Y, F_CAN_CLIMB | F_CLIMBING | F_RIGHT, 0, FRAMES);
    Trace on = hit(TO_THE_FLOOR, true, LADDER_X, FLOOR_Y, F_CAN_CLIMB | F_CLIMBING | F_RIGHT, 0, FRAMES);
    CHECK(same(&on, &off) && off.f[2].x == LADDER_X - 24);
    off = hit(FLOOR, false, LADDER_X, FLOOR_Y, F_RIGHT, 0, FRAMES);
    on = hit(FLOOR, true, LADDER_X, FLOOR_Y, F_RIGHT, 0, FRAMES);
    CHECK(same(&on, &off));
    CHECK(held == 0);
    /* In a jump past a ladder the first update of knockback is the game's,
     * which ends the jump. Still in front of the ladder after it (8 pixels
     * left, X & 15 = 8: Player_CheckIfOnLadder looks one block right too), he
     * holds on for the other two. */
    off = hit(HANGING, false, LADDER_X, 0x50, F_CAN_CLIMB | F_JUMPING | F_RIGHT, 0, FRAMES);
    on = hit(HANGING, true, LADDER_X, 0x50, F_CAN_CLIMB | F_JUMPING | F_RIGHT, 0, FRAMES);
    CHECK(!memcmp(&on.f[0], &off.f[0], sizeof on.f[0]) && on.f[0].x == LADDER_X - 8 && !(on.f[0].flags & F_JUMPING));
    CHECK(off.f[2].x == LADDER_X - 24 && off.f[FRAMES - 1].y == FLOOR_Y);
    for (int i = 1; i < FRAMES; i++) CHECK(on.f[i].x == on.f[0].x && on.f[i].y == on.f[0].y && (on.f[i].flags & F_CAN_CLIMB));
    CHECK(held == 2);
    printf("ok: on the ground, at a ladder's foot or not, knockback is the game's; in a jump past a ladder, its "
           "first update is, and ends the jump\n");
}

static void off_means_off(void) {
    feature(true);
    feature(false);
    for (unsigned i = 0; i < nsites; i++) CHECK(!sites[i].on);
    CHECK(!save_get && !save_set && !save_validate && !save_id);   /* no save-state record: nothing kept */
    Trace off = hit(HANGING, false, LADDER_X, 0x50, F_CAN_CLIMB | F_CLIMBING, 0, FRAMES);
    CHECK(off.f[FRAMES - 1].y == FLOOR_Y);
    printf("ok: off, no hook is on; it keeps no save-state record\n");
}

int main(void) {
    the_original();
    holds_on();
    leaves_alone();
    off_means_off();
    printf("ladder_hold_test: %u checks passed\n", checks);
    return 0;
}
