/* Unit tests for recharging items (fax_recharge.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, the
 * registers, the hook sites as the engine dispatches them (a handled site
 * returns as an RTS from S would, so a pushed address is a jump), the PPU
 * buffer as the NMI drains it into the HUD's T: and the MP bar, the
 * committed calls it makes (Sound_PlayEffect, Player_SetMP, which waits for
 * room in the buffer), the pad as the NMI reads it, and the item, countdown,
 * death and pickup code
 * played step by step as the disassembly has them (the JSRs push their real
 * return addresses). So the tests check what the player holds, sees and
 * hears rather than restate the module's constants. */
#include "fax_recharge.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
uint32_t cyc_ring_frame;                      /* the engine's frames completed: what a freeze's end is noted at */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int sound = -1;                        /* the last effect Sound_PlayEffect queued */
static int bar_mp = -1;                       /* the MP the HUD's bar was last queued with */
static unsigned bar_calls, bars_queued;       /* Player_SetMP called; and it queued its bar */
static uint8_t pad;                           /* the buttons the player holds: BUTTON_A flies */

static uint8_t bank12[0x4000];               /* $8000-$BFFF as bank 12 maps it: the Gurus' scripts */
/* Read overrides (cyc_mod_set_prg_read_override), in banks 12 and 13: what a
 * read of a byte returns instead, and how many bytes have an entry. They are
 * not machine state: a save state's load does not bring them back. */
static uint8_t served[2][0x4000];
static bool serving[2][0x4000];
static unsigned entries;
static bool table_full;                      /* the engine's table has no room: every new entry fails */
bool cyc_mod_set_prg_read_override(uint32_t offset, uint8_t compare, uint8_t value) {
    unsigned bank = offset / 0x4000, at = offset % 0x4000;
    CHECK((bank == 12 || bank == 13) && compare == 0xFF);   /* unused ROM, in the two banks it uses */
    if (!serving[bank - 12][at]) {
        if (table_full) return false;
        ++entries;
    }
    serving[bank - 12][at] = true;
    served[bank - 12][at] = value;
    return true;
}
static void overrides_lost(void) { memset(serving, 0, sizeof serving); entries = 0; }
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr < 0x8000 || addr >= 0xC000) return 0;
    return serving[0][addr - 0x8000] ? served[0][addr - 0x8000] : bank12[addr - 0x8000];
}
/* Message $C3, as Messages_Load would read it from bank 13: '/' for a new line. */
static void slot_text(char out[64]) {
    unsigned n = 0;
    for (unsigned i = 0; i < SLOT_LEN && n < 63; ++i) {
        uint16_t at = (uint16_t)(SLOT - 0x8000 + i);
        uint8_t c = serving[1][at] ? served[1][at] : 0xFF;
        if (c == 0xFF) break;
        out[n++] = c == 0xFD ? ' ' : c == 0xFE ? '/' : (char)c;
    }
    out[n] = 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated; }

/* PPUBuffer_HasCapacity: ReadOffset - WriteOffset is 0 (empty) or at least $24. */
static bool has_capacity(uint8_t write) { uint8_t room = (uint8_t)(ram[PPU_BUFFER_READ] - write); return room == 0 || room >= 0x24; }
/* One PPU buffer command: [length][address][length tiles]. */
static void put(uint8_t *w, uint8_t len, uint16_t ppu) {
    ram[PPU_BUFFER + *w] = len;
    ram[PPU_BUFFER + (uint8_t)(*w + 1)] = (uint8_t)(ppu >> 8);
    ram[PPU_BUFFER + (uint8_t)(*w + 2)] = (uint8_t)ppu;
    for (unsigned k = 0; k < len; ++k) ram[PPU_BUFFER + (uint8_t)(*w + 3 + k)] = 0x09;
    *w = (uint8_t)(*w + 3 + len);
}
/* The routines it commits. Player_SetMP ($FA85), A the MP: capped at 80,
 * stored, then UI_SetHUDBarValue's two commands, the bar (3 + width + 1
 * bytes) and the partial tile's pattern (3 + 16), each after
 * PPUBuffer_WaitForCapacity, which spins for ever in a committed call (the
 * call fails, nothing committed) when the buffer has no room. The registers
 * it returns with become the CPU's. */
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    if (routine == SOUND_PLAY_EFFECT) { sound = regs->a; return true; }
    CHECK(routine == PLAYER_SET_MP);
    ++bar_calls;
    uint8_t w = ram[PPU_BUFFER_WRITE];
    if (!has_capacity(w)) return false;
    if (!has_capacity((uint8_t)(w + 3 + ram[BAR_WIDTH] + 1))) return false;
    ram[PLAYER_MP] = regs->a > 80 ? 80 : regs->a;
    put(&w, (uint8_t)(ram[BAR_WIDTH] + 1), 0x2043);
    put(&w, 16, 0x10D0);
    ram[PPU_BUFFER_WRITE] = w;
    bar_mp = ram[PLAYER_MP];
    ++bars_queued;
    regs->a = 0x10; regs->x = w; regs->y = 0x08; regs->p = 0x26;   /* what the routine leaves */
    cpu = *regs;
    return true;
}

/* The engine's hook table: registration order, an enabled flag each. */
typedef struct { const char *id; uint16_t addr; NESModFunctionEntryCallback cb; int on; } Site;
static Site sites[16];
static int site_count;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (site_count == 16) return 0;
    sites[site_count++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < site_count; ++i)
        if (!strcmp(sites[i].id, id)) { sites[i].on = on; return 1; }
    return 0;
}
static NESModActivationCallback reset_cb, plugin_cb;
static const char *plugin_id;
int nes_mod_register_reset_callback(NESModActivationCallback cb) { reset_cb = cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugin_id = id; plugin_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) { (void)name; (void)required; }
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    if (strcmp(id, "faxanadu.recharge")) return 0;
    save_get = get; save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    if (strcmp(id, "faxanadu.recharge")) return 0;
    save_validate = validate;
    return 1;
}
/* Save files (fax_save.c): the file in play's fields, a small key-value list. */
static FaxSaveListener save_listener;
static bool file_in_play;
static struct { char key[FAX_SAVE_KEY], value[FAX_SAVE_VALUE]; } fields[8];
static int field_count;
bool fax_save_listen(FaxSaveListener listener) { save_listener = listener; return true; }
static const char *field(const char *key) {
    for (int i = 0; i < field_count; ++i) if (!strcmp(fields[i].key, key)) return fields[i].value;
    return NULL;
}
static void fields_clear(void) { field_count = 0; }
static void field_set(const char *key, const char *value) {
    int i = 0;
    while (i < field_count && strcmp(fields[i].key, key)) ++i;
    if (!value) { if (i < field_count) fields[i] = fields[--field_count]; return; }
    if (i == field_count) { CHECK(field_count < 8); snprintf(fields[field_count++].key, FAX_SAVE_KEY, "%s", key); }
    snprintf(fields[i].value, FAX_SAVE_VALUE, "%s", value);
}
const char *fax_save_get(const char *key) { return file_in_play ? field(key) : NULL; }
bool fax_save_set(const char *key, const char *value) {
    if (!file_in_play) return false;
    CHECK(!strncmp(key, "recharge.", 9));    /* its own fields only */
    field_set(key, value);
    return true;
}
long fax_save_get_int(const char *key, long fallback) {
    const char *v = fax_save_get(key);
    return v ? strtol(v, NULL, 10) : fallback;
}
bool fax_save_set_int(const char *key, long value) {
    char v[24];
    snprintf(v, sizeof v, "%ld", value);
    return fax_save_set(key, v);
}

/* Hook sites at `addr`, as the scheduler runs them: enabled callbacks in
 * order until one handles the call. 1 when the routine was handled. */
static int fire(uint16_t addr) {
    for (int i = 0; i < site_count; ++i)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return 1;
    return 0;
}
static void push(uint8_t v) { ram[0x100 | cpu.s] = v; cpu.s = (uint8_t)(cpu.s - 1); }
static uint8_t pull(void) { cpu.s = (uint8_t)(cpu.s + 1); return ram[0x100 | cpu.s]; }
static void jsr(uint16_t at) { uint16_t last = (uint16_t)(at + 2); push((uint8_t)(last >> 8)); push((uint8_t)last); }
static uint16_t rts(void) { uint8_t lo = pull(); return (uint16_t)((lo | pull() << 8) + 1); }
/* The site at `addr`, mid-routine: where the CPU goes on. `addr` itself if
 * nothing handled it; else where the RTS the engine does lands (the routine's
 * caller, or an address a hook pushed). */
static uint16_t site(uint16_t addr) { return fire(addr) ? rts() : addr; }

/* ---- the game, as the disassembly has it ---- */
enum { WING_BOOTS = 0x0F, HOUR_GLASS = 0x0D, RED_POTION = 0x10, MATTOCK = 0x09, NO_ITEM = 0xFF };
enum { HP_U = 0x0431, HP_L = 0x0432, PLAYER_IS_DEAD = 0x0438, DURATION_GLOVE = 0x0428, PLAYER_STATUS = 0x00A5,
       MUSIC = 0x00FA, AREA_MUSIC = 0x03D1 };
enum { SCRIPT_USED_HOUR_GLASS = 0x82, SCRIPT_USED_WING_BOOTS = 0x83, SCRIPT_USED_ELIXIR = 0x85,
       SCRIPT_GOT_ELIXIR = 0x86, SCRIPT_GOT_WING_BOOTS = 0x89, SCRIPT_GOT_HOUR_GLASS = 0x8A };
static const uint8_t STOCK_FLIGHT[BANDS] = { 40, 30, 20, 10 };    /* TITLE_TO_WINGBOOTS_DURATION ($C599) */
static int picture;                         /* the item the HUD's picture tiles show */
static int script;                          /* the last item script run */
static int gone;                            /* the last timed item that ran out ("... is gone") */
static unsigned gones;                      /* how many "... is gone" lines */
static char t_shown[3];                     /* T:'s two tiles, as the PPU has them */

static uint8_t selected(void) { return ram[SELECTED_ITEM]; }
static unsigned count(void) { return ram[NUMBER_OF_ITEMS]; }
static void give(const uint8_t *items, unsigned n, uint8_t selection) {
    memset(ram + ITEM_INVENTORY, 0xEE, 8);  /* stale bytes past the count */
    if (n) memcpy(ram + ITEM_INVENTORY, items, n);
    ram[NUMBER_OF_ITEMS] = (uint8_t)n;
    ram[SELECTED_ITEM] = selection;
    picture = selection;
}
static unsigned hp(void) { return ram[HP_U]; }
static unsigned mp(void) { return ram[PLAYER_MP]; }

/* The NMI: InterruptCounter, Input_HandleOnInterrupt's read of the pad into
 * Joy1_ButtonMask, then PPUBuffer_Draw empties the buffer. */
