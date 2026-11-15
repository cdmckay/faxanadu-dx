/* Unit tests for the Magic button (fax_magic.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, the stack,
 * the NMI's pad read (Input_HandleOnInterrupt) and Game_MainLoop's readers of
 * the joypad masks, each testing the bits the disassembly tests, in the order
 * the main loop calls them. So the tests check what each part of the game
 * sees, rather than restate the module's constants. */
#include "fax_magic.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu = { .s = 0xFD };
static int hook_on[2] = { -1, -1 };              /* each hook's enabled state, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, "faxanadu.magic-button.cast") && addr == 0xBA5B && cb == cast_hook) ||
           (!strcmp(id, "faxanadu.magic-button.restore") && addr == 0xC478 && cb == restore_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < 2; ++i)
        if (!strcmp(id, hook_ids[i])) { hook_on[i] = on; return 1; }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.magic-button") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.magic-button") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    return !strcmp(id, "faxanadu.magic-button") && validate == state_validate;
}

enum { A = 0x80, B = 0x40, START = 0x10, UP = 0x08, DOWN = 0x04, LEFT = 0x02, RIGHT = 0x01 };
#define HELD    ram[0x16]                        /* Joy1_ButtonMask */
#define PREV    ram[0x18]                        /* Joy1_PrevButtonMask */
#define CHANGED ram[0x19]                        /* Joy1_ChangedButtonMask */

/* Input_HandleOnInterrupt, in the NMI that starts each frame. */
static void nmi(uint8_t pad) {
    PREV = HELD;
    HELD = pad;
    CHANGED = (uint8_t)((HELD ^ PREV) & HELD);
}

/* A JSR from `jsr` into a hooked routine: the hook runs at its entry with the
 * JSR's return address on the stack, and never handles the call. */
static void call(uint16_t jsr, int (*hook)(uint16_t), uint16_t entry) {
    uint16_t pushed = (uint16_t)(jsr + 2);
    ram[0x100 | cpu.s] = (uint8_t)(pushed >> 8); cpu.s = (uint8_t)(cpu.s - 1);
    ram[0x100 | cpu.s] = (uint8_t)pushed;        cpu.s = (uint8_t)(cpu.s - 1);
    CHECK(hook(entry) == 0);
    cpu.s = (uint8_t)(cpu.s + 2);
}

/* What the main loop's readers of the masks decide, each as its code tests. */
typedef struct { bool door, sword, cast, item, talk; } Seen;
static Seen seen;
static void update_player(void) {                /* GameLoop_UpdatePlayer */
    seen.sword = CHANGED & B;                    /* Player_CheckHandleAttack */
    seen.door = CHANGED & UP;                    /* Player_CheckHandleEnterDoor, standing at a door */
}
static void cast_magic(void) {                   /* JSR Player_CastMagic at $DB5F */
    call(0xDB5F, cast_hook, PLAYER_CAST_MAGIC);
    seen.cast = (HELD & UP) && (CHANGED & B);
}
static void use_item_and_on(void) {              /* JSR GameLoop_CheckUseCurrentItem at $DB62 */
    call(0xDB62, restore_hook, GAME_LOOP_CHECK_USE_CURRENT_ITEM);
    seen.item = (HELD & DOWN) && (CHANGED & B);
    seen.talk = CHANGED == UP;                   /* Player_CheckHandlePressUpOnNPC, in Sprites_UpdateAll */
}
/* One frame of Game_MainLoop with the pad as given. */
static Seen frame(uint8_t pad) {
    memset(&seen, 0, sizeof seen);
    nmi(pad);
    update_player();
    cast_magic();
    use_item_and_on();
    fax_magic_frame_end();
    return seen;
}
static bool nothing(Seen s) { return !s.door && !s.sword && !s.cast && !s.item && !s.talk; }
static void fresh(void) {
    deactivate(); activate();
    memset(ram, 0, sizeof ram);
    cpu.s = 0xFD;
}

static void magic_casts_alone(void) {
    fresh();
    CHECK(hook_on[0] == 1 && hook_on[1] == 1);
    CHECK(nothing(frame(0)));
    press();
    Seen s = frame(0);
    /* Player_CastMagic, and nothing else: no door, no sword, no item, no NPC. */
    CHECK(s.cast && !s.door && !s.sword && !s.item && !s.talk);
    CHECK(HELD == 0 && CHANGED == 0 && PREV == 0);
    CHECK(presses == 1 && presented == 1 && restored == 1 && reread == 0 && !swap.injected);
    CHECK(nothing(frame(0)));                     /* one press, one cast */
}

static void the_player_buttons_pass_through(void) {
    fresh();
    /* Walking right, and A newly pressed, in the frame Magic goes down. */
    press();
    Seen s = frame(RIGHT | A);
    CHECK(s.cast && !s.sword && !s.door);
    CHECK(HELD == (RIGHT | A) && CHANGED == (RIGHT | A));
    /* B pressed the next frame is new to the next read: the masks the NMI
     * compares with are the real ones, not Up+B. */
    s = frame(RIGHT | B);
    CHECK(s.sword && !s.cast && CHANGED == B);
    /* Up the frame after that is new too, and talks to an NPC. */
    s = frame(UP);
    CHECK(s.talk && s.door && !s.cast);
    /* Holding Down: Down+B uses the item, so the item check must not see our B. */
    press();
    s = frame(DOWN);
    CHECK(s.cast && !s.item && HELD == DOWN && CHANGED == DOWN);
}

