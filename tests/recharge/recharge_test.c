/* Unit tests for recharging items (fax_recharge.c), without the emulator or a
 * ROM. The module runs against a model of the machine: CPU RAM, the
 * registers, the hook sites as the engine dispatches them, the PPU buffer as
 * the NMI drains it into the HUD's T:, and the item, countdown, death and
 * pickup code played step by step as the disassembly has them (the JSRs push
 * their real return addresses). So the tests check what the player holds,
 * sees and hears rather than restate the module's constants. */
#include "fax_recharge.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static CycModRegs cpu;
static bool isolated;
static int sound = -1;                        /* the last effect Sound_PlayEffect queued */

static uint8_t bank12[0x4000];               /* $8000-$BFFF as bank 12 maps it: the Gurus' scripts */
uint8_t cyc_mod_peek(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    return addr >= 0x8000 && addr < 0xC000 ? bank12[addr - 0x8000] : 0;
}
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) { cpu = *in; }
bool cyc_mod_isolated(void) { return isolated; }
bool cyc_mod_call_commit(uint16_t routine, CycModRegs *regs) {
    CHECK(routine == SOUND_PLAY_EFFECT);      /* the only routine it runs */
    sound = regs->a;
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
static long field_int(const char *key) { const char *v = field(key); return v ? strtol(v, NULL, 10) : -1; }
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

/* ---- the game, as the disassembly has it ---- */
enum { WING_BOOTS = 0x0F, HOUR_GLASS = 0x0D, RED_POTION = 0x10, MATTOCK = 0x09, NO_ITEM = 0xFF };
enum { HP_U = 0x0431, HP_L = 0x0432, PLAYER_IS_DEAD = 0x0438, DURATION_GLOVE = 0x0428 };
enum { SCRIPT_USED_HOUR_GLASS = 0x82, SCRIPT_USED_WING_BOOTS = 0x83, SCRIPT_USED_ELIXIR = 0x85,
       SCRIPT_GOT_ELIXIR = 0x86, SCRIPT_GOT_WING_BOOTS = 0x89, SCRIPT_GOT_HOUR_GLASS = 0x8A };
static const uint8_t STOCK_FLIGHT[BANDS] = { 40, 30, 20, 10 };    /* TITLE_TO_WINGBOOTS_DURATION ($C599) */
static int picture;                         /* the item the HUD's picture tiles show */
static int script;                          /* the last item script run */
static int gone;                            /* the last timed item that ran out ("... are gone") */
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

/* The NMI: InterruptCounter, then PPUBuffer_Draw empties the buffer. */
static void nmi(void) {
    ++ram[INTERRUPT_COUNTER];
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
static bool t_blank(void) { return t_shown[0] == TILE_BLANK && t_shown[1] == TILE_BLANK; }
static int t_value(void) {
    if (t_shown[0] < '0' || t_shown[0] > '9' || t_shown[1] < '0' || t_shown[1] > '9') return -1;
    return (t_shown[0] - '0') * 10 + t_shown[1] - '0';
}

/* GameLoop_CountdownItems: Game_DecHourGlassDuration, Game_DecWingBootsDuration, ... */
static void countdown(void) {
    CHECK(!fire(GAME_LOOP_COUNTDOWN));      /* it only observes */
    bool second = (ram[INTERRUPT_COUNTER] & 0x3F) == 0;
    if (ram[DURATION_HOUR_GLASS] < 0x80 && second && --ram[DURATION_HOUR_GLASS] >= 0x80) gone = HOUR_GLASS;
    if (ram[DURATION_WING_BOOTS] < 0x80 && second) {
        if (--ram[DURATION_WING_BOOTS] >= 0x80) gone = WING_BOOTS;
        else draw_time(ram[DURATION_WING_BOOTS]);
    }
}
static void frames(unsigned n) { while (n--) { nmi(); countdown(); } }
static void seconds(unsigned n) { frames(n * 64); }
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
    case HOUR_GLASS:                        /* Player_UseHourGlass */
        if (fire(PLAYER_USE_HOUR_GLASS)) break;
        script = SCRIPT_USED_HOUR_GLASS;
        clear_selected(0xC5D0);
        sound = 0x1A;
        ram[HP_L] = (uint8_t)(ram[HP_L] >> 1 | (ram[HP_U] & 1) << 7);   /* LSR Player_HP_U, ROR Player_HP_L */
        ram[HP_U] >>= 1;
        ram[DURATION_HOUR_GLASS] = 0x0F;
        break;
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
/* Player_ReduceHP taking the last of the hero's HP. */
static void hurt(void) {
    ram[HP_U] = 0; ram[HP_L] = 0;
    if (ram[SPECIAL_ITEMS] & BIT_ELIXIR) {  /* JMP Player_UseElixir */
        CHECK(!fire(PLAYER_USE_ELIXIR));
        ram[SPECIAL_ITEMS] &= (uint8_t)~BIT_ELIXIR;
        script = SCRIPT_USED_ELIXIR;
        ram[HP_U] = 0x50;                   /* Player_FillHPAndMP */
    } else {
        ram[PLAYER_IS_DEAD] = 1;
    }
}
/* Player_HandleDeath, ..., Player_Spawn: HP and MP to 80, then Game_ClearTimedItems. */
static void respawn(void) {
    ram[PLAYER_IS_DEAD] = 0;
    ram[HP_U] = 0x50;
    CHECK(!fire(GAME_CLEAR_TIMED_ITEMS));
    for (uint16_t a = 0x0427; a <= DURATION_HOUR_GLASS; ++a) ram[a] = 0xFF;
}
/* A Guru travel journey: Player_Spawn entered past Game_ClearTimedItems, at
 * its JSR Game_SpawnInTemple. Nothing here for this feature. */
static void journey(void) { nmi(); }
/* A Guru's conversation begins (fax_travel.c tells its listeners). */
/* IScripts_InvokeNextAction, about to run the action at `at`. */
static void next_action(uint16_t at) {
    ram[ISCRIPT_ADDR] = (uint8_t)at; ram[ISCRIPT_ADDR + 1] = (uint8_t)(at >> 8); ram[ISCRIPT_OFFSET] = 0;
    CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));   /* it only observes */
}
/* Talking to a Guru: his script's actions, as the interpreter reaches each.
 * A temple Guru's opens with 06 temple (then the mantra or a new title, or
 * Save files' or Guru travel's steps); Eolis's with CHECK_FOR_ITEM. */