static void nmi(void) {
    ++ram[INTERRUPT_COUNTER];
    ram[JOY1_BUTTON_MASK] = pad;
    uint8_t r = ram[PPU_BUFFER_READ], w = ram[PPU_BUFFER_WRITE];
    while (r != w) {
        uint8_t len = ram[PPU_BUFFER + r], hi = ram[PPU_BUFFER + (uint8_t)(r + 1)], lo = ram[PPU_BUFFER + (uint8_t)(r + 2)];
        CHECK(len >= 1 && len <= 32);
        if (hi == 0x20 && lo == 0x58 && len == 2) {   /* T:'s digits */
            t_shown[0] = (char)ram[PPU_BUFFER + (uint8_t)(r + 3)];
            t_shown[1] = (char)ram[PPU_BUFFER + (uint8_t)(r + 4)];
        }
        r = (uint8_t)(r + 3 + len);
    }
    ram[PPU_BUFFER_READ] = r;
}
/* UI_DrawTimeValue: two ASCII digits at PPU $2058, through the buffer. */
static void draw_time(uint8_t v) {
    uint8_t w = ram[PPU_BUFFER_WRITE];
    const uint8_t bytes[5] = { 2, 0x20, 0x58, (uint8_t)('0' + v / 10 % 10), (uint8_t)('0' + v % 10) };
    for (unsigned k = 0; k < 5; ++k) ram[PPU_BUFFER + (uint8_t)(w + k)] = bytes[k];
    ram[PPU_BUFFER_WRITE] = (uint8_t)(w + 5);
}
static int t_value(void) {
    if (t_shown[0] < '0' || t_shown[0] > '9' || t_shown[1] < '0' || t_shown[1] > '9') return -1;
    return (t_shown[0] - '0') * 10 + t_shown[1] - '0';
}

/* The "... is gone" line: IScripts_Begin's text box, which plays its own frames. */
static void is_gone(int item) { gone = item; ++gones; }
/* GameLoop_CountdownItems, JSR'd from Game_MainLoop at $DB6B: its hook, then
 * Game_DecHourGlassDuration (JSR at $C88F), Game_DecWingBootsDuration ($C892), ... */
static void countdown(void) {
    cpu.s = 0xF8;
    jsr(0xDB6B);
    CHECK(!fire(GAME_LOOP_COUNTDOWN));      /* it only observes */
    bool second = (ram[INTERRUPT_COUNTER] & 0x3F) == 0;
    if (ram[DURATION_HOUR_GLASS] < 0x80) {  /* LDA DurationHourGlass, BMI @_return */
        jsr(0xC88F);
        uint16_t at = site(GLASS_SECOND);
        if (at == GLASS_SECOND) at = second ? GLASS_SECOND_DEC : rts();   /* AND #$3F, BNE @_return */
        if (at == GLASS_SECOND_DEC) {
            if (--ram[DURATION_HOUR_GLASS] >= 0x80) { is_gone(HOUR_GLASS); ram[MUSIC] = ram[AREA_MUSIC]; }
            at = rts();
        }
        CHECK(at == 0xC892);
    }
    if (ram[DURATION_WING_BOOTS] < 0x80) {
        ram[PLAYER_STATUS] |= 0x80;         /* ORA #$80: flying */
        jsr(0xC892);
        uint16_t at = site(BOOTS_SECOND);
        if (at == BOOTS_SECOND) at = second ? BOOTS_SECOND_DEC : rts();
        if (at == BOOTS_SECOND_DEC) {
            if (--ram[DURATION_WING_BOOTS] >= 0x80) { ram[PLAYER_STATUS] &= 0x7F; is_gone(WING_BOOTS); }
            else draw_time(ram[DURATION_WING_BOOTS]);
            at = rts();
        }
        CHECK(at == 0xC895);
    }
    CHECK(rts() == 0xDB6E && cpu.s == 0xF8);
}
static void frames(unsigned n) { while (n--) { nmi(); countdown(); } }
static void game_seconds(unsigned n) { frames(n * 64); }
/* Frames until `test` holds, at most `limit`; the count, or -1. */
static int frames_until(bool (*test)(void), int limit) {
    for (int n = 0; n <= limit; ++n) { if (test()) return n; frames(1); }
    return -1;
}

/* Player_ClearSelectedItem, from the JSR at `at` in an item's routine. */
static void clear_selected(uint16_t at) {
    jsr(at);
    if (!fire(FAX_PLAYER_CLEAR_SELECTED_ITEM)) {
        ram[SELECTED_ITEM] = NO_ITEM;
        picture = NO_ITEM;                  /* UI_ClearSelectedItemPic */
    }
    CHECK(rts() == at + 3);
}
/* Down-B: GameLoop_CheckUseCurrentItem (JSR'd from Game_MainLoop at $DB62)
 * RTSes into the selected item's routine, which returns to the main loop. */
static void use(void) {
    cpu.s = 0xF8; script = 0; sound = -1;
    jsr(0xDB62);
    switch (selected()) {
    case WING_BOOTS:                        /* Player_UseWingBoots */
        if (fire(PLAYER_USE_WING_BOOTS)) break;
        script = SCRIPT_USED_WING_BOOTS;
        clear_selected(0xC581);
        sound = 0x1A;
        cpu.a = STOCK_FLIGHT[ram[PLAYER_TITLE] >> 2];
        CHECK(!fire(WING_BOOTS_SET_FLIGHT));
        ram[DURATION_WING_BOOTS] = cpu.a;   /* STA DurationWingBoots */
        draw_time(cpu.a);                   /* JSR UI_DrawTimeValue */
        break;
    case HOUR_GLASS: {                      /* Player_UseHourGlass */
        if (fire(PLAYER_USE_HOUR_GLASS)) break;
        script = SCRIPT_USED_HOUR_GLASS;
        clear_selected(0xC5D0);
        sound = 0x1A;
        uint16_t at = site(HOUR_GLASS_COST);
        if (at == HOUR_GLASS_COST) {        /* LSR Player_HP_U, ROR Player_HP_L */
            ram[HP_L] = (uint8_t)(ram[HP_L] >> 1 | (ram[HP_U] & 1) << 7);
            ram[HP_U] >>= 1;
        } else {
            CHECK(at == HOUR_GLASS_DRAW_HP);
        }
        ram[DURATION_HOUR_GLASS] = 0x0F;    /* JSR UI_DrawPlayerHP, LDA #$0F, STA DurationHourGlass */
        ram[MUSIC] = 0x0B;
        break; }
    case RED_POTION:                        /* Player_UseRedPotion */
        clear_selected(0xC53B);
        ram[HP_U] = 0x50;
        break;
    case MATTOCK:                           /* Player_UseMattock, with a wall to break */
        clear_selected(0xC64C);
        break;
    }
    CHECK(rts() == 0xDB65 && cpu.s == 0xF8);
}
/* Player_CastMagic's Player_ReduceMP: the spell's cost, if MP covers it, and the bar. */
static void cast(uint8_t cost) { if (mp() >= cost) { ram[PLAYER_MP] = (uint8_t)(mp() - cost); bar_mp = mp(); } }
/* Player_ReduceHP taking the last of the hero's HP. */
static void hurt(void) {
    ram[HP_U] = 0; ram[HP_L] = 0;
    if (ram[SPECIAL_ITEMS] & BIT_ELIXIR) {  /* JMP Player_UseElixir */
        CHECK(!fire(PLAYER_USE_ELIXIR));
        ram[SPECIAL_ITEMS] &= (uint8_t)~BIT_ELIXIR;
        script = SCRIPT_USED_ELIXIR;
        ram[HP_U] = 0x50;                   /* Player_FillHPAndMP */
        ram[PLAYER_MP] = 0x50;
    } else {
        ram[PLAYER_IS_DEAD] = 1;
    }
}
/* Player_HandleDeath, ..., Player_Spawn: HP and MP to 80, then Game_ClearTimedItems. */
static void respawn(void) {
    ram[PLAYER_IS_DEAD] = 0;
    ram[HP_U] = 0x50;
    ram[PLAYER_MP] = 0x50;
    ram[PLAYER_STATUS] = 0;                 /* Game_InitStateForSpawn */
    CHECK(!fire(GAME_CLEAR_TIMED_ITEMS));
    for (uint16_t a = 0x0427; a <= DURATION_HOUR_GLASS; ++a) ram[a] = 0xFF;
}
/* A Guru travel journey: Player_Spawn entered past Game_ClearTimedItems, at
 * its JSR Game_SpawnInTemple. Nothing here for this feature. */
static void journey(void) { nmi(); }
/* IScripts_InvokeNextAction, about to run the action at `at`. */
static void next_action(uint16_t at) {
    ram[ISCRIPT_ADDR] = (uint8_t)at; ram[ISCRIPT_ADDR + 1] = (uint8_t)(at >> 8); ram[ISCRIPT_OFFSET] = 0;
    CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));   /* it never handles the call */
}

/* The interpreter, for whole conversations: what the box showed, in order
 * ("msg 22", the slot's text, "mantra", "end", or "B" for a line B ended). */
enum { NEXT_PLAYER_TITLE = 0x04ED };
enum { SPRING = 0xA1AB, QUESTS = 0x042D, QUEST_SPRING_OF_TRUNK = 0x01 };   /* the Spring of Trunk's script, Quests */
static char said[12][64];
static unsigned said_n;
static void note(const char *text) { CHECK(said_n < 12); snprintf(said[said_n++], sizeof said[0], "%s", text); }
/* MP from kills (fax_mana.c) fills HP and MP when Recharging items tells it a
 * Guru's conversation begins: here, how many times, and how many messages the
 * box had shown by the last. */
static unsigned guru_fills, said_at_fill;
void fax_mana_guru(void) { ++guru_fills; said_at_fill = said_n; }
static void (*on_line)(void);                 /* called as each message shows, for a test to act mid-script */
/* Run the script at `script` as IScripts_Begin starts it (IScriptOffset 1,
 * past its entity byte), firing IScripts_InvokeNextAction's hooks before
 * each action and going wherever they point it. B goes down on message
 * number `b_on` (from 1; 0 never), which ends the script if B may dismiss it
 * (03, not 01), as IScriptAction_ShowMessage jumps to IScriptAction_EndScript. */
