/* Unit tests for the Use item button (fax_use_item.c), without the emulator
 * or a ROM. The module runs against a model of the machine: CPU RAM, the
 * stack, the registers, the NMI's pad read (Input_HandleOnInterrupt), and
 * GameLoop_CheckUseCurrentItem's code as the disassembly has it: each load,
 * then the hook at the AND after it (which may change A), then the AND and
 * its branch. Around it, the other readers of the pad in Game_MainLoop's
 * order, so the tests check what each part of the game sees. */
#include "fax_use_item.c"

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #test);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu = { .s = 0xFD };
static int hook_on[4] = { -1, -1, -1, -1 }; /* each hook's enabled state, as last set */

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return false; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, "faxanadu.use-item.air") && addr == 0xC47A && cb == air_hook) ||
           (!strcmp(id, "faxanadu.use-item.down") && addr == 0xC480 && cb == down_hook) ||
           (!strcmp(id, "faxanadu.use-item.b") && addr == 0xC487 && cb == b_hook) ||
           (!strcmp(id, "faxanadu.use-item.back") && addr == 0xDB5F && cb == back_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < 4; ++i)
        if (!strcmp(id, hook_ids[i])) {
            hook_on[i] = on;
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.use-item") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}

enum { A = 0x80, B = 0x40, UP = 0x08, DOWN = 0x04, LEFT = 0x02, RIGHT = 0x01 };
enum {
    JUMPING = 0x01,
    FALLING = 0x04,
    CLIMBING = 0x10,
    LADDER = 0x08,
    WINGS = 0x80
}; /* Player_Flags; Player_StatusFlag */
#define HELD    ram[0x16] /* Joy1_ButtonMask */
#define PREV    ram[0x18] /* Joy1_PrevButtonMask */
#define CHANGED ram[0x19] /* Joy1_ChangedButtonMask */
#define FLAGS   ram[0xA4] /* Player_Flags */
#define STATUS  ram[0xA5] /* Player_StatusFlag */

/* Input_HandleOnInterrupt, in the NMI that starts each frame. */
static void nmi(uint8_t pad) {
    PREV = HELD;
    HELD = pad;
    CHANGED = (uint8_t)((HELD ^ PREV) & HELD);
}

/* GameLoop_CheckUseCurrentItem, entered by the JSR at `jsr` (Game_MainLoop's
 * $DB62, EndGame_MainLoop's $DC0E): true when it reaches the item's routine. */
static bool check_use_current_item(uint16_t jsr) {
    uint16_t pushed = (uint16_t)(jsr + 2);
    ram[0x100 | cpu.s] = (uint8_t)(pushed >> 8);
    cpu.s = (uint8_t)(cpu.s - 1);
    ram[0x100 | cpu.s] = (uint8_t)pushed;
    cpu.s = (uint8_t)(cpu.s - 1);
    bool used = false;
    cpu.a = FLAGS; /* LDA Player_Flags */
    CHECK(air_hook(0xC47A) == 0);
    if (!(cpu.a & 0x05)) { /* AND #$05, BNE return */
        cpu.a = HELD;      /* LDA Joy1_ButtonMask */
        CHECK(down_hook(0xC480) == 0);
        if (cpu.a & 0x04) {  /* AND #$04, BEQ return */
            cpu.a = CHANGED; /* LDA a:Joy1_ChangedButtonMask */
            CHECK(b_hook(0xC487) == 0);
            used = (cpu.a & 0x40) != 0; /* AND #$40, BEQ return */
        }
    }
    cpu.s = (uint8_t)(cpu.s + 2);
    return used;
}

/* What the main loop's readers of the pad decide, each as its code tests. */
typedef struct {
    bool sword, climb_down, cast, item, talk;
} Seen;
static Seen seen;
/* A pass of Game_MainLoop after its pad read. `inside`, when given, runs in
 * GameLoop_UpdatePlayer (a door's line, with reads of its own). */
static Seen pass(void (*inside)(void)) {
    memset(&seen, 0, sizeof seen);
    seen.sword = (CHANGED & B) && !(FLAGS & CLIMBING);   /* Player_CheckHandleAttack */
    seen.climb_down = (FLAGS & LADDER) && (HELD & DOWN); /* Player_CheckHandleVertMovement */
    if ((STATUS & WINGS) && (HELD & A) && (HELD & DOWN)) /* ... flying: Down is a climb down, */
        FLAGS &= 0xDA;                                   /* AND #$DA: no longer jumping or falling */
    if (inside) inside();                                /* Player_CheckHandleEnterDoor */
    CHECK(back_hook(0xDB5F) == 0);                       /* JSR Player_CastMagic */
    seen.cast = (HELD & UP) && (CHANGED & B);
    seen.item = check_use_current_item(0xDB62);
    seen.talk = CHANGED == UP; /* Player_CheckHandlePressUpOnNPC */
    fax_use_item_frame_end();
    return seen;
}
/* One frame of Game_MainLoop with the pad as given. */
static Seen frame(uint8_t pad) {
    nmi(pad);
    return pass(NULL);
}
static void fresh(void) {
    deactivate();
    activate();
    memset(ram, 0, sizeof ram);
    cpu = (CycModRegs){ .s = 0xFD };
}

static void the_item_alone(void) {
    fresh();
    CHECK(hook_on[0] == 1 && hook_on[1] == 1 && hook_on[2] == 1);
    CHECK(!frame(0).item);
    press();
    Seen s = frame(0);
    /* The item check sees Down and a new B; nothing else sees either. */
    CHECK(s.item && !s.sword && !s.climb_down && !s.talk);
    CHECK(HELD == 0 && CHANGED == 0 && PREV == 0); /* the masks are the pad's */
    CHECK(presses == 1 && shown == 1 && lapsed == 0);
    CHECK(!frame(0).item); /* one press, one use */
    /* At a ladder: Down+B would climb down; the button only uses the item. */
    FLAGS = LADDER;
    press();
    s = frame(0);
    CHECK(s.item && !s.climb_down);
}

static void the_games_rule(void) {
    /* Not while jumping or falling, as GameLoop_CheckUseCurrentItem refuses. */
    fresh();
    FLAGS = JUMPING;
    press();
    CHECK(!frame(0).item && shown == 0 && lapsed == 1);
    FLAGS = FALLING;
    press();
    CHECK(!frame(A).item && lapsed == 2);
    /* Flying without A: the hero falls, and Down+B would not use it either. */
    STATUS = WINGS;
    FLAGS = FALLING;
    CHECK(!frame(DOWN | B).item);
    press();
    CHECK(!frame(0).item && lapsed == 3);
    /* Flying with A held: Down+B gets there, through the climb down, and so
     * does the button, without Down. */
    FLAGS = JUMPING;
    frame(A);
    CHECK(frame(A | DOWN | B).item);
    FLAGS = JUMPING;
    frame(A);
    press();
    Seen s = frame(A);
    CHECK(s.item && flights == 1 && FLAGS == JUMPING); /* only A saw the flags differently */
    /* The Wing Boots spent: A held is a jump again. */
    STATUS = 0;
    FLAGS = JUMPING;
    press();
    CHECK(!frame(A).item);
}

static void the_player_buttons_pass_through(void) {
    fresh();
    /* Walking right with A newly pressed in the frame Use item goes down. */
    press();
    Seen s = frame(RIGHT | A);
    CHECK(s.item && !s.sword && HELD == (RIGHT | A) && CHANGED == (RIGHT | A));
    /* B the frame after: the sword, as ever. */
    s = frame(RIGHT | B);
    CHECK(s.sword && !s.item && CHANGED == B);
    /* Down+B the stock way still uses it, and swings the sword as it did. */
    frame(DOWN);
    s = frame(DOWN | B);
    CHECK(s.item && s.sword && shown == 1);
    /* The button with Down+B already there: one use. */
    frame(DOWN);
    press();
    s = frame(DOWN | B);
    CHECK(s.item && shown == 2);
}

static void a_press_lasts_one_frame(void) {
    fresh();
    /* Use item goes down while a conversation holds the main loop: the frame
     * ends without the item check, and the press is gone with it. */
    press();
    nmi(0);
    fax_use_item_frame_end();
    CHECK(lapsed == 1 && !frame(0).item);
    /* Two presses in one frame are one use. */
    press();
    press();
    CHECK(frame(0).item && presses == 3 && shown == 1);
    CHECK(!frame(0).item);
}

static void not_in_the_ending(void) {
    /* EndGame_MainLoop plays its own buttons: a press there shows nothing. */
    fresh();
    press();
    nmi(0);
    CHECK(!check_use_current_item(0xDC0E));
    fax_use_item_frame_end();
    CHECK(shown == 0 && lapsed == 1);
    /* The stack wraps within page 1, as the 6502's does. */
    fresh();
    cpu.s = 0x00;
    press();
    CHECK(frame(0).item && cpu.s == 0x00);
}

static void off_means_off(void) {
    fresh();
    deactivate();
    CHECK(hook_on[0] == 0 && hook_on[1] == 0 && hook_on[2] == 0 && hook_on[3] == 0 && !enabled);
    press();
    CHECK(!pressed && presses == 0);
    CHECK(!frame(0).item);
    /* The controller's button: no press, and the pad bytes are the host's. */
    pad_pressed();
    CHECK(pad_press == PAD_UP);
    uint8_t bytes[2] = { 0, 0 };
    fax_use_item_input(bytes);
    CHECK(bytes[0] == 0 && backs == 0);
}

/* ---- the controller's press: where the game waits for its frame ---- */

#define LATCH    ram[0x10]  /* Game_InterruptsHandledLatch */
#define HANDLERS ram[0x13]  /* Game_InterruptHandlersEnabled */
#define BANK     ram[0x100] /* CurrentROMBank */
/* The JSRs to WaitForNextFrame, as the ROM has them. */
enum {
    MAIN_LOOP = 0xDB4D,
    SCROLL = 0xDBC4, /* Game_MainLoop's, at the top of a pass and in a scroll */
    ENTER_SCREEN = 0xDAA9,
    FILL_HP = 0xC4E7, /* Game_SetupEnterScreen's, Player_FillHPAndMP's */
    DEATH = 0xD941,
    ENDING = 0xDBF6,
    START = 0xDB3F,  /* Player_HandleDeath's, EndGame_MainLoop's, Game_Start's */
    SPLASH = 0xA7AE, /* SplashAnimation_RunIntro's (bank 12) */
    TEXT = 0x87B0,
    MENU = 0x8ADD,
    EMPTY_LIST = 0x8B41,
    LIST = 0x8B71,
    PLAYER = 0x8E41, /* bank 12 */
    PAUSE = 0xE036,
    TITLE = 0xFC6E,
};
enum { LOGIC = 14, MENUS = 12 }; /* the bank Game_MainLoop keeps; bank 12 */

/* The main thread spins in WaitForNextFrame, called by the JSR at `jsr` with
 * `bank` at $8000: the NMI that starts the next frame reads the pad. */
static void waits_at(uint16_t jsr, uint8_t bank) {
    uint16_t pushed = (uint16_t)(jsr + 2);
    cpu.s = 0xFD;
    ram[0x1FE] = (uint8_t)pushed;
    ram[0x1FF] = (uint8_t)(pushed >> 8);
    cpu.pc = 0xCA29; /* @_loop: LDA Game_InterruptsHandledLatch */
    HANDLERS = 1;
    LATCH = 0;
    BANK = bank;
}
/* The main thread busy past the frame's start: the NMI reads nothing. */
static void lags(void) {
    cpu.pc = 0xE0CA;
    LATCH = 1;
}
/* One frame's start: the host's pad bytes through the input callback, then
 * the NMI, which reads them if the game waits. What the pad byte was. */
static uint8_t host_frame(uint8_t pad) {
    uint8_t bytes[2] = { pad, 0 };
    fax_use_item_input(bytes);
    CHECK(bytes[1] == 0);
    if (HANDLERS && !LATCH) {
        nmi(bytes[0]);
        LATCH = 1;
    }
    return bytes[0];
}
static uint8_t reads_at(uint16_t jsr, uint8_t bank) { /* next_read() for that wait */
    waits_at(jsr, bank);
    return next_read();
}

static void the_in_play_check(void) {
    fresh();
    /* The menus' and the text's waits, each with its bank. */
    CHECK(reads_at(TEXT, MENUS) == READ_MENU && reads_at(MENU, MENUS) == READ_MENU);
    CHECK(reads_at(EMPTY_LIST, MENUS) == READ_MENU && reads_at(LIST, MENUS) == READ_MENU);
    CHECK(reads_at(PLAYER, MENUS) == READ_MENU);
    CHECK(reads_at(PAUSE, LOGIC) == READ_MENU && reads_at(TITLE, MENUS) == READ_MENU);
    /* Play's: the main loop's, a scroll's, a door's, an item's, a death's,
     * the ending's, a new game's, the intro's. */
    CHECK(reads_at(MAIN_LOOP, LOGIC) == READ_PLAY && reads_at(SCROLL, LOGIC) == READ_PLAY);
    CHECK(reads_at(ENTER_SCREEN, LOGIC) == READ_PLAY && reads_at(FILL_HP, LOGIC) == READ_PLAY);
    CHECK(reads_at(DEATH, LOGIC) == READ_PLAY && reads_at(ENDING, LOGIC) == READ_PLAY);
    CHECK(reads_at(START, LOGIC) == READ_PLAY && reads_at(SPLASH, MENUS) == READ_PLAY);
    /* A bank-12 address with another bank mapped is that bank's code. */
    CHECK(reads_at(TEXT, LOGIC) == READ_PLAY && reads_at(LIST, 5) == READ_PLAY);
    /* No read: a lag frame (the latch still 1), or the NMI's work off. */
    waits_at(TEXT, MENUS);
    LATCH = 1;
    CHECK(next_read() == READ_NONE);
    waits_at(TEXT, MENUS);
    HANDLERS = 0;
    CHECK(next_read() == READ_NONE);
    /* The latch's other store (the game's reset), outside WaitForNextFrame:
     * play's, so no B. */
    waits_at(TEXT, MENUS);
    cpu.pc = 0xCA2E;
    CHECK(next_read() == READ_PLAY);
    cpu.pc = 0xCA24;
    CHECK(next_read() == READ_PLAY);
    cpu.pc = 0xCA25;
    CHECK(next_read() == READ_MENU);
    cpu.pc = 0xCA2D;
    CHECK(next_read() == READ_MENU);
}

static void b_backs_out_of_a_menu(void) {
    fresh();
    FLAGS = 0;
    /* The Select menu waits; B goes down: the NES B, new to the menu. */
    waits_at(MENU, MENUS);
    pad_pressed();
    CHECK(pad_press == PAD_NEW);
    CHECK(host_frame(0) == B && CHANGED == B);
    CHECK(pad_press == PAD_BACK && backs == 1 && presses == 0 && !pressed);
    fax_use_item_frame_end();
    CHECK(lapsed == 0);
    /* Held: the NES B held, not new again; in the next text box too. */
    waits_at(MENU, MENUS);
    CHECK(host_frame(0) == B && CHANGED == 0 && HELD == B);
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == B && CHANGED == 0);
    /* The menu closed: play's read lets the NES B go. Neither the sword nor
     * the item sees anything, and it stays up while B is held. */
    waits_at(MAIN_LOOP, LOGIC);
    CHECK(host_frame(DOWN) == DOWN && HELD == DOWN && CHANGED == DOWN);
    Seen s = pass(NULL);
    CHECK(!s.sword && !s.item && !s.cast && pad_press == PAD_DONE);
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == 0); /* a text box later: not again */
    waits_at(MAIN_LOOP, LOGIC);
    CHECK(host_frame(0) == 0 && !pass(NULL).item);
    pad_released();
    CHECK(pad_press == PAD_UP && backs == 1 && presses == 0 && shown == 0);
    /* X, the host's own NES B, is untouched in a menu and in play. */
    waits_at(LIST, MENUS);
    CHECK(host_frame(B) == B && CHANGED == B);
    waits_at(MAIN_LOOP, LOGIC);
    nmi(0);
    waits_at(MAIN_LOOP, LOGIC);
    CHECK(host_frame(B) == B && pass(NULL).sword && kept == 0);
    /* Every menu's wait takes it: the list, the PLAYER page, the pause's
     * screens, the title. */
    const uint16_t waits[] = { LIST, EMPTY_LIST, PLAYER, TEXT, PAUSE, TITLE };
    for (unsigned i = 0; i < sizeof waits / sizeof waits[0]; ++i) {
        nmi(0);
        waits_at(waits[i], waits[i] < 0xC000 ? MENUS : LOGIC);
        pad_pressed();
        CHECK(host_frame(0) == B && CHANGED == B && backs == 2 + i);
        pad_released();
        waits_at(waits[i], waits[i] < 0xC000 ? MENUS : LOGIC);
        CHECK(host_frame(0) == 0 && CHANGED == 0);
    }
    CHECK(presses == 0);
}