static void meet_guru(unsigned temple) {
    static const uint16_t script = 0xA200, eolis = 0xA0BC;
    if (temple == 0) {
        bank12[eolis - 0x8000] = 0x81;                          /* the Guru's portrait */
        static const uint8_t first[] = { 0x12, 0x80, 0xC6, 0xA0, 0x01, 0x2B, 0x10, 0x80, 0x00 };
        memcpy(bank12 + (eolis + 1 - 0x8000), first, sizeof first);
        ram[ISCRIPT_ADDR] = (uint8_t)eolis; ram[ISCRIPT_ADDR + 1] = (uint8_t)(eolis >> 8); ram[ISCRIPT_OFFSET] = 1;
        CHECK(!fire(ISCRIPTS_INVOKE_NEXT_ACTION));
        next_action(0xA0C6);                                    /* 03 2D, 00: he has the ring */
        return;
    }
    const uint8_t bytes[] = { 0x06, (uint8_t)temple, 0x17, 0xFC, 0xA5 };
    memcpy(bank12 + (script - 0x8000), bytes, sizeof bytes);
    next_action(script);
    next_action(script + 2);                                    /* 17: on to the mantra */
    next_action(0xA5FC);
}
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
    ram[INTERRUPT_COUNTER] = 1;
    give(NULL, 0, NO_ITEM);
    memcpy(t_shown, "00", 3);
    isolated = false;
    if (on) plugin_cb();
    title_screen();
    respawn();                              /* Game_Start clears the timed items too */
}

static bool boots_down(void) { return ram[DURATION_WING_BOOTS] >= 0x80; }
static bool glass_down(void) { return ram[DURATION_HOUR_GLASS] >= 0x80; }
static bool boots_ready(void) { return ready(&charge, BOOTS); }
static bool glass_ready(void) { return ready(&charge, GLASS); }

/* Over one game second, T: shows `v` and blinks: the digits, then blanks. */
static void check_t_blinks(int v) {
    bool digits = false, blank = false;
    for (int n = 0; n < 64; ++n) {
        frames(1);
        if (t_blank()) blank = true;
        else if (t_value() == v || t_value() == v - 1) digits = true;
        else CHECK(!"T: shows something else");
    }
    CHECK(digits && blank);
}

