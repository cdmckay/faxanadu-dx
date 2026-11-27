/* Unit tests for Run (fax_run.c), against tests/movement/model.h: the hero's
 * update as bank 15 has it, with Run's hook sites (the LDX before
 * Player_CheckIfPassable in Player_TryMoveRight and Player_TryMoveLeft) and
 * the stack the JSRs on the way push. Each case plays the same input with
 * Run held and not, and compares what the hero does. Run's button reaches it
 * as the window's SDL events do. */
#define CYC_WITH_SDL 1
#include "fax_run.c"
#include "mod_savestate.h"
#include "../movement/model.h"

void cyc_tcp_register(const char *name, const char *help, void (*fn)(int, const char *)) {
    (void)name;
    (void)help;
    (void)fn;
}
void cyc_tcp_ok(int id, const char *fields) {
    (void)id;
    (void)fields;
}
void cyc_tcp_quote(const char *in, char *out, size_t n) { snprintf(out, n, "\"%s\"", in); }

static const char *const FLOOR[13] = {
    "................", "................", "................", "................", "................",
    "................", "................", "................", "................", "................",
    "................", "................", "################",
};
/* A ledge, columns 0-7 of row 8, over the floor. */
static const char *const LEDGE[13] = {
    "................", "................", "................", "................", "................",
    "................", "................", "................", "########........", "................",
    "................", "................", "################",
};
/* A ladder down to the floor, column 8. */
static const char *const TO_THE_FLOOR[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "........H.......", "........H.......",
    "........H.......", "........H.......", "################",
};
/* A ladder hanging in the air, rows 3-7. */
static const char *const HANGING[13] = {
    "................", "................", "................", "........H.......", "........H.......",
    "........H.......", "........H.......", "........H.......", "................", "................",
    "................", "................", "################",
};
enum { FLOOR_Y = 0xA0, LEDGE_Y = 0x60, LADDER_X = 0x80, FRAMES = 128 };

/* Run's button, as the window sends it: the left trigger, all the way or let go. */
static void trigger(bool down) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    e.caxis.value = down ? 32767 : 0;
    fax_run_event(&e);
}
static void feature(bool on) {
    reset_cb();
    if (on) activate_cb();
}
static unsigned pos(void) { return (unsigned)ram[M_X] << 8 | ram[M_X_FRAC]; }
static unsigned accel_now(void) { return (unsigned)ram[M_ACCEL_U] << 8 | ram[M_ACCEL]; }

/* How the hero starts, besides where. */
static struct {
    uint8_t flags, status, title, iframes;
    unsigned accel; /* 0: the standard */
} setup;
static void fresh_setup(void) {
    memset(&setup, 0, sizeof setup);
    setup.flags = F_RIGHT;
}

typedef struct {
    uint8_t buttons;
    bool run;
} Input;
typedef struct {
    Frame f[FRAMES];
    unsigned x[FRAMES], accel[FRAMES]; /* his place (8.8) and Player_MoveAcceleration after each frame */
} Trace;
/* From (x, y) on `rows`, `first` for frames before `split`, `then` after. */
static Trace play(const char *const *rows, uint8_t x, uint8_t y, int frames, Input first, int split, Input then) {
    CHECK(frames <= FRAMES);
    load_screen(rows);
    place(x, y);
    ram[M_FLAGS] = setup.flags;
    ram[M_STATUS] = setup.status;
    ram[M_TITLE] = setup.title;
    ram[M_IFRAMES] = setup.iframes;
    if (setup.accel) {
        ram[M_ACCEL] = (uint8_t)setup.accel;
        ram[M_ACCEL_U] = (uint8_t)(setup.accel >> 8);
    }
    Trace t;
    memset(&t, 0, sizeof t);
    for (int i = 0; i < frames; i++) {
        Input in = i < split ? first : then;
        trigger(in.run);
        t.f[i] = step(in.buttons);
        t.x[i] = pos();
        t.accel[i] = accel_now();
    }
    trigger(false);
    return t;
}
static Trace hold(const char *const *rows, uint8_t x, uint8_t y, int frames, uint8_t buttons, bool run) {
    return play(rows, x, y, frames, (Input){ buttons, run }, frames, (Input){ 0, false });
}
static bool same(const Trace *a, const Trace *b, int from, int to) {
    for (int i = from; i < to; i++)
        if (memcmp(&a->f[i], &b->f[i], sizeof a->f[i]) || a->x[i] != b->x[i] || a->accel[i] != b->accel[i])
            return false;
    return true;
}
static unsigned moved(const Trace *t, int i, unsigned start) {
    unsigned before = i ? t->x[i - 1] : start;
    return t->x[i] > before ? t->x[i] - before : before - t->x[i];
}