static void run_from(uint16_t script, uint8_t offset, unsigned b_on) {
    said_n = 0;
    ram[ISCRIPT_ADDR] = (uint8_t)script; ram[ISCRIPT_ADDR + 1] = (uint8_t)(script >> 8); ram[ISCRIPT_OFFSET] = offset;
    unsigned messages = 0;
    for (int steps = 0; steps < 64; ++steps) {
        CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));
        uint16_t base = (uint16_t)(ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8), p = (uint16_t)(base + ram[ISCRIPT_OFFSET]);
        uint8_t op = cyc_mod_peek(p), arg = cyc_mod_peek((uint16_t)(p + 1));
        uint16_t to = (uint16_t)(arg | cyc_mod_peek((uint16_t)(p + 2)) << 8);                        /* 04, 17 */
        uint16_t if_held = (uint16_t)(cyc_mod_peek((uint16_t)(p + 2)) | cyc_mod_peek((uint16_t)(p + 3)) << 8);   /* 12 */
        unsigned len = 0;
        bool jump = false;
        switch (op) {
        case 0x00: note("end"); return;
        case 0x01: case 0x03: {
            char text[64];
            if (arg == SLOT_ID) slot_text(text); else snprintf(text, sizeof text, "msg %02X", arg);
            note(text);
            if (on_line) on_line();
            if (++messages == b_on && op == 0x03) { note("B"); return; }
            len = 2;
            break; }
        case 0x04:                                              /* CHECK_UPDATE_PLAYER_TITLE */
            if (ram[NEXT_PLAYER_TITLE] != ram[PLAYER_TITLE]) { ram[PLAYER_TITLE] = ram[NEXT_PLAYER_TITLE]; jump = true; }
            else len = 3;
            break;
        case 0x02: {                                            /* a question: A goes on (no B here) */
            char text[16];
            snprintf(text, sizeof text, "ask %02X", arg);
            note(text);
            len = 2;
            break; }
        case 0x06: if (arg > ram[0x0439]) ram[0x0439] = arg; len = 2; break;   /* only raises TempleSpawnPoint */
        case 0x07: ram[SPECIAL_ITEMS] |= arg; len = 2; break;  /* ADD_ITEM: the Ring of Elf ($80) here */
        case 0x0B:                                              /* IF_QUEST: ISCRIPTS_QUEST_BITS[arg - 1] */
            if (ram[QUESTS] & (1u << (arg - 1))) { to = if_held; jump = true; }
            else len = 4;
            break;
        case 0x0E: ram[QUESTS] |= (uint8_t)(1u << (arg - 1)); len = 2; break;   /* SET_QUEST */
        case 0x10:                                              /* CONSUME_ITEM: Player_RemoveItem; the Elixir's bit */
            CHECK(arg == 0x92);
            ram[SPECIAL_ITEMS] &= (uint8_t)~BIT_ELIXIR;
            len = 2;
            break;
        case 0x12:                                              /* CHECK_FOR_ITEM: the ring, or the Elixir, by bit */
            if ((arg == 0x80 && (ram[SPECIAL_ITEMS] & 0x80)) || (arg == 0x92 && (ram[SPECIAL_ITEMS] & BIT_ELIXIR))) {
                to = if_held;
                jump = true;
            } else {
                len = 4;
            }
            break;
        case 0x14: note("mantra"); len = 1; break;
        case 0x17: jump = true; break;
        default: fprintf(stderr, "unmodelled action %02X at $%04X\n", op, p); exit(1);
        }
        if (jump) { ram[ISCRIPT_ADDR] = (uint8_t)to; ram[ISCRIPT_ADDR + 1] = (uint8_t)(to >> 8); ram[ISCRIPT_OFFSET] = 0; }
        else ram[ISCRIPT_OFFSET] = (uint8_t)(ram[ISCRIPT_OFFSET] + len);
    }
    CHECK(!"the script did not end");
}
static void run_script(uint16_t script, unsigned b_on) { run_from(script, 1, b_on); }
static void expect(const char *const *lines) {
    unsigned n = 0;
    for (; lines[n]; ++n) {
        if (n >= said_n || strcmp(said[n], lines[n])) {
            fprintf(stderr, "said %u: \"%s\", expected \"%s\"\n", n, n < said_n ? said[n] : "", lines[n]);
            for (unsigned i = 0; i < said_n; ++i) fprintf(stderr, "  %s\n", said[i]);
            exit(1);
        }
        ++checks;
    }
    CHECK(n == said_n);
}
#define SAID(...) do { static const char *const lines_[] = { __VA_ARGS__, NULL }; expect(lines_); } while (0)
#define RESTORED "Your Elixir is/restored. Come/back when used."
#define CAN "If your Elixir/is used, a Guru/will restore it."

/* The Spring of Trunk's old man (ISCRIPT_SPRING_OF_TRUNK, $A1AB), as the ROM
 * has it: done, "find the poison in Mascon" ($5C); else his question ($59),
 * then with the Elixir "2 more springs" ($5A, unskippable), CONSUME_ITEM it and
 * SET_QUEST 1, and without it "Come back with the medicine." ($5B). */
static void spring_script(void) {
    static const uint8_t bytes[] = { 0x00, 0x0B, 0x01, 0xC0, 0xA1, 0x02, 0x59, 0x12, 0x92, 0xB9, 0xA1, 0x03, 0x5B,
                                     0x00, 0x01, 0x5A, 0x10, 0x92, 0x0E, 0x01, 0x00, 0x03, 0x5C, 0x00 };
    memcpy(bank12 + (SPRING - 0x8000), bytes, sizeof bytes);
}

/* The stock Gurus' scripts (bank 12), as the ROM has them. */
enum { EOLIS_GURU = 0xA0BC, APOLUNE_GURU = 0xA603, CONFLATE_GURU = 0xA627, OTHER_GURU = 0xA200, FRATERNAL_GURU = 0xA33C };
static void stock_scripts(void) {
    static const uint8_t common[] = { 0x03, 0x22, 0x14, 0x00, 0x01, 0x23, 0x00 };   /* $A5FC: mantra; $A600: new title */
    static const uint8_t eolis[] = { 0x81, 0x12, 0x80, 0xC6, 0xA0, 0x01, 0x2C, 0x07, 0x80, 0x00, 0x03, 0x2D, 0x00 };
    static const uint8_t conflate[] = { 0x81, 0x12, 0x23, 0x34, 0xA6, 0x06, 0x05, 0x04, 0x00, 0xA6, 0x17, 0xFC, 0xA5,
                                        0x12, 0x82, 0x2C, 0xA6, 0x01, 0x8C, 0x07, 0x82, 0x00 };
    static const uint8_t fraternal[] = { 0x81, 0x12, 0x45, 0x48, 0xA3, 0x03, 0x60, 0x00 };   /* no Dragon Slayer: a line */
    memcpy(bank12 + (0xA5FC - 0x8000), common, sizeof common);
    memcpy(bank12 + (EOLIS_GURU - 0x8000), eolis, sizeof eolis);
    memcpy(bank12 + (CONFLATE_GURU - 0x8000), conflate, sizeof conflate);
    memcpy(bank12 + (FRATERNAL_GURU - 0x8000), fraternal, sizeof fraternal);
}
/* A temple Guru's script at `at`: 06 temple, 04 NEW_RANK, 17 PASSWORD, as Apolune's ($A603) and four others. */
static void temple_script(uint16_t at, unsigned temple) {
    const uint8_t bytes[] = { 0x81, 0x06, (uint8_t)temple, 0x04, 0x00, 0xA6, 0x17, 0xFC, 0xA5 };
    memcpy(bank12 + (at - 0x8000), bytes, sizeof bytes);
}
/* Talking to a Guru: his whole script. Eolis's (0) checks for the Ring of
 * Elf; Conflate's (5) for the Battle Suit first; the rest set the spawn
 * point, then the mantra or a new title. No Save files or Guru travel steps:
 * the stock endings. */
static void meet_guru_b(unsigned temple, unsigned b_on) {
    stock_scripts();
    if (temple == 0) { run_script(EOLIS_GURU, b_on); return; }
    if (temple == 5) { run_script(CONFLATE_GURU, b_on); return; }
    temple_script(OTHER_GURU, temple);
    run_script(OTHER_GURU, b_on);
}
static void meet_guru(unsigned temple) { meet_guru_b(temple, 0); }
/* Player_PickUpWingBoots / Player_PickUpHourGlass: the script, then
 * JMP Player_PickUpItem with A the item, back to Player_HandleTouchItem. */
static void pick_up(uint8_t item) {
    script = item == WING_BOOTS ? SCRIPT_GOT_WING_BOOTS : SCRIPT_GOT_HOUR_GLASS;
    cpu.s = 0xF0;
    jsr(0x8920);
    cpu.a = item;
    if (!fire(PLAYER_PICK_UP_ITEM) && count() < MAX_ITEMS) {
        ram[ITEM_INVENTORY + count()] = item;
        ++ram[NUMBER_OF_ITEMS];
    }
    CHECK(rts() == 0x8923);
}
/* Player_PickUpElixir: the script, then SpecialItems |= the Elixir. */
static void pick_up_elixir(void) { script = SCRIPT_GOT_ELIXIR; ram[SPECIAL_ITEMS] |= BIT_ELIXIR; }
/* Game_ShowStartScreen: power-on or a reset, before a new game or a mantra. */
static void title_screen(void) { CHECK(!fire(GAME_SHOW_START_SCREEN)); }

static void play(bool on) {                 /* a launch with the toggle */
    reset_cb();
    memset(ram, 0, sizeof ram);
    for (uint16_t a = 0x0427; a <= DURATION_HOUR_GLASS; ++a) ram[a] = 0xFF;
    ram[HP_U] = 0x50;
    ram[PLAYER_MP] = 0x50;
    ram[BAR_WIDTH] = 10;                    /* UI_MPAndHPBarWidth, as UI_DrawHUDSprites sets it */
    ram[AREA_MUSIC] = 0x02;
    ram[INTERRUPT_COUNTER] = 1;
    give(NULL, 0, NO_ITEM);
    memcpy(t_shown, "00", 3);
    bar_mp = 0x50;
    bar_calls = bars_queued = gones = 0;
    gone = 0;
    isolated = false;
    overrides_lost();                       /* a launch: the engine's table starts empty */
    table_full = false;
    on_line = NULL;
    pad = 0;                                /* nothing held */
    if (on) plugin_cb();
    title_screen();
    respawn();                              /* Game_Start clears the timed items too */
}

static bool boots_down(void) { return ram[DURATION_WING_BOOTS] >= 0x80; }
static bool glass_down(void) { return ram[DURATION_HOUR_GLASS] >= 0x80; }
static bool boots_ready(void) { return ready(&charge, BOOTS); }
static bool glass_ready(void) { return ready(&charge, GLASS); }
static bool flying(void) { return ram[PLAYER_STATUS] & 0x80; }
/* The frames a flight's MP takes: one every FRAMES_PER_MP at the band. */
static const unsigned STEP[BANDS] = { 32, 40, 48, 64 };     /* 2, 1.6, 1.33 and 1 MP a second (#124) */
enum { GLASS_STEP = 24 };                                    /* 40 MP for the whole 15 s freeze */

/* ---- tests ---- */
static void registers(void) {
    CHECK(site_count == 13 && !strcmp(plugin_id, "faxanadu.recharge") && reset_cb == deactivate);
    CHECK(save_get && save_set && save_validate);
    play(true);
    for (int i = 0; i < site_count; ++i) CHECK(sites[i].on);
    play(false);
    for (int i = 0; i < site_count; ++i) CHECK(!sites[i].on);
}