static void up_b_still_casts(void) {
    fresh();
    Seen s = frame(UP);
    CHECK(s.door && s.talk && !s.cast);          /* Up alone: a door or an NPC, as ever */
    s = frame(UP | B);
    CHECK(s.cast && s.sword && !s.door);         /* the stock combo swings and casts */
    CHECK(presented == 0 && restored == 0);
}

static void magic_with_up_b_held(void) {
    /* Both at once: the masks already hold what we would show. */
    fresh();
    frame(UP);
    press();
    Seen s = frame(UP | B);
    CHECK(s.cast && HELD == (UP | B) && CHANGED == B);
    CHECK(presented == 1 && restored == 1);
}

static void a_press_lasts_one_frame(void) {
    fresh();
    /* Magic goes down while a conversation holds the main loop: the frame
     * ends without Player_CastMagic, and the press is gone with it. */
    press();
    nmi(0);
    fax_magic_frame_end();
    CHECK(nothing(frame(0)) && presented == 0);
    /* Two presses before one frame are one press. */
    press(); press();
    CHECK(frame(0).cast && presses == 3 && presented == 1);
    CHECK(!frame(0).cast);
}

static void not_in_the_ending(void) {
    /* EndGame_MainLoop plays its own buttons: a press there shows nothing. */
    fresh();
    press();
    nmi(0);
    call(0xDC0B, cast_hook, PLAYER_CAST_MAGIC);
    CHECK(!swap.injected && HELD == 0 && CHANGED == 0);
    call(0xDC0E, restore_hook, GAME_LOOP_CHECK_USE_CURRENT_ITEM);
    fax_magic_frame_end();
    CHECK(presented == 0 && restored == 0);
    /* The stack wraps within page 1, as the 6502's does. */
    fresh();
    cpu.s = 0x00;
    press();
    CHECK(frame(0).cast && cpu.s == 0x00);
}

static void a_pad_read_in_between(void) {
    /* A lag frame: the NMI reads the pad between the two hooks, comparing it
     * with our Up+B. B, pressed by then, must still count as new, and Up and
     * B must not stay held. */
    fresh();
    nmi(RIGHT);
    press();
    update_player();
    cast_magic();
    CHECK(seen.cast && swap.injected);
    nmi(RIGHT | B);
    CHECK(CHANGED == 0);                         /* against ours, the read lost the new B */
    use_item_and_on();
    CHECK(HELD == (RIGHT | B) && CHANGED == B && PREV == RIGHT);
    CHECK(reread == 1 && restored == 1 && !swap.injected);
    nmi(RIGHT | B);
    CHECK(CHANGED == 0);                         /* held, not new again */
    /* Two reads in between: the last one compared real with real. */
    fresh();
    press();
    nmi(0);
    cast_magic();
    nmi(LEFT);
    nmi(LEFT | B);
    use_item_and_on();
    CHECK(HELD == (LEFT | B) && CHANGED == B && PREV == LEFT && reread == 0 && restored == 1);
}

static void a_save_state_in_between(void) {
    fresh();
    nmi(DOWN);
    press();
    cast_magic();
    uint8_t record[16];
    int n = state_get(record, (int)sizeof record);
    CHECK(n == 3 && state_validate(record, n));
    CHECK(state_get(record, 2) == -1);
    uint8_t machine[0x800];
    memcpy(machine, ram, sizeof ram);
    /* Loaded later, into a game that has moved on. */
    fresh();
    memcpy(ram, machine, sizeof ram);
    CHECK(state_set(record, n) && swap.injected);
    use_item_and_on();
    CHECK(HELD == DOWN && CHANGED == DOWN && !seen.item);   /* Down+B would have used the item */
    CHECK(state_get(record, (int)sizeof record) == 0);       /* no record between swaps */
    /* No record, or a bad one. */
    CHECK(state_set(NULL, 0) && !swap.injected);
    uint8_t bad[3] = { 0, 1, 2 };
    CHECK(!state_validate(bad, 3) && !state_validate(record, 2) && !state_set(bad, 3));
    /* Loaded with the feature off: no hook will restore, so it does at once. */
    fresh();
    memcpy(ram, machine, sizeof ram);
    deactivate();
    CHECK(state_set(record, n) && !swap.injected && HELD == DOWN && CHANGED == DOWN);
}

static void off_means_off(void) {
    fresh();
    press();
    frame(0);
    deactivate();
    CHECK(hook_on[0] == 0 && hook_on[1] == 0 && !enabled && presses == 0 && presented == 0);
    press();
    CHECK(!pressed && presses == 0);
    /* Turned off between the two hooks: the real masks come back at once. */
    fresh();
    nmi(LEFT);
    press();
    cast_magic();
    deactivate();
    CHECK(HELD == LEFT && CHANGED == LEFT && !swap.injected);
}

int main(void) {
    magic_casts_alone();
    the_player_buttons_pass_through();
    up_b_still_casts();
    magic_with_up_b_held();
    a_press_lasts_one_frame();
    not_in_the_ending();
    a_pad_read_in_between();
    a_save_state_in_between();
    off_means_off();
    printf("magic_test: %u checks passed\n", checks);
    return 0;
}