/* ---- tests ---- */
static void registers(void) {
    CHECK(site_count == 10 && !strcmp(plugin_id, "faxanadu.recharge") && reset_cb == deactivate);
    CHECK(save_get && save_set && save_validate);
    play(true);
    for (int i = 0; i < site_count; ++i) CHECK(sites[i].on);
    play(false);
    for (int i = 0; i < site_count; ++i) CHECK(!sites[i].on);
}

static void flight_grows_with_title(void) {
    static const uint8_t flight[BANDS] = { 30, 40, 50, 60 };          /* decided in #27 */
    for (int title = 0; title < 16; ++title) {
        play(true);
        ram[PLAYER_TITLE] = (uint8_t)title;
        give(NULL, 0, WING_BOOTS);
        use();
        CHECK(script == SCRIPT_USED_WING_BOOTS && sound == 0x1A);
        CHECK(ram[DURATION_WING_BOOTS] == flight[title / 4]);
        nmi();
        CHECK(t_value() == flight[title / 4]);   /* the game's own draw shows it */
        CHECK(selected() == WING_BOOTS && picture == WING_BOOTS && count() == 0);
        /* The cooldown is 60 s at every title. */
        CHECK(frames_until(boots_down, (flight[title / 4] + 1) * 64) > 0);
        frames(1);
        CHECK(charge.cooldown[BOOTS] == 60);
    }
    /* Turned off: the original table, and the pair is used up. */
    for (int title = 0; title < 16; ++title) {
        play(false);
        ram[PLAYER_TITLE] = (uint8_t)title;
        give(NULL, 0, WING_BOOTS);
        use();
        CHECK(ram[DURATION_WING_BOOTS] == STOCK_FLIGHT[title / 4] && selected() == NO_ITEM && picture == NO_ITEM);
    }
}

static void wing_boots_cycle(void) {
    play(true);
    const uint8_t items[] = { RED_POTION };
    give(items, 1, WING_BOOTS);
    use();
    CHECK(charge.running[BOOTS] && !boots_ready() && flights == 1 && kept == 1);
    CHECK(selected() == WING_BOOTS && picture == WING_BOOTS && count() == 1);
    /* Down-B mid-flight does nothing but the "invalid choice" sound. */
    frames(100);
    uint8_t left = ram[DURATION_WING_BOOTS];
    use();
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && refused == 1 && ram[DURATION_WING_BOOTS] == left);
    /* The flight runs out as in the original: 30 decrements, the last to $FF. */
    int flown = frames_until(boots_down, 32 * 64);
    CHECK(flown > 0 && gone == WING_BOOTS);
    CHECK(t_value() == 0);                  /* the game's last draw */
    /* The next frame the cooldown starts, and T: blinks it. */
    frames(1);
    CHECK(charge.cooldown[BOOTS] == 60 && !charge.running[BOOTS]);
    check_t_blinks(60);
    use();
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && refused == 2 && boots_down());
    CHECK(selected() == WING_BOOTS && picture == WING_BOOTS);
    /* Ready after 60 of the game's seconds, to the frame. */
    int waited = frames_until(boots_ready, 62 * 64);
    CHECK(waited >= 58 * 64 && waited <= 60 * 64);
    frames(1);
    CHECK(t_value() == 0 && !t_blank());    /* T: back to the idle 00 */
    frames(200);
    CHECK(t_value() == 0);
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS && ram[DURATION_WING_BOOTS] == 30 && flights == 2);
}

static void hour_glass_cycle(void) {
    play(true);
    give(NULL, 0, HOUR_GLASS);
    use();
    CHECK(script == SCRIPT_USED_HOUR_GLASS && ram[DURATION_HOUR_GLASS] == 15 && hp() == 40);   /* half of 80 */
    CHECK(selected() == HOUR_GLASS && picture == HOUR_GLASS && freezes == 1);
    use();                                  /* not while it runs */
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && hp() == 40);
    CHECK(frames_until(glass_down, 17 * 64) > 0 && gone == HOUR_GLASS);
    frames(1);
    CHECK(charge.cooldown[GLASS] == 60);
    check_t_blinks(60);                     /* T: blinks it too: the Wing Boots are not cooling */
    use();
    CHECK(script == 0 && sound == SOUND_INVALID_CHOICE && hp() == 40 && glass_down());
    CHECK(frames_until(glass_ready, 61 * 64) > 0);
    use();
    CHECK(script == SCRIPT_USED_HOUR_GLASS && hp() == 20 && ram[DURATION_HOUR_GLASS] == 15);   /* the cost again */
}

