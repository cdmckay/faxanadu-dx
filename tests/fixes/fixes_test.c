/* Unit tests for Small fixes (fax_fixes.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers, the
 * ROM bytes each site reads (bank 5's typing-sound table and the LDA after
 * it; bank 12 around the mantra's code and characters), and the game's code
 * around each hook site, played instruction by instruction as the disassembly
 * has it: SoundEffect_Message_OnTick's index and NOISE_LO store, the mantra's
 * typing through TextBox_ShowMessage's line-end check, and
 * Player_HandleShieldHitByMagic. So the tests check what the game does (the
 * sound written, the cells the mantra lands in, the HP lost), not the
 * module's own arithmetic. */
#include "fax_fixes.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* RAM */
#define SOUND_STATE_COUNTER   0x0127u   /* SoundEffect_State_Counter */
#define MESSAGE_ID            0x0213u
#define CHAR_POS_FOR_LINE     0x0216u   /* TextBox_CharPosForLine */
#define DRAWN_LINE_NUM        0x0217u   /* TextBox_DrawnLineNum */
#define INVINCIBILITY_PHASE   0x00ADu   /* Player_InvincibilityPhase */
#define HP                    0x0431u
/* ROM: bank 5's SOUNDEFFECT_MESSAGE_NOISE_LO ($10, $01) and the next routine,
 * SoundEffect_HitEnemy_Setup: LDA #$14 */
#define MESSAGE_NOISE_LO      0x86D1u
static const uint8_t bank5_bytes[] = { 0x10, 0x01, 0xA9, 0x14 };
/* Bank 12 from PasswordScreen_ShowNextChar's JSR TextBox_ShowMessage ($875F:
 * 20 FF F4), PLP, RTS, then PASSWORD_ENTERED_CHARS. */
#define SHOW_MESSAGE_JSR      0x875Fu
static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789,?";
/* Where the Guru's greeting ($22, bank 13 $8739-$876A) leaves the message:
 * Message_StartAddr $8700, Message_ProcessedLength $6B, the next message's
 * first byte. Bank 12's byte there is $48, an H. */
#define GREETING_PAGE         0x8700u
#define AFTER_GREETING        0x6Bu
#define AT_FF                 0x60u     /* bank 12's $8760, the JSR's operand: $FF */
#define HOOD_SHOT_DAMAGE      8u        /* SPRITE_ENTITY_DAMAGES[$51] */

/* ---- the machine ---- */
static uint8_t ram[0x800], bank5[0x4000], bank12[0x4000];
static unsigned mapped = 12;
static CycModRegs cpu;
static bool isolated;
static int sound_on = -1, mantra_on = -1, ointment_on = -1;   /* the hooks' enabled states, as last set */

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr >= 0x8000 && addr < 0xC000) return (mapped == 5 ? bank5 : bank12)[addr - 0x8000];
    return 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { uint16_t pc = cpu.pc; cpu = *in; cpu.pc = pc; }
bool cyc_mod_isolated(void) { return isolated; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    return (!strcmp(id, sound_id) && addr == MESSAGE_SOUND_INDEX && cb == message_sound_hook) ||
           (!strcmp(id, mantra_id) && addr == SHOW_PASSWORD && cb == show_password_hook) ||
           (!strcmp(id, ointment_id) && addr == HANDLE_HIT_BY_MAGIC && cb == hit_by_magic_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, sound_id)) sound_on = on;
    else if (!strcmp(id, mantra_id)) mantra_on = on;
    else if (!strcmp(id, ointment_id)) ointment_on = on;
    else return 0;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.small-fixes") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }

static void setup(void) {
    memset(ram, 0, sizeof ram);
    memset(bank5, 0xEA, sizeof bank5);
    memset(bank12, 0xEA, sizeof bank12);
    memcpy(&bank5[MESSAGE_NOISE_LO - 0x8000], bank5_bytes, sizeof bank5_bytes);
    memcpy(&bank12[SHOW_MESSAGE_JSR - 0x8000], (const uint8_t[]){ 0x20, 0xFF, 0xF4, 0x28, 0x60 }, 5);
    memcpy(&bank12[PASSWORD_ENTERED_CHARS - 0x8000], chars, 64);
    cpu = (CycModRegs){ .a = 0x5A, .x = 0x33, .y = 0x44, .s = 0xF0, .p = 0x24 };
    isolated = false;
    mapped = 12;
    deactivate();
    activate();
    CHECK(enabled && sound_on == 1 && mantra_on == 1 && ointment_on == 1);
}

/* ---- the typing sound ---- */

/* One tick of SoundEffect_Message_OnTick, from $86AD: LDA
 * SoundEffect_State_Counter, AND #$02, the hook site at TAX, TAX, LDA
 * SOUNDEFFECT_MESSAGE_NOISE_LO,X, STA NOISE_LO, ..., INC
 * SoundEffect_State_Counter. Returns what NOISE_LO got. */
static uint8_t message_tick(bool hooked) {
    mapped = 5;
    cpu.a = ram[SOUND_STATE_COUNTER] & 0x02;
    if (hooked) CHECK(message_sound_hook(MESSAGE_SOUND_INDEX) == 0);   /* the game goes on */
    cpu.x = cpu.a;                                                      /* TAX */
    uint8_t noise_lo = cyc_mod_peek((uint16_t)(MESSAGE_NOISE_LO + cpu.x));
    ram[SOUND_STATE_COUNTER]++;
    return noise_lo;
}

/* A character's typing sound: SoundEffect_Message_Setup zeroes the counter,
 * then four ticks. */
static void character_sound(bool hooked, uint8_t out[4]) {
    ram[SOUND_STATE_COUNTER] = 0;
    for (int t = 0; t < 4; ++t) out[t] = message_tick(hooked);
}

static void the_typing_sound(void) {
    setup();
    uint8_t ticks[4];
    character_sound(false, ticks);
    CHECK(!memcmp(ticks, ((uint8_t[]){ 0x10, 0x10, 0xA9, 0xA9 }), 4));   /* the original: past the table */
    character_sound(true, ticks);
    CHECK(!memcmp(ticks, ((uint8_t[]){ 0x10, 0x10, 0x01, 0x01 }), 4));   /* the table's own, every two ticks */
    CHECK(sounds == 4);
    CHECK(message_sound_index(0) == 0 && message_sound_index(2) == 1);
    /* Only A changes, and isolated machines are fixed too but not counted. */
    ram[SOUND_STATE_COUNTER] = 2;
    cpu.a = 2;
    CycModRegs before = cpu;
    isolated = true;
    CHECK(message_sound_hook(MESSAGE_SOUND_INDEX) == 0);
    isolated = false;
    CHECK(cpu.a == 1 && cpu.x == before.x && cpu.y == before.y && cpu.s == before.s && cpu.p == before.p);
    CHECK(sounds == 4);
}

/* ---- the mantra ---- */

/* The text box's four rows of 16 cells, as TextBox_WriteChar puts a glyph
 * at PPU low byte CharPosForLine << 4 (8 bits) of row DrawnLineNum. */
static char cells[4][17];

/* IScriptAction_ShowPassword typing `mantra`, from the message state the
 * last message left (Message_StartAddr `page`, Message_ProcessedLength
 * `processed`): the hook at its entry when `hooked`, TextBox_ClearPasswordSize,
 * then per character TextBox_WriteChar and
 * TextBox_ShowMessage_HandleSpaceOrEndOfLine: INC CharPosForLine, under 16
 * return; else LDA (Message_StartAddr),Message_ProcessedLength with bank 12
 * mapped: $FF ends the message (MessageID 0), $FC returns, anything else
 * starts a new line. */