static void b_in_play_is_use_item(void) {
    fresh();
    /* Play's read: a press of Use item, and the NES B stays up. */
    waits_at(MAIN_LOOP, LOGIC);
    pad_pressed();
    CHECK(host_frame(0) == 0 && pad_press == PAD_DONE && presses == 1 && backs == 0);
    Seen s = pass(NULL);
    CHECK(s.item && !s.sword && !s.cast && shown == 1);
    /* Held into the item's line (the Red Potion's) and the text after: never
     * the NES B, so it backs out of nothing. */
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == 0 && CHANGED == 0);
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == 0);
    pad_released();
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == 0 && backs == 0 && presses == 1);
    /* A press down at a scroll's or a door's read is play's too. */
    waits_at(SCROLL, LOGIC);
    pad_pressed();
    CHECK(host_frame(0) == 0 && presses == 2);
    pad_released();
    fax_use_item_frame_end();
    waits_at(ENTER_SCREEN, LOGIC);
    pad_pressed();
    CHECK(host_frame(0) == 0 && presses == 3 && backs == 0);
    pad_released();
}

static void a_press_waits_for_a_read(void) {
    fresh();
    /* A text box opening: lag frames, no read. The press waits, neither yet. */
    waits_at(TEXT, MENUS);
    lags();
    pad_pressed();
    CHECK(host_frame(0) == 0 && pad_press == PAD_NEW && presses == 0 && backs == 0);
    lags();
    CHECK(host_frame(0) == 0 && pad_press == PAD_NEW);
    /* The box's first read takes it: the NES B, new there. */
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == B && CHANGED == B && backs == 1);
    pad_released();
    /* A tap, down and up before any read: let go in a lag frame, it is gone,
     * as an NES button's would be. */
    nmi(0);
    lags();
    pad_pressed();
    pad_released();
    CHECK(pad_press == PAD_NEW);
    CHECK(host_frame(0) == 0 && pad_press == PAD_UP && backs == 1 && presses == 0);
    /* Let go just before a read: that read has it, for that one frame. */
    waits_at(TEXT, MENUS);
    pad_pressed();
    pad_released();
    CHECK(host_frame(0) == B && CHANGED == B && backs == 2 && pad_press == PAD_UP);
    waits_at(TEXT, MENUS);
    CHECK(host_frame(0) == 0 && CHANGED == 0);
    /* And in play, a tap is one Use item press. */
    waits_at(MAIN_LOOP, LOGIC);
    pad_pressed();
    pad_released();
    CHECK(host_frame(0) == 0 && presses == 1 && pass(NULL).item);
    /* A lag frame keeps a back press's B in the byte (no read takes it). */
    waits_at(MENU, MENUS);
    pad_pressed();
    CHECK(host_frame(0) == B);
    lags();
    CHECK(host_frame(0) == B && pad_press == PAD_BACK);
    waits_at(MENU, MENUS);
    CHECK(host_frame(0) == B && CHANGED == 0);
    pad_released();
}