/* From a standstill he speeds up as the game has him, a run 1.5 times each
 * step of the walk; the acceleration itself is the game's. */
static void from_a_standstill(void) {
    fresh_setup();
    feature(true);
    Trace walk = hold(FLOOR, 0x20, FLOOR_Y, 40, B_RIGHT, false);
    CHECK(runs == 0 && walks == 0);
    Trace run = hold(FLOOR, 0x20, FLOOR_Y, 40, B_RIGHT, true);
    CHECK(walk.x[39] - 0x2000 == 40 * 0xC0 + 2 * (40 * 41 / 2)); /* $C2, $C4, ... 256ths */
    CHECK(run.x[39] - 0x2000 == (walk.x[39] - 0x2000) * 3 / 2);
    for (int i = 0; i < 40; i++) {
        CHECK(moved(&run, i, 0x2000) == moved(&walk, i, 0x2000) * 3 / 2);
        CHECK(run.accel[i] == walk.accel[i] && moved(&walk, i, 0x2000) == walk.accel[i]);
        CHECK(run.f[i].flags == walk.f[i].flags && run.f[i].y == FLOOR_Y);
    }
    CHECK(runs == 40 && walks == 0 && last_step == 0x110 * 3 / 2);
    unsigned walked = walk.x[39] - 0x2000, ran = run.x[39] - 0x2000;
    /* Left, the same. */
    setup.flags = 0;
    walk = hold(FLOOR, 0xE0, FLOOR_Y, 40, B_LEFT, false);
    run = hold(FLOOR, 0xE0, FLOOR_Y, 40, B_LEFT, true);
    CHECK(0xE000 - run.x[39] == (0xE000 - walk.x[39]) * 3 / 2 && 0xE000 - walk.x[39] == 40 * 0xC0 + 40 * 41);
    /* A Champion speeds up by 8 an update, to the top in 24. */
    fresh_setup();
    setup.title = 12;
    walk = hold(FLOOR, 0x10, FLOOR_Y, 40, B_RIGHT, false);
    run = hold(FLOOR, 0x10, FLOOR_Y, 40, B_RIGHT, true);
    CHECK(run.x[39] - 0x1000 == (walk.x[39] - 0x1000) * 3 / 2 && walk.accel[23] == 0x180);
    printf("ok: from a standstill a run is 1.5 times each step of the walk, %u/256 pixels to the walk's %u/256 in "
           "40 frames, either way and at any title; the acceleration is the game's\n",
           ran, walked);
}

/* At the top: 2.25 pixels a frame to the walk's 1.5. */
static void top_speed(void) {
    fresh_setup();
    feature(true);
    Trace walk = hold(FLOOR, 0x08, FLOOR_Y, 120, B_RIGHT, false);
    Trace run = hold(FLOOR, 0x08, FLOOR_Y, 120, B_RIGHT, true);
    CHECK(walk.accel[95] == 0x180 && walk.accel[119] == 0x180 && run.accel[119] == 0x180);
    for (int i = 96; i < 120; i++) CHECK(moved(&walk, i, 0) == 0x180 && moved(&run, i, 0) == 0x240);
    CHECK(last_step == 0x240);
    printf("ok: at the top a run steps $240 (2.25 pixels) a frame to the walk's $180 (1.5)\n");
}

/* A jump keeps the walk's speed: from the same run-up, with Run held or let
 * go at the jump, every frame of it is the same, each step the acceleration. */