/* A full bar flies 40, 50, 60 and 80 s by title band, an MP every 32, 40,
 * 48 and 64 frames to the frame, T: counting the seconds left (rounded up)
 * and the bar drawn at each MP. At 0 MP the game's own line ends it. */
static void flight_by_title_band(void) {
    static const unsigned flight[BANDS] = { 40, 50, 60, 80 };
    for (int title = 0; title < 16; ++title) {
        play(true);
        unsigned band = (unsigned)title / 4, step = STEP[band];
        ram[PLAYER_TITLE] = (uint8_t)title;
        give(NULL, 0, WING_BOOTS);
        use();
        pad = BUTTON_A;                     /* he flies the whole way */
        CHECK(script == SCRIPT_USED_WING_BOOTS && sound == 0x1A && charge.flight.running && flights == 1);
        CHECK(ram[DURATION_WING_BOOTS] == flight[band]);
        nmi();
        CHECK(t_value() == (int)flight[band]);   /* the game's own draw shows it */
        CHECK(selected() == WING_BOOTS && picture == WING_BOOTS && count() == 0);
        countdown();                        /* the flight's first frame */
        unsigned flown = 1;
        for (unsigned want = 79; want > 0; --want) {
            while (mp() > want) { frames(1); ++flown; CHECK(flown <= 80 * step); }
            CHECK(flown == (80 - want) * step && bar_mp == (int)want);    /* on schedule, the bar with it */
            CHECK(flying() && !boots_down());
            nmi(); countdown(); ++flown;    /* drawn at the next NMI */
            CHECK(t_value() == (int)((want * step - (flown - (80 - want) * step) + 63) / 64));
        }
        CHECK(frames_until(boots_down, (int)step) >= 0);
        CHECK(mp() == 0 && gone == WING_BOOTS && gones == 1 && !flying() && !charge.flight.running);
        nmi();
        CHECK(t_value() == 0);              /* T: back to the idle 00 */
        CHECK(charge.flight.next == 0);
    }
    /* Turned off: the original table, and the pair is used up. Its timer
     * runs on the game's seconds, A held or not. */
    for (int title = 0; title < 16; ++title) {
        play(false);
        ram[PLAYER_TITLE] = (uint8_t)title;
        give(NULL, 0, WING_BOOTS);
        use();
        CHECK(ram[DURATION_WING_BOOTS] == STOCK_FLIGHT[title / 4] && selected() == NO_ITEM && picture == NO_ITEM);
        uint8_t was = ram[PLAYER_MP];
        pad = title & 1 ? BUTTON_A : 0;
        game_seconds(5);
        CHECK(mp() == was);                 /* no MP taken */
        CHECK(ram[DURATION_WING_BOOTS] == STOCK_FLIGHT[title / 4] - 5);
    }
}

/* The exact length, from frames the game plays. */
static void a_flight_lasts_its_mp(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    ram[PLAYER_MP] = 20;                    /* 20 MP: 20 * 32 = 640 frames, 10 s */
    use();
    pad = BUTTON_A;
    CHECK(ram[DURATION_WING_BOOTS] == 10 && charge.flight.next == 32);
    int n = frames_until(boots_down, 1000);
    CHECK(n == 640 && mp() == 0 && gone == WING_BOOTS);
    /* With no MP: refused, the game's "invalid choice" sound, no line, nothing flies. */
    frames(10);
    use();
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && refused == 1 && boots_down() && !charge.flight.running);
    CHECK(selected() == WING_BOOTS && flights == 1);
    /* No cooldown: with MP again, they fly at once. */
    ram[PLAYER_MP] = 1;
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS && ram[DURATION_WING_BOOTS] == 1 && flights == 2);
    CHECK(frames_until(boots_down, 100) == 32 && mp() == 0);
    /* An odd MP and band: 7 MP at 1.6 a second, 280 frames: 5 s, rounded up. */
    play(true);
    ram[PLAYER_TITLE] = 5;
    ram[PLAYER_MP] = 7;
    give(NULL, 0, WING_BOOTS);
    use();
    pad = BUTTON_A;
    CHECK(ram[DURATION_WING_BOOTS] == 5);
    frames(41);                             /* 239 left: 4 s */
    CHECK(ram[DURATION_WING_BOOTS] == 4 && mp() == 6);
    nmi();
    CHECK(t_value() == 4);
    CHECK(frames_until(boots_down, 400) == 239);
}

/* The Wing Boots take MP only while A is held (#148), A being what flies
 * him: with them on, walking, standing and falling take none and T: holds,
 * and each frame with A held counts toward the next MP at the band's rate.
 * They stay on until MP runs out (however it went), Down+B or a death, and
 * the Hour Glass still works every frame. */
static void only_while_a_is_held(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    nmi();
    CHECK(t_value() == 40 && charge.flight.next == 32 && ram[DURATION_WING_BOOTS] == 40);
    frames(1000);                           /* walking, standing, falling: A up */
    nmi();
    CHECK(mp() == 80 && charge.flight.next == 32 && ram[DURATION_WING_BOOTS] == 40 && t_value() == 40);
    CHECK(flying() && charge.flight.running && !boots_ready() && rest_frames == 1000 && lift_frames == 0 && !lifting);
    pad = BUTTON_A;                         /* he flies */
    frames(31);
    CHECK(mp() == 80 && charge.flight.next == 1 && lifting && lift_frames == 31);
    frames(1);
    CHECK(mp() == 79 && bar_mp == 79 && charge.flight.next == 32);
    frames(20);                             /* 12 frames of A to the next MP */
    pad = 0;                                /* let go: he falls, then stands */
    frames(500);
    nmi();
    unsigned left = 79 * 32 - 20;           /* frames of flight left: 40 s */
    CHECK(mp() == 79 && charge.flight.next == 12 && ram[DURATION_WING_BOOTS] == (left + 63) / 64);
    CHECK(t_value() == (int)((left + 63) / 64) && rest_frames == 1500 && lift_frames == 52);
    pad = BUTTON_A;
    frames(11);
    CHECK(mp() == 79);
    frames(1);
    CHECK(mp() == 78 && charge.flight.next == 32);
    /* A frame of A, a frame without: 64 frames, 32 of them flown, one MP. */
    for (unsigned k = 0; k < 64; ++k) { pad = k & 1 ? BUTTON_A : 0; frames(1); }
    CHECK(mp() == 77 && charge.flight.next == 32);
    /* Each band's rate, on frames of A alone, in pieces with rests between. */
    for (unsigned band = 0; band < BANDS; ++band) {
        play(true);
        ram[PLAYER_TITLE] = (uint8_t)(band * 4 + 1);
        give(NULL, 0, WING_BOOTS);
        use();
        for (unsigned held = 0; held < 3 * STEP[band]; ) {
            pad = BUTTON_A;
            for (unsigned k = 0; k < 7; ++k) { frames(1); ++held; CHECK(mp() == 80 - held / STEP[band]); }
            pad = 0;
            frames(13);
            CHECK(mp() == 80 - held / STEP[band] && charge.flight.running);
        }
    }
    /* Out of MP in flight: the game's own line ends them, as before. */
    play(true);
    give(NULL, 0, WING_BOOTS);
    ram[PLAYER_MP] = 2;
    use();
    frames(300);
    CHECK(mp() == 2 && !boots_down() && charge.flight.running && ram[DURATION_WING_BOOTS] == 1);
    pad = BUTTON_A;
    CHECK(frames_until(boots_down, 200) == 64 && mp() == 0 && gone == WING_BOOTS && gones == 1 && !flying());
    nmi();
    CHECK(t_value() == 0 && boots_ready() && landings[BOOTS] == 0);
    /* MP spent with A up, by a spell: at 0 MP they end all the same, at the next frame. */
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    frames(10);
    for (int k = 0; k < 5; ++k) cast(16);
    CHECK(mp() == 0 && !boots_down());
    frames(1);
    CHECK(boots_down() && gone == WING_BOOTS && !flying() && landings[BOOTS] == 0);
    /* Down+B standing, A up: they end at this frame's countdown, no MP taken. */
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    frames(100);
    use();
    frames(1);
    CHECK(boots_down() && gone == WING_BOOTS && mp() == 80 && landings[BOOTS] == 1);
    /* The Hour Glass works every frame: with the boots on and A up, only it
     * drains, and the flight's step holds. */
    play(true);
    const uint8_t items[] = { HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    ram[PLAYER_MP] = 20;
    use();
    ram[SELECTED_ITEM] = HOUR_GLASS; ram[ITEM_INVENTORY] = WING_BOOTS;
    use();
    frames(48);
    CHECK(mp() == 18 && charge.flight.next == 32 && charge.freeze.played == 48);
    /* T: on the glass counts the flight's drain as if A were held: the
     * freeze never ends sooner than it says. */
    unsigned shown = freeze_left(&charge, mp(), 0);
    CHECK(shown == to_empty(&charge, mp(), 0) && shown < 18 * GLASS_STEP);
    int n = frames_until(glass_down, 1000);
    CHECK(n == 18 * GLASS_STEP && mp() == 0 && (unsigned)n > shown && boots_down() && gones == 2);
}

/* MP that changes in the air changes the flight: a spell, the Elixir. */
static void the_flight_follows_mp(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    pad = BUTTON_A;
    frames(100);                            /* 3 MP gone: 77, next in 28 */
    CHECK(mp() == 77 && charge.flight.next == 28 && ram[DURATION_WING_BOOTS] == 39);
    cast(16);                               /* Tilte: 61 MP, 60 * 32 + 28 frames */
    frames(1);
    CHECK(ram[DURATION_WING_BOOTS] == (60 * 32 + 27 + 63) / 64);
    nmi();
    CHECK(t_value() == 31);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();                                 /* revived: MP full again */
    frames(1);
    CHECK(ram[DURATION_WING_BOOTS] == 40 && charge.flight.running);
    /* A title won mid-flight: the next MP at the new band's step. */
    ram[PLAYER_MP] = 3;
    ram[PLAYER_TITLE] = 4;
    unsigned next = charge.flight.next;
    frames(next);
    CHECK(mp() == 2 && charge.flight.next == 40);
}

/* Down+B mid-flight ends it at this frame's countdown with the game's own
 * line, no MP more; and again at once, it flies again (no cooldown). */
static void using_them_again_lands(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    pad = BUTTON_A;                         /* in the air, so Down+B can reach the item */
    frames(70);
    uint8_t left = ram[PLAYER_MP];
    use();
    CHECK(script == 0 && sound == -1 && charge.flight.land && !boots_down());   /* no line yet: the countdown's */
    frames(1);
    CHECK(boots_down() && gone == WING_BOOTS && !flying() && mp() == left && landings[BOOTS] == 1);
    CHECK(!charge.flight.running && boots_ready());
    nmi();
    CHECK(t_value() == 0);
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS && ram[DURATION_WING_BOOTS] == (left * 32 + 63) / 64 && flights == 2);
    frames(64);
    CHECK(mp() == left - 2u);
}

