/* Unit tests for the Use item button (fax_use_item.c), without the emulator
 * or a ROM. The module runs against a model of the machine: CPU RAM, the
 * stack, the registers, the NMI's pad read (Input_HandleOnInterrupt), and
 * GameLoop_CheckUseCurrentItem's code as the disassembly has it: each load,
 * then the hook at the AND after it (which may change A), then the AND and
 * its branch. Around it, the other readers of the pad in Game_MainLoop's
 * order, so the tests check what each part of the game sees. */
#include "fax_use_item.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu = { .s = 0xFD };
static int hook_on[3] = { -1, -1, -1 };          /* each hook's enabled state, as last set */

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
           (!strcmp(id, "faxanadu.use-item.b") && addr == 0xC487 && cb == b_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < 3; ++i)
        if (!strcmp(id, hook_ids[i])) { hook_on[i] = on; return 1; }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.use-item") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

enum { A = 0x80, B = 0x40, UP = 0x08, DOWN = 0x04, LEFT = 0x02, RIGHT = 0x01 };
enum { JUMPING = 0x01, FALLING = 0x04, CLIMBING = 0x10, LADDER = 0x08, WINGS = 0x80 };   /* Player_Flags; Player_StatusFlag */
#define HELD    ram[0x16]                        /* Joy1_ButtonMask */
#define PREV    ram[0x18]                        /* Joy1_PrevButtonMask */
#define CHANGED ram[0x19]                        /* Joy1_ChangedButtonMask */
#define FLAGS   ram[0xA4]                        /* Player_Flags */
#define STATUS  ram[0xA5]                        /* Player_StatusFlag */

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
    ram[0x100 | cpu.s] = (uint8_t)(pushed >> 8); cpu.s = (uint8_t)(cpu.s - 1);
    ram[0x100 | cpu.s] = (uint8_t)pushed;        cpu.s = (uint8_t)(cpu.s - 1);
    bool used = false;
    cpu.a = FLAGS;                               /* LDA Player_Flags */
    CHECK(air_hook(0xC47A) == 0);
    if (!(cpu.a & 0x05)) {                       /* AND #$05, BNE return */
        cpu.a = HELD;                            /* LDA Joy1_ButtonMask */
        CHECK(down_hook(0xC480) == 0);
        if (cpu.a & 0x04) {                      /* AND #$04, BEQ return */
            cpu.a = CHANGED;                     /* LDA a:Joy1_ChangedButtonMask */
            CHECK(b_hook(0xC487) == 0);
            used = (cpu.a & 0x40) != 0;          /* AND #$40, BEQ return */
        }
    }
    cpu.s = (uint8_t)(cpu.s + 2);
    return used;
}

/* What the main loop's readers of the pad decide, each as its code tests. */
typedef struct { bool sword, climb_down, item, talk; } Seen;
static Seen seen;
/* One frame of Game_MainLoop with the pad as given. */
static Seen frame(uint8_t pad) {
    memset(&seen, 0, sizeof seen);
    nmi(pad);
    seen.sword = (CHANGED & B) && !(FLAGS & CLIMBING);        /* Player_CheckHandleAttack */
    seen.climb_down = (FLAGS & LADDER) && (HELD & DOWN);      /* Player_CheckHandleVertMovement */
    if ((STATUS & WINGS) && (HELD & A) && (HELD & DOWN))      /* ... flying: Down is a climb down, */
        FLAGS &= 0xDA;                                         /* AND #$DA: no longer jumping or falling */
    seen.item = check_use_current_item(0xDB62);
    seen.talk = CHANGED == UP;                                 /* Player_CheckHandlePressUpOnNPC */
    fax_use_item_frame_end();
    return seen;
}
static void fresh(void) {
    deactivate(); activate();
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
    CHECK(HELD == 0 && CHANGED == 0 && PREV == 0);   /* the masks are the pad's */
    CHECK(presses == 1 && shown == 1 && lapsed == 0);
    CHECK(!frame(0).item);                           /* one press, one use */
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
    CHECK(s.item && flights == 1 && FLAGS == JUMPING);   /* only A saw the flags differently */
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
    press(); press();
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
    CHECK(hook_on[0] == 0 && hook_on[1] == 0 && hook_on[2] == 0 && !enabled);
    press();
    CHECK(!pressed && presses == 0);
    CHECK(!frame(0).item);
}

int main(void) {
    the_item_alone();
    the_games_rule();
    the_player_buttons_pass_through();
    a_press_lasts_one_frame();
    not_in_the_ending();
    off_means_off();
    printf("use_item_test: %u checks passed\n", checks);
    return 0;
}