static void jumps(void) {
    fresh_setup();
    feature(true);
    Trace held_on = play(FLOOR, 0x10, FLOOR_Y, 60, (Input){ B_RIGHT, true }, 20, (Input){ B_A, true });
    unsigned air = walks, ran = runs;
    feature(true);
    Trace let_go = play(FLOOR, 0x10, FLOOR_Y, 60, (Input){ B_RIGHT, true }, 20, (Input){ B_A, false });
    CHECK(same(&held_on, &let_go, 0, 60));
    int jumping = 0, landed = 0;
    for (int i = 20; i < 60; i++) {
        if (!(held_on.f[i].flags & F_JUMPING)) {
            landed = landed ? landed : i;
            continue;
        }
        ++jumping;
        CHECK(moved(&held_on, i, 0) == held_on.accel[i]); /* the walk's step in the air */
    }
    CHECK(jumping == 31 && landed == 51 && held_on.f[landed].y == FLOOR_Y);
    CHECK(air == 31 && ran == 20); /* in the air Run held keeps the game's step */
    printf("ok: a running jump lands where a walking one does: every frame alike with Run held or let go, at "
           "the walk's step\n");
}

/* Running off a ledge, the fall keeps the walk's step; on the floor again, the run's. */
static void off_a_ledge(void) {
    fresh_setup();
    feature(true);
    Trace t = hold(LEDGE, 0x68, LEDGE_Y, 30, B_RIGHT, true);
    int falls = 0;
    for (int i = 0; i < 30; i++) {
        unsigned step_was = moved(&t, i, 0x6800);
        if (t.f[i].flags & F_FALLING) {
            ++falls;
            CHECK(step_was == t.accel[i]);
        } else {
            CHECK(step_was == t.accel[i] * 3 / 2);
        }
    }
    CHECK(falls >= 5 && t.f[29].y == FLOOR_Y);
    printf("ok: off a ledge the fall is at the walk's step (%d frames), and the run is back on the floor\n", falls);
}

/* Ladders: walking past one on the floor is no ladder; climbing, lining up
 * with one and hanging on one are. */
static void ladders(void) {
    fresh_setup();
    feature(true);
    Trace past = hold(TO_THE_FLOOR, 0x60, FLOOR_Y, 40, B_RIGHT, true);
    Trace open = hold(FLOOR, 0x60, FLOOR_Y, 40, B_RIGHT, true);
    bool in_front = false;
    for (int i = 0; i < 40; i++) {
        in_front |= (past.f[i].flags & F_CAN_CLIMB) != 0;
        CHECK(past.x[i] == open.x[i]);
    }
    CHECK(in_front && runs == 80 && walks == 0);

    /* Hanging on a ladder in the air, stepping off it. */
    setup.flags = F_CAN_CLIMB | F_RIGHT;
    feature(true);
    /* Right until he has stepped off (12 updates), then nothing: he lands, and stops. */
    Trace hang_run = play(HANGING, LADDER_X, 0x50, 30, (Input){ B_RIGHT, true }, 16, (Input){ 0, true });
    CHECK(runs == 0 && walks > 0);
    Trace hang_walk = play(HANGING, LADDER_X, 0x50, 30, (Input){ B_RIGHT, false }, 16, (Input){ 0, false });
    CHECK(same(&hang_run, &hang_walk, 0, 30) && hang_run.f[29].y == FLOOR_Y);

    /* Climbing (at a ladder's foot, on the floor): the walk's step until he is off it. */
    setup.flags = F_CAN_CLIMB | F_CLIMBING | F_RIGHT;
    feature(true);
    Trace off_foot = hold(TO_THE_FLOOR, LADDER_X, FLOOR_Y, 30, B_RIGHT, true);
    CHECK(moved(&off_foot, 0, LADDER_X << 8) == off_foot.accel[0]);
    CHECK(moved(&off_foot, 29, 0) == off_foot.accel[29] * 3 / 2 && !(off_foot.f[29].flags & F_CLIMBING));

    /* Up in front of a ladder lines him up with it, as the game does. */
    fresh_setup();
    feature(true);
    Trace line_run = hold(TO_THE_FLOOR, LADDER_X + 4, FLOOR_Y, 12, B_UP, true);
    Trace line_walk = hold(TO_THE_FLOOR, LADDER_X + 4, FLOOR_Y, 12, B_UP, false);
    CHECK(same(&line_run, &line_walk, 0, 12) && (line_run.f[11].flags & F_CLIMBING) && runs == 0);
    printf("ok: past a ladder on the floor he runs on; climbing, lining up with a ladder and hanging on one are "
           "the walk's\n");
}