/* A door's line, in GameLoop_UpdatePlayer: a read of its own, which a new
 * press of B closes, before the rest of the pass. */
static void doors_line(void) {
    waits_at(TEXT, MENUS);
    pad_pressed();
    CHECK(host_frame(UP | DOWN) == (UP | DOWN | B) && (CHANGED & B));
    CHECK(pad_press == PAD_BACK);
}
static void doors_line_with_x(void) {
    waits_at(TEXT, MENUS);
    pad_pressed();
    CHECK(host_frame(UP | DOWN | B) == (UP | DOWN | B) && (CHANGED & B));
}

static void a_line_it_closes_is_all(void) {
    fresh();
    /* Up at a locked door, Up and Down still held: the line's read sees B
     * new, then the cast and the item check do not. */
    waits_at(MAIN_LOOP, LOGIC);
    host_frame(UP | DOWN);
    Seen s = pass(doors_line);
    CHECK(!s.sword && !s.cast && !s.item && kept == 1 && presses == 0 && backs == 1);
    CHECK(CHANGED == 0 && HELD == (UP | DOWN | B)); /* the held mask is the pad's */
    /* The next pass: play's read lets it go. */
    waits_at(MAIN_LOOP, LOGIC);
    CHECK(host_frame(UP | DOWN) == (UP | DOWN));
    s = pass(NULL);
    CHECK(!s.cast && !s.item && pad_press == PAD_DONE);
    pad_released();
    /* With X down with it, the B is X's as well: the game's own, as ever. */
    nmi(0);
    waits_at(MAIN_LOOP, LOGIC);
    host_frame(UP | DOWN);
    s = pass(doors_line_with_x);
    CHECK(s.cast && kept == 1);
    pad_released();
}

int main(void) {
    the_item_alone();
    the_games_rule();
    the_player_buttons_pass_through();
    a_press_lasts_one_frame();
    not_in_the_ending();
    off_means_off();
    the_in_play_check();
    b_backs_out_of_a_menu();
    b_in_play_is_use_item();
    a_press_waits_for_a_read();
    a_line_it_closes_is_all();
    printf("use_item_test: %u checks passed\n", checks);
    return 0;
}