static void hud_prefers_the_selected_item(void) {
    play(true);
    charge.cooldown[BOOTS] = 50; charge.cooldown[GLASS] = 20;
    give(NULL, 0, HOUR_GLASS);
    Show s = hud_show(&charge, 0xFF, HOUR_GLASS, 0);
    CHECK(s.value == 20 && !s.blank);
    s = hud_show(&charge, 0xFF, RED_POTION, 0);
    CHECK(s.value == 50);                   /* the Wing Boots first otherwise */
    CHECK(hud_show(&charge, 0xFF, WING_BOOTS, 0x30).blank && !hud_show(&charge, 0xFF, WING_BOOTS, 0x2F).blank);
    CHECK(hud_show(&charge, 12, HOUR_GLASS, 0).value == SHOW_GAME);   /* flying: the game's count */
    charge.cooldown[BOOTS] = charge.cooldown[GLASS] = 0;
    CHECK(hud_show(&charge, 0xFF, WING_BOOTS, 0x30).value == 0 && !hud_show(&charge, 0xFF, WING_BOOTS, 0x30).blank);
    /* A full PPU buffer: T: waits for room, as the game's own draws do. */
    play(true);
    charge.cooldown[BOOTS] = 30;
    ram[PPU_BUFFER_READ] = 0x10; ram[PPU_BUFFER_WRITE] = 0x00;        /* 16 bytes free: too few */
    tick_hook(GAME_LOOP_COUNTDOWN);
    CHECK(ram[PPU_BUFFER_WRITE] == 0x00);
    ram[PPU_BUFFER_READ] = 0x00;                                       /* drained */
    tick_hook(GAME_LOOP_COUNTDOWN);
    CHECK(ram[PPU_BUFFER_WRITE] == 5);
    /* Wrapping round the 256-byte ring. */
    play(true);
    charge.cooldown[GLASS] = 42;
    ram[PPU_BUFFER_READ] = ram[PPU_BUFFER_WRITE] = 0xFE;
    nmi(); countdown(); nmi();
    CHECK(ram[PPU_BUFFER_WRITE] == 0x03 && (t_value() == 42 || t_blank()));
}

static void dying_makes_both_ready(void) {
    play(true);
    const uint8_t items[] = { HOUR_GLASS };
    give(items, 1, WING_BOOTS);
    use();
    seconds(5);
    respawn();                              /* dies mid-flight */
    frames(2);
    CHECK(boots_ready() && boots_down() && !charge.running[BOOTS] && charge.cooldown[BOOTS] == 0);
    CHECK(t_value() == 0 && !t_blank());
    use();
    CHECK(script == SCRIPT_USED_WING_BOOTS);
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    frames(64 * 10);
    CHECK(charge.cooldown[BOOTS] > 0);
    ram[SELECTED_ITEM] = HOUR_GLASS;
    use();
    CHECK(frames_until(glass_down, 17 * 64) > 0);
    frames(2);
    CHECK(charge.cooldown[GLASS] > 0);
    respawn();                              /* dies with both cooling */
    CHECK(boots_ready() && glass_ready() && spawns >= 2);
}