/* Each MP's bar is Player_SetMP's, in a committed call: a full PPU buffer
 * holds the bar, never the MP, and the CPU gets its registers back. */
static void the_bar_waits_for_room(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    pad = BUTTON_A;
    frames(31);
    CHECK(mp() == 80 && bar_calls == 0);
    nmi();
    ram[PPU_BUFFER_READ] = 0x30; ram[PPU_BUFFER_WRITE] = 0x00;    /* 48 bytes free: T: fits, the bar does not */
    cpu.a = 0x11; cpu.x = 0x22; cpu.y = 0x33; cpu.p = 0x04;
    countdown();
    CHECK(mp() == 79 && bar_mp == 80 && charge.bar && bar_calls == 0);   /* not even tried */
    CHECK(cpu.a == 0x11 && cpu.x == 0x22 && cpu.y == 0x33 && cpu.p == 0x04);
    ram[PPU_BUFFER_READ] = ram[PPU_BUFFER_WRITE];                  /* drained */
    countdown();
    CHECK(bar_mp == 79 && !charge.bar && bars_queued == 1);
    frames(31);
    CHECK(mp() == 78 && bar_mp == 78);
}

/* The Hour Glass: the game's 15 s freeze, no HP, and an MP every 24 frames:
 * 40 for the whole freeze. */
static void the_freeze_runs_on_mp(void) {
    play(true);
    give(NULL, 0, HOUR_GLASS);
    ram[HP_L] = 0x80;                       /* 80.5 HP: the original took half */
    use();
    CHECK(script == SCRIPT_USED_HOUR_GLASS && ram[DURATION_HOUR_GLASS] == 15 && hp() == 80 && ram[HP_L] == 0x80);
    CHECK(selected() == HOUR_GLASS && picture == HOUR_GLASS && freezes == 1 && ram[MUSIC] == 0x0B);
    frames(1);
    nmi();
    CHECK(t_value() == 15);                 /* selected: its seconds */
    unsigned played = 1;
    for (unsigned want = 79; want >= 41; --want) {
        while (mp() > want) { frames(1); ++played; CHECK(played <= FREEZE); }
        CHECK(played == (80 - want) * GLASS_STEP && bar_mp == (int)want && !glass_down());
    }
    CHECK(frames_until(glass_down, 30) == 24);
    played += 24;
    CHECK(played == 960 && mp() == 40 && gone == HOUR_GLASS && gones == 1 && ram[MUSIC] == 0x02);
    CHECK(hp() == 80 && ram[HP_L] == 0x80 && !charge.freeze.running && glass_ready());
    nmi();
    CHECK(t_value() == 0);
    /* No cooldown: again at once. */
    use();
    CHECK(script == SCRIPT_USED_HOUR_GLASS && ram[DURATION_HOUR_GLASS] == 15 && freezes == 2);
    CHECK(frames_until(glass_down, 1000) == 960 && mp() == 0);
    /* With no MP: refused with the sound. */
    use();
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && glass_down() && refused == 1);
    /* Less MP than the freeze costs: it ends at 0 MP, 10 MP in 240 frames, T: 4 s. */
    ram[PLAYER_MP] = 10;
    use();
    frames(1);
    nmi();
    CHECK(t_value() == 4 && ram[DURATION_HOUR_GLASS] == 4);
    CHECK(frames_until(glass_down, 1000) == 239 && mp() == 0 && gones == 3);
    /* Turned off: the original's half of HP, and the glass used up, no MP. */
    play(false);
    give(NULL, 0, HOUR_GLASS);
    use();
    CHECK(hp() == 40 && selected() == NO_ITEM && ram[DURATION_HOUR_GLASS] == 15);
    game_seconds(5);
    CHECK(mp() == 80);
}

/* Down+B on it again ends the freeze at once; and on a boss's screen
 * LimitItemDurations cuts it: no more MP, and the game's second ends it. */
static void the_freeze_ends_early(void) {
    play(true);
    give(NULL, 0, HOUR_GLASS);
    use();
    frames(100);
    uint8_t left = ram[PLAYER_MP];
    CHECK(left == 80 - 4);
    use();
    CHECK(script == 0 && sound == -1 && !glass_down());
    frames(1);
    CHECK(glass_down() && gone == HOUR_GLASS && mp() == left && landings[GLASS] == 1 && ram[MUSIC] == 0x02);
    frames(200);
    CHECK(mp() == left);
    use();
    CHECK(script == SCRIPT_USED_HOUR_GLASS && freezes == 2);
    /* LimitItemDurations, the first frame on a boss's screen. */
    frames(200);
    left = ram[PLAYER_MP];
    ram[DURATION_HOUR_GLASS] = 0;
    gones = 0;
    int n = frames_until(glass_down, 70);
    CHECK(n >= 1 && n <= 65 && gone == HOUR_GLASS && gones == 1 && cuts == 1 && mp() == left);
}