/* Wing Boots flight, knockback and the ending's walk are the game's. */
static void not_on_foot(void) {
    fresh_setup();
    setup.status = S_WING_BOOTS;
    feature(true);
    Trace fly_run = hold(FLOOR, 0x20, FLOOR_Y, 30, B_A | B_RIGHT, true);
    Trace fly_walk = hold(FLOOR, 0x20, FLOOR_Y, 30, B_A | B_RIGHT, false);
    CHECK(same(&fly_run, &fly_walk, 0, 30) && fly_run.f[29].y < FLOOR_Y && runs == 0 && walks == 30);

    /* A hit while running: knockback's three pushes of 8, then the run again. */
    fresh_setup();
    setup.flags = F_RIGHT | F_MOVING;
    setup.status = S_KNOCKBACK;
    setup.iframes = 0x3C;
    feature(true);
    Trace hit_run = hold(FLOOR, LADDER_X, FLOOR_Y, 10, B_RIGHT, true);
    Trace hit_walk = hold(FLOOR, LADDER_X, FLOOR_Y, 10, B_RIGHT, false);
    CHECK(same(&hit_run, &hit_walk, 0, 3) && hit_run.x[2] == (LADDER_X - 24) << 8);
    CHECK(moved(&hit_run, 9, 0) == moved(&hit_walk, 9, 0) * 3 / 2);

    /* EndGame_MainLoop's walk to the King. */
    fresh_setup();
    update_caller = 0xDC01;
    feature(true);
    Trace end_run = hold(FLOOR, 0x20, FLOOR_Y, 30, B_RIGHT, true);
    Trace end_walk = hold(FLOOR, 0x20, FLOOR_Y, 30, B_RIGHT, false);
    CHECK(same(&end_run, &end_walk, 0, 30) && runs == 0 && walks == 0);
    update_caller = 0xDB55;
    printf("ok: in Wing Boots flight, knockback and the ending's walk Run does nothing\n");
}

/* The step past a screen's side, straight at the hook: right, it wraps as
 * the routine's ADC does; left, past X 0 the pushed carry clears. */
static void the_sides(void) {
    fresh_setup();
    feature(true);
    trigger(true);
    struct {
        bool left;
        uint8_t x, frac, p, want_x, want_frac, want_p;
    } cases[] = {
        { false, 0xFF, 0x80, 0, 0x00, 0x40, 0 },          /* $FF80 + $C0 */
        { true, 0x01, 0x00, FLAG_C, 0x00, 0x40, FLAG_C }, /* $0100 - $C0: still not past */
        { true, 0x00, 0x80, FLAG_C, 0x00, 0xC0, 0 },      /* $0080 - $C0: now past */
        { true, 0x00, 0x20, 0, 0x00, 0x60, 0 },           /* already past: X stays 0 */
        { true, 0x40, 0x00, FLAG_C, 0x3F, 0x40, FLAG_C }, /* in the room */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        place(cases[i].x, FLOOR_Y);
        ram[M_X_FRAC] = cases[i].frac;
        ram[M_ACCEL] = 0x80;
        ram[M_ACCEL_U] = 0x01;
        ram[M_FLAGS] = F_MOVING;
        cpu.s = 0xF0;
        push_return(0xDB55);
        push_return(0xE0D2);
        if (cases[i].left) push((uint8_t)(0x24 | cases[i].p));
        CHECK(move_hook(cases[i].left ? 0xE246 : 0xE1EE) == 0);
        CHECK(ram[M_X] == cases[i].want_x && ram[M_X_FRAC] == cases[i].want_frac);
        if (cases[i].left) CHECK((ram[0x100 | (uint8_t)(cpu.s + 1)] & FLAG_C) == cases[i].want_p);
    }
    /* Knockback's push (Player_KnockbackHoriz's JSR on top) is left alone. */
    place(0x40, FLOOR_Y);
    cpu.s = 0xF0;
    push_return(0xDB55);
    push_return(0xE0D2);
    push_return(0xE15E);
    move_hook(0xE1EE);
    CHECK(pos() == 0x4000);
    trigger(false);
    printf("ok: past the right side the step wraps as the game's does; past X 0 going left the pushed carry "
           "clears, so the routine goes on to the screen's side\n");
}