static void guru_travel_keeps_them(void) {
    play(true);
    give(NULL, 0, WING_BOOTS);
    use();
    seconds(3);
    uint8_t flying = ram[DURATION_WING_BOOTS];
    journey();
    CHECK(charge.running[BOOTS] && ram[DURATION_WING_BOOTS] == flying);
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    frames(1);
    CHECK(charge.cooldown[BOOTS] == 60);
    seconds(10);
    uint8_t cooling = charge.cooldown[BOOTS];
    journey();
    meet_guru(3);                           /* talking to a Guru changes neither */
    CHECK(charge.cooldown[BOOTS] == cooling && !boots_ready());
    seconds(5);
    CHECK(charge.cooldown[BOOTS] < cooling);
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
    seconds(400);                           /* no timer, and no recharge on death */
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
    /* The Spring of Trunk's CONSUME_ITEM takes a charged one for good: neither
     * charged nor spent, it is no longer his, and a Guru gives none. */
    ram[SPECIAL_ITEMS] &= (uint8_t)~BIT_ELIXIR;
    frames(1);
    meet_guru(0);
    CHECK(!(ram[SPECIAL_ITEMS] & BIT_ELIXIR) && !charge.elixir_spent && !fax_recharge_elixir_spent());
    /* Turned off, the original: its revive uses it up, and none is spent. */
    play(false);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    CHECK(!fax_recharge_on() && !fax_recharge_elixir_spent() && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
}

static void finds_recharge(void) {
    play(true);
    const uint8_t items[] = { RED_POTION, HOUR_GLASS };
    give(items, 2, WING_BOOTS);
    use();
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    frames(64);
    CHECK(charge.cooldown[BOOTS] > 50);
    pick_up(WING_BOOTS);                    /* a pair in the world */
    CHECK(boots_ready() && count() == 2 && pickups == 1 && script == SCRIPT_GOT_WING_BOOTS);
    frames(2);
    CHECK(t_value() == 0 && !t_blank());
    /* The Hour Glass in the list, unselected, while it cools down. */
    ram[SELECTED_ITEM] = HOUR_GLASS;
    ram[ITEM_INVENTORY + 1] = WING_BOOTS;
    use();
    CHECK(frames_until(glass_down, 17 * 64) > 0);
    frames(2);
    ram[SELECTED_ITEM] = RED_POTION; ram[ITEM_INVENTORY] = HOUR_GLASS;
    CHECK(!glass_ready());
    pick_up(HOUR_GLASS);
    CHECK(glass_ready() && count() == 2 && pickups == 2);
    /* Found while flying: this flight goes on, and no cooldown follows it. */
    ram[SELECTED_ITEM] = WING_BOOTS; ram[ITEM_INVENTORY + 1] = RED_POTION;
    use();
    seconds(4);
    pick_up(WING_BOOTS);
    CHECK(ram[DURATION_WING_BOOTS] < 0x80 && boots_ready());
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    frames(2);
    CHECK(boots_ready());
    /* Not owned: the original pickup, into the list. */
    give(NULL, 0, RED_POTION);
    pick_up(WING_BOOTS);
    CHECK(count() == 1 && ram[ITEM_INVENTORY] == WING_BOOTS && pickups == 3);
    /* A full list still recharges a pair you own. */
    const uint8_t full[] = { WING_BOOTS, 1, 2, 3, 4, 5, 6, 7 };
    give(full, 8, RED_POTION);
    charge.cooldown[BOOTS] = 33;
    pick_up(WING_BOOTS);
    CHECK(boots_ready() && count() == 8);
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
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    isolated = true;                        /* Seamless simulating another room */
    frames(1);
    CHECK(charge.running[BOOTS] && charge.cooldown[BOOTS] == 0);
    respawn();
    pick_up(WING_BOOTS);
    CHECK(charge.running[BOOTS] && count() == 1);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    meet_guru(0);
    CHECK(!charge.elixir_spent);
    isolated = false;
    frames(1);
    CHECK(charge.cooldown[BOOTS] == 60);
}

static void save_states(void) {
    play(true);
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    give(NULL, 0, WING_BOOTS);
    use();
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    seconds(7);
    ram[SELECTED_ITEM] = HOUR_GLASS;
    use();
    uint8_t buf[16];
    int n = save_get(buf, sizeof buf);
    CHECK(n == RECORD && save_validate(buf, n));
    CHECK(buf[0] == 1 && buf[1] == 0 && buf[2] >= 52 && buf[2] <= 54 && buf[3] == 1 && buf[4] == 0 && buf[5] == 1);
    CHECK(buf[6] == 0 && buf[7] == 0 && buf[8] == 0);
    CHECK(save_get(buf, RECORD - 1) == -1);
    Charge saved = charge;
    play(true);
    CHECK(save_set(buf, n) == 1 && !memcmp(&charge, &saved, sizeof charge));
    /* A state from before the feature: everything ready, nothing spent. */
    CHECK(save_validate(buf, 0) && save_set(buf, 0) == 1 && boots_ready() && glass_ready() && !charge.elixir_spent);
    /* Records it does not accept change nothing. */
    CHECK(save_set(buf, n) == 1);
    static const struct { int at; uint8_t value; } bad[] = {
        { 0, 2 }, { 1, 2 }, { 2, 100 }, { 3, 7 }, { 4, 0xFF }, { 5, 2 }, { 6, 2 }, { 7, 100 }, { 8, 200 } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint8_t b[RECORD];
        memcpy(b, buf, sizeof b);
        b[bad[i].at] = bad[i].value;
        CHECK(!save_validate(b, sizeof b) && !save_set(b, sizeof b));
        CHECK(!memcmp(&charge, &saved, sizeof charge));
    }
    CHECK(!save_validate(buf, RECORD - 1) && !save_validate(buf, RECORD + 1));
}

/* Save files: the file in play keeps the cooldowns and the Elixir's charge. */
static void save_files(void) {
    CHECK(save_listener == on_save);
    /* Written whenever the file is: at a Guru, mid-cooldown, with the Elixir spent. */
    play(true);
    file_in_play = true;
    ram[SPECIAL_ITEMS] = BIT_ELIXIR;
    hurt();
    give(NULL, 0, WING_BOOTS);
    use();
    CHECK(frames_until(boots_down, 32 * 64) > 0);
    seconds(12);
    ram[SELECTED_ITEM] = HOUR_GLASS;
    use();                                  /* still freezing when the file is written */
    save_listener(FAX_SAVE_WRITING);
    long boots = field_int("recharge.wingboots"), glass = field_int("recharge.hourglass");
    CHECK(boots >= 47 && boots <= 49 && glass == 60 && !strcmp(field("recharge.elixir"), "spent"));
    /* Quit, then continue the file: Game_ShowStartScreen forgets, the file is
     * read back, and the spawn that follows keeps the cooldowns. */
    title_screen();
    CHECK(boots_ready() && !charge.elixir_spent);
    save_listener(FAX_SAVE_CONTINUED);
    respawn();                              /* Player_Spawn: Game_ClearTimedItems */
    CHECK(charge.cooldown[BOOTS] == boots && charge.cooldown[GLASS] == 60 && !charge.running[BOOTS]);
    CHECK(charge.elixir_spent && !(ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    frames(2);
    CHECK(t_value() == 60 || t_blank());     /* the selected Hour Glass's */
    meet_guru(0);
    CHECK(!charge.elixir_spent && (ram[SPECIAL_ITEMS] & BIT_ELIXIR));
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("recharge.elixir") == NULL);          /* charged: the file's special items say so */
    /* A later death makes them ready, and the file says so at its next write. */
    respawn();
    CHECK(boots_ready() && glass_ready());
    save_listener(FAX_SAVE_WRITING);
    CHECK(field_int("recharge.wingboots") == 0 && field_int("recharge.hourglass") == 0);
    /* A file from before the feature: no fields, everything ready. */
    fields_clear();
    title_screen();
    save_listener(FAX_SAVE_CONTINUED);
    respawn();
    CHECK(boots_ready() && glass_ready() && !charge.elixir_spent);
    /* A value out of range is clamped. */
    field_set("recharge.wingboots", "500");
    title_screen();
    save_listener(FAX_SAVE_CONTINUED);
    respawn();
    CHECK(charge.cooldown[BOOTS] == MOST_SECONDS);
    /* A new game resets them. */
    charge.elixir_spent = 1;
    save_listener(FAX_SAVE_NEW_GAME);
    respawn();
    CHECK(boots_ready() && glass_ready() && !charge.elixir_spent);
    /* A save state keeps a continued file's cooldowns until the spawn. */
    field_set("recharge.wingboots", "33");
    title_screen();
    save_listener(FAX_SAVE_CONTINUED);
    uint8_t buf[RECORD];
    CHECK(save_get(buf, sizeof buf) == RECORD && buf[6] == 1 && buf[7] == 33);
    play(true);
    CHECK(save_set(buf, RECORD) == 1);
    respawn();
    CHECK(charge.cooldown[BOOTS] == 33);
    /* Turned off, it writes nothing and reads nothing. */
    play(false);
    fields_clear();
    save_listener(FAX_SAVE_WRITING);
    CHECK(field("recharge.wingboots") == NULL && field("recharge.elixir") == NULL);
    file_in_play = false;
}

int main(void) {
    registers();
    flight_grows_with_title();
    wing_boots_cycle();
    hour_glass_cycle();
    hud_prefers_the_selected_item();
    dying_makes_both_ready();
    guru_travel_keeps_them();
    elixir();
    elixir_found_not_bought();
    finds_recharge();
    other_items_untouched();
    new_game_and_mantra();
    isolated_runs_change_nothing();
    save_states();
    save_files();
    printf("recharge_test: %u checks passed\n", checks);
    return 0;
}