static void type_mantra(const char *mantra, uint16_t page, uint8_t processed, bool hooked) {
    mapped = 12;
    ram[MESSAGE_START_ADDR] = (uint8_t)page;
    ram[MESSAGE_START_ADDR + 1] = (uint8_t)(page >> 8);
    ram[MESSAGE_PROCESSED_LENGTH] = processed;
    if (hooked) CHECK(show_password_hook(SHOW_PASSWORD) == 0);         /* the game goes on */
    ram[CHAR_POS_FOR_LINE] = ram[DRAWN_LINE_NUM] = 0;                  /* TextBox_ClearPasswordSize */
    memset(cells, ' ', sizeof cells);
    for (int r = 0; r < 4; ++r) cells[r][16] = 0;
    for (const char *c = mantra; *c; ++c) {
        cells[ram[DRAWN_LINE_NUM] & 3][(uint8_t)(ram[CHAR_POS_FOR_LINE] << 4) >> 4] = *c;
        if (++ram[CHAR_POS_FOR_LINE] < 16) continue;
        uint16_t at = (uint16_t)((ram[MESSAGE_START_ADDR] | ram[MESSAGE_START_ADDR + 1] << 8) +
                                 ram[MESSAGE_PROCESSED_LENGTH]);
        uint8_t next = cyc_mod_peek(at);
        if (next == 0xFF) { ram[MESSAGE_ID] = 0; continue; }
        if (next == 0xFC) continue;
        ram[CHAR_POS_FOR_LINE] = 0;
        ram[DRAWN_LINE_NUM]++;
    }
}

static void the_mantra(void) {
    const char *mantra = "v5UIMFkkYpkxUUh0vg";                     /* 18 characters: a second line */
    setup();
    /* After the Guru's greeting, as the game always has it: bank 12 holds an
     * H where the next message starts, so the original is right. */
    CHECK(cyc_mod_peek(GREETING_PAGE + AFTER_GREETING) == 'H');
    type_mantra(mantra, GREETING_PAGE, AFTER_GREETING, false);
    CHECK(!strcmp(cells[0], "v5UIMFkkYpkxUUh0") && !strcmp(cells[1], "vg              "));
    /* With the message pointing at bank 12's $FF, the original starts no
     * line: the 17th and 18th characters overwrite the first two. */
    CHECK(cyc_mod_peek(GREETING_PAGE + AT_FF) == 0xFF);
    type_mantra(mantra, GREETING_PAGE, AT_FF, false);
    CHECK(!strcmp(cells[0], "vgUIMFkkYpkxUUh0") && !strcmp(cells[1], "                "));
    CHECK(ram[DRAWN_LINE_NUM] == 0 && ram[CHAR_POS_FOR_LINE] == 18);
    /* The fix: either way, two lines, and the pointer on the characters. */
    type_mantra(mantra, GREETING_PAGE, AT_FF, true);
    CHECK(!strcmp(cells[0], "v5UIMFkkYpkxUUh0") && !strcmp(cells[1], "vg              "));
    CHECK(ram[MESSAGE_START_ADDR] == 0x64 && ram[MESSAGE_START_ADDR + 1] == 0x87 && ram[MESSAGE_PROCESSED_LENGTH] == 0);
    CHECK(mantras == 1 && last_pointer_was[0] == 0x00 && last_pointer_was[1] == 0x87 && last_pointer_was[2] == AT_FF);
    type_mantra(mantra, GREETING_PAGE, AFTER_GREETING, true);
    CHECK(!strcmp(cells[0], "v5UIMFkkYpkxUUh0") && !strcmp(cells[1], "vg              "));
    /* A $FC (a pause) starts no line either; the fix again. */
    bank12[0x9000 - 0x8000] = 0xFC;
    type_mantra(mantra, 0x9000, 0, false);
    CHECK(!strcmp(cells[0], "vgUIMFkkYpkxUUh0"));
    type_mantra(mantra, 0x9000, 0, true);
    CHECK(!strcmp(cells[0], "v5UIMFkkYpkxUUh0") && !strcmp(cells[1], "vg              "));
    /* The longest mantra, 32 characters: both line ends. */
    const char *longest = "ABCDEFGHIJKLMNOPabcdefghijklmnop";
    type_mantra(longest, GREETING_PAGE, AT_FF, true);
    CHECK(!strcmp(cells[0], "ABCDEFGHIJKLMNOP") && !strcmp(cells[1], "abcdefghijklmnop"));
    CHECK(ram[DRAWN_LINE_NUM] == 2 && ram[CHAR_POS_FOR_LINE] == 0);
    /* Not counted in an isolated machine. */
    isolated = true;
    type_mantra(mantra, GREETING_PAGE, AFTER_GREETING, true);
    isolated = false;
    CHECK(mantras == 4);
}