/* Both at once drain together; each ends when MP runs out. */
static void both_at_once(void) {
    play(true);
    const uint8_t items[] = { HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    ram[PLAYER_MP] = 30;
    use();
    pad = BUTTON_A;
    frames(10);
    ram[SELECTED_ITEM] = HOUR_GLASS; ram[ITEM_INVENTORY] = WING_BOOTS;
    use();
    CHECK(charge.flight.running && charge.freeze.running);
    /* From here the flight's MP go at 22, 54, ... 406 (13 of them) and the
     * glass's at 24, 48, ... 408 (17): the 30th, the glass's, at 408. */
    unsigned left = to_empty(&charge, mp(), 0);
    CHECK(left == 408 && left == freeze_left(&charge, mp(), 0) && left == flight_left(&charge, mp(), 0));
    frames(1);
    nmi();
    CHECK(t_value() == (int)((left - 1 + 63) / 64));   /* the glass is selected: its seconds */
    ram[SELECTED_ITEM] = RED_POTION;
    frames(1);
    nmi();
    CHECK(t_value() == (int)((left - 2 + 63) / 64));   /* the flight's */
    int n = frames_until(boots_down, 1000);
    CHECK(n == (int)left - 2 && mp() == 0);
    frames(1);
    CHECK(glass_down() && gones == 2);
}

/* T: shows the selected item's seconds while it runs; else the original's:
 * the flight's while the boots fly, else 00 (#119). */
static void hud_shows_the_selected_item(void) {
    play(true);
    Charge c = { 0 };
    CHECK(hud_value(&c, WING_BOOTS, 80, 0) == 0 && hud_value(&c, HOUR_GLASS, 80, 0) == 0);
    c.freeze = freeze_begin();
    CHECK(hud_value(&c, HOUR_GLASS, 80, 0) == 15 && hud_value(&c, WING_BOOTS, 80, 0) == 0);
    CHECK(hud_value(&c, RED_POTION, 80, 0) == 0 && hud_value(&c, NO_ITEM, 80, 0) == 0);
    c.flight = take_off(0);
    c.freeze = (Freeze){ 0 };
    CHECK(hud_value(&c, WING_BOOTS, 80, 0) == 40 && hud_value(&c, RED_POTION, 80, 0) == 40);
    CHECK(hud_value(&c, HOUR_GLASS, 80, 0) == 40);   /* the glass not running: the flight's, as the original */
    CHECK(hud_value(&c, WING_BOOTS, 80, 12) == 80 && hud_value(&c, WING_BOOTS, 1, 0) == 1);
    CHECK(hud_value(&c, WING_BOOTS, 0, 0) == 0);
    /* On the screen: the game draws 00 over T: (UI_DrawHUD after a box or a
     * screen); within 16 frames it shows the flight's seconds again. */
    give(NULL, 0, WING_BOOTS);
    use();
    frames(2);
    draw_time(0);
    nmi();
    CHECK(t_value() == 0);
    for (int k = 0; k < 17 && t_value() != 40; ++k) frames(1);
    CHECK(t_value() == 40);
    /* A full PPU buffer: T: waits for room, as the game's own draws do. */
    ram[PPU_BUFFER_READ] = 0x10; ram[PPU_BUFFER_WRITE] = 0x00;        /* 16 bytes free: too few */
    drawn_valid = false;
    tick_hook(GAME_LOOP_COUNTDOWN);
    CHECK(ram[PPU_BUFFER_WRITE] == 0x00);
    ram[PPU_BUFFER_READ] = 0x00;                                       /* drained */
    tick_hook(GAME_LOOP_COUNTDOWN);
    CHECK(ram[PPU_BUFFER_WRITE] == 5);
    /* Wrapping round the 256-byte ring. */
    nmi();
    ram[PPU_BUFFER_READ] = ram[PPU_BUFFER_WRITE] = 0xFE;
    drawn_valid = false;
    countdown(); nmi();
    CHECK(ram[PPU_BUFFER_WRITE] == 0x03 && t_value() == (int)seconds(flight_left(&charge, mp(), 0)));
}

static void dying_ends_both(void) {
    play(true);
    const uint8_t items[] = { HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    use();
    game_seconds(5);
    respawn();                              /* dies mid-flight */
    frames(2);
    CHECK(boots_ready() && boots_down() && !charge.flight.running && mp() == 80);
    nmi();
    CHECK(t_value() == 0);
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS);
    ram[SELECTED_ITEM] = HOUR_GLASS;
    use();
    frames(10);
    respawn();                              /* dies with both running */
    CHECK(boots_ready() && glass_ready() && spawns >= 2);
    frames(64);
    CHECK(mp() == 80);
}

static void guru_travel_keeps_them(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    pad = BUTTON_A;
    game_seconds(3);
    uint8_t flying_left = ram[DURATION_WING_BOOTS], had = ram[PLAYER_MP];
    journey();
    meet_guru(3);                           /* talking to a Guru changes nothing */
    CHECK(charge.flight.running && ram[DURATION_WING_BOOTS] == flying_left && mp() == had);
    game_seconds(1);
    CHECK(mp() == had - 2u && charge.flight.running);
}

/* What the status bar is told (fax_charges.c draws the Elixir's): not his,
 * ready, or used. */
static FaxCharge boots_c(void) { return fax_recharge_charge(FAX_RECHARGE_BOOTS); }
static FaxCharge glass_c(void) { return fax_recharge_charge(FAX_RECHARGE_GLASS); }
static FaxCharge elixir_c(void) { return fax_recharge_charge(FAX_RECHARGE_ELIXIR); }
static void charges_on_the_status_bar(void) {
    play(true);
    CHECK(boots_c() == FAX_CHARGE_NONE && glass_c() == FAX_CHARGE_NONE && elixir_c() == FAX_CHARGE_NONE);
    CHECK(fax_recharge_charge(-1) == FAX_CHARGE_NONE && fax_recharge_charge(FAX_RECHARGE_ITEMS) == FAX_CHARGE_NONE);
    /* His: selected, or in the list's first NumberOfItems slots, not past them. */
    const uint8_t items[] = { RED_POTION, HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    ram[ITEM_INVENTORY + 1] = HOUR_GLASS;   /* a stale byte past the count */
    CHECK(boots_c() == FAX_CHARGE_READY && glass_c() == FAX_CHARGE_NONE);
    give(items, 2, WING_BOOTS);
    CHECK(boots_c() == FAX_CHARGE_READY && glass_c() == FAX_CHARGE_READY);
    /* Used while it runs, ready the moment it ends. */
    use();
    CHECK(boots_c() == FAX_CHARGE_USED && glass_c() == FAX_CHARGE_READY);
    use();
    frames(1);
    CHECK(boots_c() == FAX_CHARGE_READY);
    give((const uint8_t[]){ WING_BOOTS }, 1, HOUR_GLASS);
    use();
    CHECK(glass_c() == FAX_CHARGE_USED && boots_c() == FAX_CHARGE_READY);
    respawn();
    CHECK(glass_c() == FAX_CHARGE_READY);
    /* The Elixir: charged, spent by a revive, charged by a Guru, gone to the Spring of Trunk. */
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    CHECK(elixir_c() == FAX_CHARGE_READY);
    hurt();
    CHECK(elixir_c() == FAX_CHARGE_USED);
    respawn();
    CHECK(elixir_c() == FAX_CHARGE_USED);
    meet_guru(0);
    CHECK(elixir_c() == FAX_CHARGE_READY);
    hurt();
    pick_up_elixir();                       /* found while spent: ready at once, before tick_hook forgets it was spent */
    CHECK(charge.elixir_spent && elixir_c() == FAX_CHARGE_READY);
    frames(1);
    CHECK(!charge.elixir_spent && elixir_c() == FAX_CHARGE_READY);
    ram[SPECIAL_ITEMS] &= (uint8_t)~BIT_ELIXIR;   /* the Spring of Trunk's CONSUME_ITEM */
    frames(1);
    CHECK(elixir_c() == FAX_CHARGE_NONE);
    /* Turned off: nothing, whatever he holds. */
    play(false);
    give(items, 2, WING_BOOTS);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    CHECK(boots_c() == FAX_CHARGE_NONE && glass_c() == FAX_CHARGE_NONE && elixir_c() == FAX_CHARGE_NONE);
}

/* A pair or an Hour Glass he owns already is not added again. */
static void finds_add_nothing(void) {
    play(true);
    const uint8_t items[] = { RED_POTION, HOUR_GLASS };
    give(items, 2, WING_BOOTS);
    pick_up(WING_BOOTS);                    /* a pair in the world */
    CHECK(count() == 2 && pickups == 1 && script == SCRIPT_GOT_WING_BOOTS);
    pick_up(HOUR_GLASS);                    /* in the list, unselected */
    CHECK(count() == 2 && pickups == 2);
    /* Found while flying: the flight goes on. */
    use();
    frames(10);
    pick_up(WING_BOOTS);
    CHECK(charge.flight.running && !boots_down() && count() == 2);
    /* Not owned: the original pickup, into the list. */
    give(NULL, 0, RED_POTION);
    pick_up(WING_BOOTS);
    CHECK(count() == 1 && ram[ITEM_INVENTORY] == WING_BOOTS && pickups == 3);
    /* A full list holding one: nothing added, nothing lost. */
    const uint8_t full[] = { WING_BOOTS, 1, 2, 3, 4, 5, 6, 7 };
    give(full, 8, RED_POTION);
    pick_up(WING_BOOTS);
    CHECK(count() == 8 && pickups == 4);
    /* Turned off: the original, a second copy. */
    play(false);
    const uint8_t one[] = { WING_BOOTS };
    give(one, 1, NO_ITEM);
    pick_up(WING_BOOTS);
    CHECK(count() == 2);
}

static void other_items_untouched(void) {
    play(true);
    const uint8_t items[] = { WING_BOOTS };
    give(items, 1, RED_POTION);
    ram[HP_U] = 10;
    use();
    CHECK(selected() == NO_ITEM && picture == NO_ITEM && hp() == 80);   /* the Red Potion is used up */
    give(items, 1, MATTOCK);
    use();
    CHECK(selected() == NO_ITEM);           /* reusable tools keeps the Mattock, not this feature */
    CHECK(kept == 0);
    /* Player_ClearSelectedItem from any other caller. */
    give(items, 1, WING_BOOTS);
    cpu.s = 0xF0;
    clear_selected(0xE123);
    CHECK(selected() == NO_ITEM);
}

static void new_game_and_mantra(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    give(NULL, 0, WING_BOOTS);
    use();
    title_screen();                         /* a reset, then a new game or a mantra */
    respawn();
    CHECK(boots_ready() && glass_ready() && !charge.elixir_spent);
    meet_guru(0);                           /* a mantra holds only a charged Elixir */
    CHECK(!(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
}

static void isolated_runs_change_nothing(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    frames(31);
    isolated = true;                        /* Seamless simulating another room */
    uint8_t had = ram[PLAYER_MP], next = charge.flight.next;
    tick_hook(GAME_LOOP_COUNTDOWN);
    CHECK(!boots_second_hook(BOOTS_SECOND) && !glass_second_hook(GLASS_SECOND));
    CHECK(mp() == had && charge.flight.next == next);
    respawn();
    pick_up(WING_BOOTS);
    CHECK(charge.flight.running && count() == 1);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    meet_guru(0);
    CHECK(!charge.elixir_spent);
    isolated = false;
}

static void save_states(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    const uint8_t items[] = { HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    use();
    pad = BUTTON_A;
    frames(45);
    ram[SELECTED_ITEM] = HOUR_GLASS; ram[ITEM_INVENTORY] = WING_BOOTS;
    use();
    frames(30);
    uint8_t buf[32];
    int n = save_get(buf, sizeof buf);
    CHECK(n == RECORD && save_validate(buf, n));
    CHECK(buf[0] == 3 && buf[1] == 1 && buf[2] == charge.flight.next && buf[3] == 0 && buf[4] == 1 && buf[5] == 0);
    CHECK((buf[6] | buf[7] << 8) == 30 && buf[8] == charge.freeze.next && buf[9] == 15 && buf[11] == 1);
    CHECK(buf[12] == 0 && buf[13] == TALK_IDLE && buf[14] == LINE_NONE);
    CHECK(save_get(buf, RECORD - 1) == -1);
    Charge saved = charge;
    uint8_t machine[0x800];
    memcpy(machine, ram, sizeof ram);
    int boots_end = frames_until(boots_down, 4000), had = (int)mp();
    /* Loaded into a new launch: the same, frame for frame. */
    play(true);
    memcpy(ram, machine, sizeof ram);
    pad = BUTTON_A;
    CHECK(save_set(buf, n) == 1 && !memcmp(&charge, &saved, sizeof charge));
    fax_recharge_loaded();
    CHECK(!memcmp(&charge, &saved, sizeof charge));
    CHECK(frames_until(boots_down, 4000) == boots_end && (int)mp() == had);
    /* A state from before the feature: nothing running, nothing spent. */
    CHECK(save_validate(buf, 0) && save_set(buf, 0) == 1 && boots_ready() && glass_ready() && !charge.elixir_spent);
    /* Records it does not accept change nothing. */
    CHECK(save_set(buf, n) == 1);
    static const struct { int at; uint8_t value; } bad[] = {
        { 0, 4 }, { 0, 2 }, { 1, 2 }, { 2, 0 }, { 2, 65 }, { 3, 2 }, { 4, 2 }, { 5, 2 }, { 7, 4 }, { 8, 0 },
        { 8, 25 }, { 9, 16 }, { 10, 2 }, { 11, 2 }, { 12, 2 }, { 13, TALK_PHASES }, { 14, LINES },
        { 14, LINE_CAN } };                 /* the last: a line with no talk */
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint8_t b[RECORD];
        memcpy(b, buf, sizeof b);
        b[bad[i].at] = bad[i].value;
        CHECK(!save_validate(b, sizeof b) && !save_set(b, sizeof b));
        CHECK(!memcmp(&charge, &saved, sizeof charge));
    }
    CHECK(!save_validate(buf, RECORD - 1) && !save_validate(buf, RECORD + 1));
}

/* Records from before #124 (version 2, and version 1 before the Guru's
 * lines): their cooldowns are dropped, and a run on the game's timer goes on
 * on MP, the freeze for the seconds it had left. */
static void old_records_load(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    ram[DURATION_WING_BOOTS] = 25; ram[DURATION_HOUR_GLASS] = 7;   /* both running, as the game timed them */
    ram[PLAYER_STATUS] = 0x80;
    pad = BUTTON_A;                         /* flying */
    /* Version 2: boots running, 0 cooldown; glass running; the Elixir spent; no continued file; told; no line. */
    const uint8_t v2[RECORD_V2] = { 2, 1, 0, 1, 0, 1, 0, 0, 0, 1, TALK_IDLE, LINE_NONE, 0, 0, 0, 0, 0 };
    CHECK(save_validate(v2, RECORD_V2) && save_set(v2, RECORD_V2) == 1);
    fax_recharge_loaded();
    CHECK(charge.flight.running && charge.flight.next == 32 && charge.freeze.running && charge.elixir_spent);
    CHECK(charge.elixir_told && charge.freeze.played == FREEZE - 7 * 64 && charge.freeze.shown == 7);
    CHECK(frames_until(glass_down, 1000) == 7 * 64 && gone == HOUR_GLASS && charge.flight.running);
    CHECK(mp() == 80 - 7 * 64 / 32 - 7 * 64 / 24);
    /* Version 1, the glass's last partial second, the boots' cooldown: no flight. */
    play(true);
    ram[DURATION_HOUR_GLASS] = 0;
    const uint8_t v1[RECORD_V1] = { 1, 0, 40, 1, 0, 0, 1, 30, 30 };
    CHECK(save_validate(v1, RECORD_V1) && save_set(v1, RECORD_V1) == 1);
    fax_recharge_loaded();
    CHECK(!charge.flight.running && boots_ready() && charge.freeze.running && !charge.elixir_told);
    CHECK(frames_until(glass_down, 10) == 1 && gone == HOUR_GLASS);
    /* One the game had already ended is over. */
    play(true);
    const uint8_t done[RECORD_V2] = { 2, 1, 0, 1, 0, 0, 0, 0, 0, 0, TALK_IDLE, LINE_NONE, 0, 0, 0, 0, 0 };
    CHECK(save_set(done, RECORD_V2) == 1);
    fax_recharge_loaded();
    CHECK(boots_ready() && glass_ready());
    /* Old records it never accepted, it still does not. */
    uint8_t b[RECORD_V2];
    memcpy(b, v2, sizeof b); b[2] = 100; CHECK(!save_validate(b, RECORD_V2));
    memcpy(b, v2, sizeof b); b[0] = 1;   CHECK(!save_validate(b, RECORD_V2));
    memcpy(b, v2, sizeof b); b[11] = LINE_CAN; CHECK(!save_validate(b, RECORD_V2));
    CHECK(!save_validate(v1, RECORD_V2) && !save_validate(v2, RECORD_V1));
}

/* Save files: the file in play keeps the Elixir's charge; nothing of the
 * timed items, and #27's cooldown fields are dropped. */
static void save_files(void) {
    CHECK(save_listener == on_save);
    play(true);
    file_in_play = true;
    fields_clear();
    field_set("recharge.wingboots", "45");  /* a file from #27 */
    field_set("recharge.hourglass", "30");
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    give(NULL, 0, WING_BOOTS);
    use();
    frames(100);
    save_listener(FAX_SAVE_WRITING);        /* a Guru's save, mid-flight */
    CHECK(!field("recharge.wingboots") && !field("recharge.hourglass") && !strcmp(field("recharge.elixir"), "spent"));
    /* Quit, then continue the file: nothing runs, the Elixir is spent. */
    field_set("recharge.wingboots", "45");  /* read no more */
    title_screen();
    CHECK(boots_ready() && !charge.elixir_spent);
    save_listener(FAX_SAVE_CONTINUED);
    respawn();
    CHECK(boots_ready() && glass_ready() && charge.elixir_spent && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS);   /* no cooldown came back with the file */
    meet_guru(0);
    CHECK(!charge.elixir_spent && (ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("recharge.elixir") == NULL && !field("recharge.wingboots"));   /* charged: special items say so */
    /* A new game resets it. */
    charge.elixir_spent = 1;
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!charge.elixir_spent);
    /* Turned off, it writes nothing and reads nothing. */
    play(false);
    fields_clear();
    field_set("recharge.wingboots", "45");
    save_listener(FAX_SAVE_WRITING);
    CHECK(!strcmp(field("recharge.wingboots"), "45") && field("recharge.elixir") == NULL);
    file_in_play = false;
}

static void elixir(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;  /* and the Ring of Elf */
    meet_guru(0);                           /* charged: nothing to do */
    CHECK(guru_recharges == 0 && ram[SPECIAL_ITEMS] == (BIT_ELIXIR | 0x80));
    hurt();
    CHECK(script == SCRIPT_USED_ELIXIR && hp() == 80 && !ram[PLAYER_IS_DEAD]);
    CHECK(charge.elixir_spent && revives == 1 && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    /* Spent, it saves no one: the next 0 HP kills him. */
    hurt();
    CHECK(ram[PLAYER_IS_DEAD] == 1);
    respawn();
    game_seconds(400);                         /* no timer, and no recharge on death */
    CHECK(charge.elixir_spent && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    /* Anyone else's script, a Guru's later actions, or a bad spawn point: nothing. */
    static const uint8_t npc[] = { 0x03, 0x40, 0x00, 0x06, 0x09, 0x00 };
    memcpy(bank12 + (0xA300 - 0x8000), npc, sizeof npc);
    next_action(0xA300);
    next_action(0xA302);
    next_action(0xA303);                    /* 06 with no temple 9 */
    next_action(0xA0C6);
    CHECK(charge.elixir_spent && guru_recharges == 0);
    meet_guru(0);                           /* Eolis's Guru, or any other */
    CHECK(!charge.elixir_spent && guru_recharges == 1 && ram[SPECIAL_ITEMS] == (BIT_ELIXIR | 0x80));
    hurt();
    CHECK(!ram[PLAYER_IS_DEAD] && charge.elixir_spent && revives == 2);
    meet_guru(6);
    CHECK(!charge.elixir_spent && guru_recharges == 2 && (ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    /* Never owned: a Guru gives none. */
    play(true);
    meet_guru(1);
    CHECK(!(ram[SPECIAL_ITEMS] & BIT_ELIXIR) && guru_recharges == 0);
    /* Turned off: the original, used up. */
    play(false);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    meet_guru(0);
    CHECK(!(ram[SPECIAL_ITEMS] & BIT_ELIXIR) && revives == 0);
}

static void elixir_found_not_bought(void) {
    /* Spent, the Elixir is still his: fax_recharge_elixir_spent says so, and
     * Quick shops lists it as owned, so no purchase charges it. A find does. */
    play(true);
    CHECK(fax_recharge_on() && !fax_recharge_elixir_spent());
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    CHECK(charge.elixir_spent && fax_recharge_elixir_spent());
    pick_up_elixir();
    frames(1);
    CHECK(!charge.elixir_spent && !fax_recharge_elixir_spent() && (ram[SPECIAL_ITEMS] & BIT_ELIXIR) &&
          script == SCRIPT_GOT_ELIXIR);
    /* A Guru charges it too. */
    hurt();
    CHECK(fax_recharge_elixir_spent());
    meet_guru(0);
    CHECK(!fax_recharge_elixir_spent() && (ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    /* The Spring of Trunk spends a charged one: still his, and a Guru
     * charges it again (the_spring_spends_it has the rest). */
    spring_script();
    run_script(SPRING, 0);
    frames(1);
    CHECK(!(ram[SPECIAL_ITEMS] & BIT_ELIXIR) && charge.elixir_spent && fax_recharge_elixir_spent());
    meet_guru(0);
    CHECK((ram[SPECIAL_ITEMS] & BIT_ELIXIR) && !fax_recharge_elixir_spent());
    /* Turned off, the original: its revive uses it up, and none is spent. */
    play(false);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    CHECK(!fax_recharge_on() && !fax_recharge_elixir_spent() && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
}

/* The Spring of Trunk (#138): with a charged Elixir his quest is done as the
 * original's, with the same lines, and the Elixir is spent, not taken: the
 * status bar's mark goes grey, and a Guru or a find charges it again. With a
 * spent one, or none, he says to come back with the medicine. Off, the
 * original: he takes it for good. */
static void the_spring_spends_it(void) {
    play(true);
    spring_script();
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    CHECK(fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_READY);
    run_script(SPRING, 0);
    SAID("ask 59", "msg 5A", "end");
    CHECK((ram[QUESTS] & QUEST_SPRING_OF_TRUNK) && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR) && springs == 1);
    frames(1);
    CHECK(fax_recharge_elixir_spent() && fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_USED);
    /* Done, he only sends the hero on, the Elixir as it was. */
    run_script(SPRING, 0);
    SAID("msg 5C", "end");
    CHECK(fax_recharge_elixir_spent() && springs == 1);
    /* Spent: the quest not done, he refuses, as without one, and it stays spent. */
    ram[QUESTS] = 0;
    run_script(SPRING, 0);
    SAID("ask 59", "msg 5B", "end");
    frames(1);
    CHECK(!(ram[QUESTS] & QUEST_SPRING_OF_TRUNK) && fax_recharge_elixir_spent() && springs == 1);
    CHECK(fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_USED);
    /* A find charges it, and the spring spends it again. */
    pick_up_elixir();
    frames(1);
    CHECK(!fax_recharge_elixir_spent() && fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_READY);
    run_script(SPRING, 0);
    SAID("ask 59", "msg 5A", "end");
    CHECK(fax_recharge_elixir_spent() && springs == 2);
    /* None: he refuses, and nothing is spent. */
    play(true);
    spring_script();
    run_script(SPRING, 0);
    SAID("ask 59", "msg 5B", "end");
    CHECK(!ram[QUESTS] && !fax_recharge_elixir_spent() && springs == 0 &&
          fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_NONE);
    /* Only the spring's step, as the ROM has it, with the Elixir charged. */
    CHECK(spring_spends(0xA1BB, 0x10, 0x92, BIT_ELIXIR));
    CHECK(!spring_spends(0xA1BB, 0x10, 0x92, 0));                    /* spent: the bit is clear */
    CHECK(!spring_spends(0xA1B9, 0x01, 0x5A, BIT_ELIXIR));
    CHECK(!spring_spends(0xA1BB, 0x10, 0x94, BIT_ELIXIR));           /* not this ROM's step */
    CHECK(!spring_spends(0xA200, 0x10, 0x92, BIT_ELIXIR));
    /* In Seamless's isolated scope, nothing. */
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    isolated = true;
    run_script(SPRING, 0);
    isolated = false;
    CHECK(!charge.elixir_spent && springs == 0);
    /* Off, the original: it takes the Elixir for good, none spent. */
    play(false);
    spring_script();
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    run_script(SPRING, 0);
    SAID("ask 59", "msg 5A", "end");
    CHECK((ram[QUESTS] & QUEST_SPRING_OF_TRUNK) && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    CHECK(!fax_recharge_elixir_spent() && fax_recharge_charge(FAX_RECHARGE_ELIXIR) == FAX_CHARGE_NONE);
}

/* Each Guru's conversation fills HP and MP (MP from kills) at his first
 * action, with the Elixir's charge, before he says anything: his Elixir line
 * too. Once a conversation; no one else's script; not when off. */
static void gurus_fill_hp_and_mp(void) {
    static const unsigned temples[] = { 0, 1, 3, 5, 6 };
    for (unsigned i = 0; i < sizeof temples / sizeof temples[0]; ++i) {
        play(true);
        ram[SPECIAL_ITEMS] = 0x80;          /* the Ring of Elf, no Elixir: no line */
        guru_fills = 0;
        meet_guru(temples[i]);
        CHECK(guru_fills == 1 && said_at_fill == 0 && said_n >= 1);
        ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
        hurt();                             /* spent: he says so, after the fill */
        CHECK(charge.elixir_spent);
        meet_guru(temples[i]);
        CHECK(guru_fills == 2 && said_at_fill == 0 && !strcmp(said[0], RESTORED) && !charge.elixir_spent);
    }
    play(true);
    guru_fills = 0;
    static const uint8_t npc[] = { 0x81, 0x03, 0x40, 0x00 };   /* a portrait, a line, the end */
    memcpy(bank12 + (0xA310 - 0x8000), npc, sizeof npc);
    run_script(0xA310, 0);
    CHECK(guru_fills == 0 && said_n == 2);
    play(false);
    meet_guru(1);
    CHECK(guru_fills == 0);
}

/* ---- what a Guru says about the Elixir ---- */

static uint8_t spawn_seen[12], title_seen[12];   /* TempleSpawnPoint and PlayerTitle as each message showed */
static void note_state(void) { spawn_seen[said_n - 1] = ram[0x0439]; title_seen[said_n - 1] = ram[PLAYER_TITLE]; }
/* Only Guru travel's SAY step and message $C3's slot are served: no entry Guru travel has not made already. */
static bool only_the_shared_bytes(void) {
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned at = 0; at < 0x4000; ++at) {
            uint16_t addr = (uint16_t)(0x8000 + at);
            bool shared = b == 0 ? addr >= SAY && addr < SAY + 3 : addr >= SLOT && addr < SLOT + SLOT_LEN;
            if (serving[b][at] && !shared) return false;
        }
    return true;
}

static void guru_lines_fit(void) {
    uint8_t out[SLOT_LEN];
    for (int i = LINE_NONE + 1; i < LINES; ++i) CHECK(encode(guru_line[i], out));
    CHECK(encode("sixteen chars ok", out) && out[16] == 0xFF);
    CHECK(!encode("seventeen chars!!", out) && !encode("1\n2\n3\n4\n5", out) && !encode("tab\there", out));
    CHECK(encode("a b\nc", out) && out[1] == 0xFD && out[3] == 0xFE && out[5] == 0xFF);
    /* Which line, by what the hero carries and what he has been told. */
    Charge c = { 0 };
    CHECK(guru_says(&c, false) == LINE_NONE && guru_says(&c, true) == LINE_CAN);
    c.elixir_told = 1;
    CHECK(guru_says(&c, true) == LINE_NONE && guru_says(&c, false) == LINE_NONE);
    c.elixir_spent = 1;
    CHECK(guru_says(&c, false) == LINE_RESTORED);
    c.elixir_told = 0;
    CHECK(guru_says(&c, false) == LINE_RESTORED);
}

/* A spent Elixir: each Guru who charges it says so first, every time, and
 * then goes on as he would have. A temple Guru's SET_SPAWN_POINT has run by
 * then, and his CHECK_UPDATE_PLAYER_TITLE runs after. */
static void the_restored_line(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;  /* and the Ring of Elf */
    hurt();
    on_line = note_state;
    meet_guru(1);
    SAID(RESTORED, "msg 22", "mantra", "end");
    CHECK(!charge.elixir_spent && (ram[SPECIAL_ITEMS] & BIT_ELIXIR) && guru_recharges == 1);
    CHECK(lines_said[LINE_RESTORED] == 1 && charge.elixir_told && talk.phase == TALK_IDLE);
    CHECK(spawn_seen[0] == 1);              /* SET_SPAWN_POINT ran before the line */
    CHECK(entries == 3 + SLOT_LEN && only_the_shared_bytes());
    hurt();
    meet_guru(0);                           /* Eolis's Guru, with the ring */
    SAID(RESTORED, "msg 2D", "end");
    hurt();
    ram[NEXT_PLAYER_TITLE] = 3; ram[PLAYER_TITLE] = 2;
    meet_guru(5);                           /* Conflate's, past his CHECK_FOR_ITEM, with a new title */
    SAID(RESTORED, "msg 23", "end");
    CHECK(title_seen[0] == 2 && ram[PLAYER_TITLE] == 3);   /* the title's check ran after the line */
    CHECK(guru_recharges == 3 && lines_said[LINE_RESTORED] == 3);
    /* Charged and told: he only greets. */
    meet_guru(3);
    SAID("msg 22", "mantra", "end");
    CHECK(lines_said[LINE_RESTORED] == 3 && lines_said[LINE_CAN] == 0);
}

/* A charged Elixir: the first Guru the hero brings it to says once that any
 * Guru restores it. Either line tells him. */
static void the_can_line(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    meet_guru(2);
    SAID(CAN, "msg 22", "mantra", "end");
    CHECK(charge.elixir_told && lines_said[LINE_CAN] == 1 && guru_recharges == 0);
    meet_guru(2);
    SAID("msg 22", "mantra", "end");
    meet_guru(0);                           /* told is told, whichever Guru */
    SAID("msg 2D", "end");
    CHECK(lines_said[LINE_CAN] == 1);
    /* Eolis's Guru the first time, before he gives the ring. */
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    meet_guru(0);
    SAID(CAN, "msg 2C", "end");
    CHECK(ram[SPECIAL_ITEMS] == (BIT_ELIXIR | 0x80));
    /* The restored line told him: no other line once it is charged. */
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    hurt();
    meet_guru(6);
    SAID(RESTORED, "msg 22", "mantra", "end");
    meet_guru(6);
    SAID("msg 22", "mantra", "end");
    CHECK(lines_said[LINE_CAN] == 0);
    /* No Elixir: nothing said, nothing told, nothing served. */
    play(true);
    ram[SPECIAL_ITEMS] = 0x80;
    meet_guru(4);
    SAID("msg 22", "mantra", "end");
    meet_guru(0);
    SAID("msg 2D", "end");
    CHECK(!charge.elixir_told && lines_said[LINE_CAN] == 0 && entries == 0);
    /* A Guru who sets no spawn point (the Castle of Fraternal's) charges nothing and says nothing. */
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    stock_scripts();
    run_script(FRATERNAL_GURU, 0);
    SAID("msg 60", "end");
    CHECK(!charge.elixir_told && entries == 0);
    /* Off, the original: no line, and nothing served. */
    play(false);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    meet_guru(1);
    SAID("msg 22", "mantra", "end");
    CHECK(entries == 0);
    /* Inside Seamless's isolated rooms: nothing. */
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    isolated = true;
    meet_guru(1);
    SAID("msg 22", "mantra", "end");
    CHECK(!charge.elixir_told && entries == 0);
    isolated = false;
}

/* B on the line ends the script, as on any of the Guru's lines: the Elixir
 * is charged and he has told the hero. The next script is someone else's,
 * even one whose end shows a line through the same SAY step (a shop's, with
 * Block's tutorial): its END ends it. */
static void b_on_the_line(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    hurt();
    meet_guru_b(1, 1);
    SAID(RESTORED, "B");
    CHECK(!charge.elixir_spent && (ram[SPECIAL_ITEMS] & BIT_ELIXIR) && charge.elixir_told);
    static const uint8_t shop[] = { 0x89, 0x03, 0x0E, 0x17, SAY & 0xFF, SAY >> 8 };
    memcpy(bank12 + (0xA300 - 0x8000), shop, sizeof shop);
    run_script(0xA300, 0);
    SAID("msg 0E", RESTORED, "end");        /* the slot as it was left: Block would have served its own line */
    CHECK(talk.phase == TALK_IDLE);
    meet_guru(1);
    SAID("msg 22", "mantra", "end");
}

/* Save states: the line, waiting or showing, comes back with the record, and
 * the slot is served again (the engine's overrides are not saved). */
static uint8_t mid_line[32];
static void save_and_relaunch(void) {
    if (said_n != 1) return;
    CHECK(save_get(mid_line, sizeof mid_line) == RECORD && mid_line[13] == TALK_SAYING && mid_line[14] == LINE_RESTORED);
    overrides_lost();                       /* a new launch, with this machine's RAM loaded */
    reset_cb();
    plugin_cb();
    CHECK(save_set(mid_line, RECORD) == 1);
    fax_recharge_loaded();
    char text[64];
    slot_text(text);
    CHECK(!strcmp(text, RESTORED) && cyc_mod_peek(SAY) == 0x03 && cyc_mod_peek(SAY + 2) == 0x00);
}
static void line_in_a_save_state(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    hurt();
    on_line = save_and_relaunch;
    meet_guru(1);
    SAID(RESTORED, "msg 22", "mantra", "end");
    CHECK(talk.phase == TALK_IDLE && !charge.elixir_spent && charge.elixir_told);
    /* Saved between SET_SPAWN_POINT and the action the line goes before. */
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    hurt();
    stock_scripts();
    temple_script(OTHER_GURU, 1);
    ram[ISCRIPT_ADDR] = (uint8_t)OTHER_GURU; ram[ISCRIPT_ADDR + 1] = (uint8_t)(OTHER_GURU >> 8); ram[ISCRIPT_OFFSET] = 1;
    CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));   /* at SET_SPAWN_POINT */
    uint8_t buf[RECORD];
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[13] == TALK_PENDING && (buf[15] | buf[16] << 8) == OTHER_GURU + 3);
    reset_cb(); plugin_cb();
    CHECK(save_set(buf, RECORD) == 1 && talk.phase == TALK_PENDING);
    run_from(OTHER_GURU, 3, 0);
    SAID(RESTORED, "msg 22", "mantra", "end");
}

/* No room for the line's overrides: the Guru restores the Elixir silently. */
static void no_room_for_the_line(void) {
    play(true);
    table_full = true;
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    hurt();
    meet_guru(1);
    SAID("msg 22", "mantra", "end");
    CHECK(lines_failed && !charge.elixir_spent && (ram[SPECIAL_ITEMS] & BIT_ELIXIR) && !charge.elixir_told);
    table_full = false;
    hurt();
    meet_guru(1);
    SAID("msg 22", "mantra", "end");        /* off for the session */
    CHECK(!charge.elixir_spent);
}

/* Save files: that a Guru told the hero is the file's recharge.elixir_told. */
static void told_in_the_file(void) {
    play(true);
    file_in_play = true;
    fields_clear();
    save_listener(FAX_SAVE_NEW_GAME);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR | 0x80;
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("recharge.elixir_told") == NULL);
    meet_guru(1);
    SAID(CAN, "msg 22", "mantra", "end");
    save_listener(FAX_SAVE_WRITING);        /* "Remember my journey.", or the next spawn */
    CHECK(field("recharge.elixir_told") && !strcmp(field("recharge.elixir_told"), "yes"));
    /* Quit, then continue the file: told, so no line. */
    title_screen();
    CHECK(!charge.elixir_told);
    save_listener(FAX_SAVE_CONTINUED);
    respawn();
    CHECK(charge.elixir_told);
    meet_guru(1);
    SAID("msg 22", "mantra", "end");
    /* A new game in the file: not told, and the file says so at its next write. */
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!charge.elixir_told);
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("recharge.elixir_told") == NULL);
    /* A file from before the lines: not told. */
    fields_clear();
    title_screen();
    save_listener(FAX_SAVE_CONTINUED);
    respawn();
    meet_guru(1);
    SAID(CAN, "msg 22", "mantra", "end");
    file_in_play = false;
}

int main(void) {
    registers();
    flight_by_title_band();
    a_flight_lasts_its_mp();
    only_while_a_is_held();
    the_flight_follows_mp();
    using_them_again_lands();
    the_bar_waits_for_room();
    the_freeze_runs_on_mp();
    the_freeze_ends_early();
    both_at_once();
    hud_shows_the_selected_item();
    dying_ends_both();
    guru_travel_keeps_them();
    elixir();
    elixir_found_not_bought();
    the_spring_spends_it();
    gurus_fill_hp_and_mp();
    charges_on_the_status_bar();
    finds_add_nothing();
    other_items_untouched();
    new_game_and_mantra();
    isolated_runs_change_nothing();
    save_states();
    old_records_load();
    save_files();
    guru_lines_fit();
    the_restored_line();
    the_can_line();
    b_on_the_line();
    line_in_a_save_state();
    no_room_for_the_line();
    told_in_the_file();
    printf("recharge_test: %u checks passed\n", checks);
    return 0;
}
