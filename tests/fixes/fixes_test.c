/* Unit tests for Small fixes (fax_fixes.c), without the emulator or a ROM.
 * The module runs against a model of the machine: CPU RAM, the registers, the
 * ROM bytes each site reads (bank 5's typing-sound table and the LDA after
 * it), and the game's code around each hook site, played instruction by
 * instruction as the disassembly has it: SoundEffect_Message_OnTick's index
 * and NOISE_LO store, and Player_HandleShieldHitByMagic with
 * Player_HandleIFrames' countdown. So the tests check what the game does (the
 * sound written, the HP lost and the iframes), not the module's own
 * arithmetic. */
#include "fax_fixes.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* RAM */
#define SOUND_STATE_COUNTER   0x0127u   /* SoundEffect_State_Counter */
#define STATUS_FLAG           0x00A5u   /* Player_StatusFlag: $02 knockback */
#define HP                    0x0431u
/* ROM: bank 5's SOUNDEFFECT_MESSAGE_NOISE_LO ($10, $01) and the next routine,
 * SoundEffect_HitEnemy_Setup: LDA #$14 */
#define MESSAGE_NOISE_LO      0x86D1u
static const uint8_t bank5_bytes[] = { 0x10, 0x01, 0xA9, 0x14 };
#define HOOD_SHOT_DAMAGE      8u        /* SPRITE_ENTITY_DAMAGES[$51] */

/* ---- the machine ---- */
static uint8_t ram[0x800], bank5[0x4000];
static unsigned mapped = 5;
static CycModRegs cpu;
static bool isolated;
static int sound_on = -1, shield_on = -1;   /* the hooks' enabled states, as last set */

uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr >= 0x8000 && addr < 0xC000 && mapped == 5) return bank5[addr - 0x8000];
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
           (!strcmp(id, shield_id) && addr == HANDLE_HIT_BY_MAGIC && cb == hit_by_magic_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    if (!strcmp(id, sound_id)) sound_on = on;
    else if (!strcmp(id, shield_id)) shield_on = on;
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
    memcpy(&bank5[MESSAGE_NOISE_LO - 0x8000], bank5_bytes, sizeof bank5_bytes);
    cpu = (CycModRegs){ .a = 0x5A, .x = 0x33, .y = 0x44, .s = 0xF0, .p = 0x24 };
    isolated = false;
    mapped = 5;
    deactivate();
    activate();
    CHECK(enabled && sound_on == 1 && shield_on == 1);
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

/* ---- the shield: Ointment and iframes ---- */

static unsigned shield_sounds;   /* sound $0C, played as Player_HandleShieldHitByMagic removes a shot */

/* Player_HandleShieldHitByMagic for the Hood's shot (8 HP), on the machine
 * as it is: JSR Player_HandleHitByMagic, whose entry is the hook site; its
 * body takes SPRITE_ENTITY_DAMAGES >> (SelectedShield + 1) (LSR, DEY, BPL)
 * and, if any, Player_SetDamagedBySprite (iframes $3C, knockback) and
 * Player_ReduceHP; then the shot is removed with sound $0C. Returns the HP
 * lost. */
static unsigned shield_shot(uint8_t shield, bool hooked) {
    mapped = 14;
    uint8_t hp = ram[HP];
    if (!hooked || !hit_by_magic_hook(HANDLE_HIT_BY_MAGIC)) {
        uint8_t a = HOOD_SHOT_DAMAGE;
        for (int y = shield; y >= 0; --y) a >>= 1;
        if (a) {
            ram[INVINCIBILITY_PHASE] = 0x3C;
            ram[STATUS_FLAG] |= 0x02;
            ram[HP] = (uint8_t)(ram[HP] - a);
        }
    }
    ++shield_sounds;
    return (unsigned)(hp - ram[HP]);
}

/* Player_HandleIFrames, once a frame: the phase counts down, and knockback
 * ends three frames after the hit (at $39). */
static void frames_pass(unsigned n) {
    while (n--) {
        if (ram[INVINCIBILITY_PHASE]) ram[INVINCIBILITY_PHASE]--;
        if (ram[INVINCIBILITY_PHASE] <= 0x39) ram[STATUS_FLAG] &= (uint8_t)~0x02;
    }
}

/* One shot on the shield from 80 HP, no iframes, Ointment's time left
 * ($FF none). */
static unsigned shield_hit(uint8_t shield, uint8_t ointment, bool hooked) {
    ram[DURATION_OINTMENT] = ointment;
    ram[HP] = 0x50;
    ram[INVINCIBILITY_PHASE] = ram[STATUS_FLAG] = 0;
    return shield_shot(shield, hooked);
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
    CHECK(ointment_saved == 6 && iframes_saved == 0);
}

/* Two Hood shots on the shield, `apart` frames apart, from 80 HP with no
 * Ointment: the HP each takes in `lost`; the iframes and knockback after the
 * second are left in RAM. */
static void two_shots(uint8_t shield, unsigned apart, bool hooked, unsigned lost[2]) {
    ram[DURATION_OINTMENT] = 0xFF;
    ram[HP] = 0x50;
    ram[INVINCIBILITY_PHASE] = ram[STATUS_FLAG] = 0;
    lost[0] = shield_shot(shield, hooked);
    frames_pass(apart);
    lost[1] = shield_shot(shield, hooked);
}

static void iframes_and_the_shield(void) {
    setup();
    enum { SMALL, LARGE, MAGIC };
    unsigned lost[2], sounds_before = shield_sounds;
    /* The original: 20 frames after the first hit (the iframes at $28), the
     * second shot hurts again, starts the iframes over and knocks him back. */
    two_shots(SMALL, 20, false, lost);
    CHECK(lost[0] == 4 && lost[1] == 4 && ram[INVINCIBILITY_PHASE] == 0x3C && (ram[STATUS_FLAG] & 0x02));
    CHECK(shield_sounds == sounds_before + 2);
    /* The fix: the second does nothing; the iframes go on counting down from
     * the first, knockback over. The shield still takes the shot, with its
     * sound. */
    two_shots(SMALL, 20, true, lost);
    CHECK(lost[0] == 4 && lost[1] == 0 && ram[INVINCIBILITY_PHASE] == 0x3C - 20 && !(ram[STATUS_FLAG] & 0x02));
    CHECK(shield_sounds == sounds_before + 4);
    CHECK(iframes_saved == 1 && ointment_saved == 0);
    /* Every shield, both shots in the same frame (one pass of the sprite
     * loop). */
    for (uint8_t shield = SMALL; shield <= MAGIC; ++shield) {
        two_shots(shield, 0, false, lost);
        CHECK(lost[0] == (8u >> (shield + 1)) && lost[1] == lost[0]);
        two_shots(shield, 0, true, lost);
        CHECK(lost[0] == (8u >> (shield + 1)) && lost[1] == 0);
    }
    CHECK(iframes_saved == 4);
    /* The whole 60 frames, down to the last: the shield keeps it out. Once
     * they are over, a shot hurts again, fix or not. */
    two_shots(SMALL, 59, true, lost);
    CHECK(lost[1] == 0 && ram[INVINCIBILITY_PHASE] == 1);
    two_shots(SMALL, 60, true, lost);
    CHECK(lost[1] == 4 && ram[INVINCIBILITY_PHASE] == 0x3C);
    two_shots(SMALL, 60, false, lost);
    CHECK(lost[1] == 4);
    CHECK(iframes_saved == 5);
    /* Iframes and Ointment at once (Sugata's flash under Ointment sets the
     * iframes): saved, and counted as iframes, which the touch handler checks
     * first. */
    ram[DURATION_OINTMENT] = 0x05;
    ram[INVINCIBILITY_PHASE] = 0x30;
    ram[HP] = 0x50;
    CHECK(shield_shot(SMALL, true) == 0 && ram[INVINCIBILITY_PHASE] == 0x30);
    CHECK(iframes_saved == 6 && ointment_saved == 0);
    CHECK(shield_guard(0, 0xFF) == LET_THROUGH && shield_guard(0, 0x80) == LET_THROUGH &&
          shield_guard(1, 0xFF) == IFRAMES && shield_guard(0x3C, 0x00) == IFRAMES && shield_guard(0, 0x7F) == OINTMENT);
    /* Saved in an isolated machine too, but not counted. */
    isolated = true;
    two_shots(SMALL, 10, true, lost);
    isolated = false;
    CHECK(lost[1] == 0 && iframes_saved == 6);
}

static void off_and_on(void) {
    setup();
    deactivate();
    CHECK(!enabled && sound_on == 0 && shield_on == 0 && sounds == 0 && iframes_saved == 0 && ointment_saved == 0);
    activate();
    CHECK(enabled && sound_on == 1 && shield_on == 1);
}

int main(void) {
    /* The module registered itself at startup (it aborts if a stub above refused). */
    the_typing_sound();
    ointment_and_the_shield();
    iframes_and_the_shield();
    off_and_on();
    printf("small fixes: %u checks passed\n", checks);
    return 0;
}