/* ---- Ointment and the shield ---- */

/* Player_HandleShieldHitByMagic for the Hood's shot (8 HP): JSR
 * Player_HandleHitByMagic, whose entry is the hook site; its body takes
 * SPRITE_ENTITY_DAMAGES >> (SelectedShield + 1) (LSR, DEY, BPL) and, if any,
 * Player_SetDamagedBySprite (iframes $3C) and Player_ReduceHP; then the shot
 * is removed with sound $0C. Returns the HP lost. */
static unsigned shield_hit(uint8_t shield, uint8_t ointment, bool hooked) {
    mapped = 14;
    ram[DURATION_OINTMENT] = ointment;
    ram[HP] = 0x50;
    ram[INVINCIBILITY_PHASE] = 0;
    if (!hooked || !hit_by_magic_hook(HANDLE_HIT_BY_MAGIC)) {
        uint8_t a = HOOD_SHOT_DAMAGE;
        for (int y = shield; y >= 0; --y) a >>= 1;
        if (a) {
            ram[INVINCIBILITY_PHASE] = 0x3C;
            ram[HP] = (uint8_t)(ram[HP] - a);
        }
    }
    return 0x50u - ram[HP];
}

static void ointment_and_the_shield(void) {
    setup();
    enum { SMALL, LARGE, MAGIC };
    /* No Ointment ($FF): the shield lets its share through, fix or not. */
    for (int hooked = 0; hooked < 2; ++hooked) {
        CHECK(shield_hit(SMALL, 0xFF, hooked) == 4 && ram[INVINCIBILITY_PHASE] == 0x3C);
        CHECK(shield_hit(LARGE, 0xFF, hooked) == 2);
        CHECK(shield_hit(MAGIC, 0xFF, hooked) == 1);
    }
    CHECK(ointment_saved == 0);
    /* Ointment running (bit 7 clear, down to its last second, 0): the
     * original lets it through and knocks him back; the fix, nothing. */
    for (unsigned left = 0; left < 0x80; left += 0x3F) {
        CHECK(shield_hit(SMALL, (uint8_t)left, false) == 4 && ram[INVINCIBILITY_PHASE] == 0x3C);
        CHECK(shield_hit(SMALL, (uint8_t)left, true) == 0 && ram[INVINCIBILITY_PHASE] == 0);
        CHECK(shield_hit(MAGIC, (uint8_t)left, true) == 0);
    }
    CHECK(ointment_saved == 6);
    CHECK(ointment_running(0x00) && ointment_running(0x7F) && !ointment_running(0x80) && !ointment_running(0xFF));
    isolated = true;
    CHECK(shield_hit(SMALL, 0x10, true) == 0);
    isolated = false;
    CHECK(ointment_saved == 6);
}

static void off_and_on(void) {
    setup();
    deactivate();
    CHECK(!enabled && sound_on == 0 && mantra_on == 0 && ointment_on == 0 && sounds == 0 && mantras == 0);
    activate();
    CHECK(enabled && sound_on == 1 && mantra_on == 1 && ointment_on == 1);
}

int main(void) {
    /* The module registered itself at startup (it aborts if a stub above refused). */
    the_typing_sound();
    the_mantra();
    ointment_and_the_shield();
    off_and_on();
    printf("small fixes: %u checks passed\n", checks);
    return 0;
}