static void off_and_isolated(void) {
    fresh_setup();
    feature(true);
    feature(false);
    for (unsigned i = 0; i < nsites; i++) CHECK(!sites[i].on);
    CHECK(!save_get && !save_set && !save_validate && !save_id); /* no save-state record */
    Trace run = hold(FLOOR, 0x20, FLOOR_Y, 30, B_RIGHT, true);
    Trace walk = hold(FLOOR, 0x20, FLOOR_Y, 30, B_RIGHT, false);
    CHECK(same(&run, &walk, 0, 30));
    /* In a scratch copy of the machine (Seamless's rooms), nothing. */
    feature(true);
    trigger(true);
    place(0x40, FLOOR_Y);
    ram[M_ACCEL] = 0x80;
    ram[M_ACCEL_U] = 0x01;
    cpu.s = 0xF0;
    push_return(0xDB55);
    push_return(0xE0D2);
    isolated = true;
    move_hook(0xE1EE);
    isolated = false;
    CHECK(pos() == 0x4000);
    trigger(false);
    printf("ok: off, no hook is on and he walks; isolated, the hook does nothing; it keeps no record\n");
}

/* The button: Left Shift or the left trigger past half, rebound from config.ini. */
static void the_button(void) {
    const CycHostBinding *b = fax_run_binding();
    CHECK(!strcmp(b->label, "Run (hold)") && b->default_key == SDL_SCANCODE_LSHIFT &&
          b->default_pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));
    CHECK(*b->key == b->default_key && *b->pad == b->default_pad);
    CHECK(!strcmp(fax_run_controls_label(), "Run: LEFT SHIFT / LT"));
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_LSHIFT;
    fax_run_event(&e);
    CHECK(held());
    e.type = SDL_KEYUP;
    fax_run_event(&e);
    CHECK(!held());
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    e.caxis.value = 16383;
    fax_run_event(&e);
    CHECK(!held());
    e.caxis.value = 16384;
    fax_run_event(&e);
    CHECK(held());
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT; /* Block's trigger lets go: still held */
    e.caxis.value = 0;
    fax_run_event(&e);
    CHECK(held());
    e.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    fax_run_event(&e);
    CHECK(!held());

    CHECK(fax_run_load_setting("RunKey", "G") && fax_run_load_setting("RunButton", "leftstick"));
    CHECK(*b->key == SDL_SCANCODE_G && *b->pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(!strcmp(fax_run_controls_label(), "Run: G / L3"));
    e.caxis.value = 32767;
    fax_run_event(&e);
    CHECK(!held()); /* the trigger is no longer Run's */
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    fax_run_event(&e);
    CHECK(held());
    e.type = SDL_CONTROLLERBUTTONUP;
    fax_run_event(&e);
    CHECK(!held());
    /* What it cannot read keeps what it had. */
    CHECK(fax_run_load_setting("RunKey", "No Such Key") && fax_run_load_setting("RunButton", "a+b"));
    CHECK(*b->key == SDL_SCANCODE_G && *b->pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(!fax_run_load_setting("BlockKey", "V"));
    char text[128] = "";
    FILE *f = tmpfile();
    CHECK(f);
    fax_run_save_settings(f);
    rewind(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(!strcmp(text, "RunKey = G\nRunButton = leftstick\n"));
    CHECK(fax_run_load_setting("RunButton", "none") && *b->pad == 0);
    CHECK(!strcmp(fax_run_controls_label(), "Run: G"));
    *b->key = b->default_key;
    *b->pad = b->default_pad;
    b->changed();
    CHECK(!strcmp(fax_run_controls_label(), "Run: LEFT SHIFT / LT"));
    printf("ok: Left Shift and the left trigger (past half) hold it; RunKey and RunButton rebind it, and are "
           "written back\n");
}

int main(void) {
    from_a_standstill();
    top_speed();
    jumps();
    off_a_ledge();
    ladders();
    not_on_foot();
    the_sides();
    off_and_isolated();
    the_button();
    printf("run_test: %u checks passed\n", checks);
    return 0;
}
