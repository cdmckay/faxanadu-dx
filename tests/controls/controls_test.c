/* Unit tests for the controller layout (fax_controls.c), without the emulator
 * or a ROM, but with SDL for key and controller names and a virtual
 * controller:
 * - its three hooks against a model of the machine (RAM and the registers),
 *   each where the disassembly has it: GameLoop_CheckShowPlayerMenu's entry,
 *   the pause check's AND after its load, the Select menu's BMI after its;
 * - in play, the NES Select that Back presses kept from the menu while Back
 *   is the Map button's, on an SDL virtual controller;
 * - an old config.ini (layout 1, before #147 wrote one; layout 2, before Run,
 *   #198) through the engine's own load (nesrecomp's cyc_settings.c), the
 *   game's keys going to this module and to stand-ins for the features'
 *   bindings: each binding still on its layout's default moves, once, Run
 *   comes in on its defaults, the player's stay, a move onto a button (or
 *   Run's key onto a key) the player kept gives way, and the file saved says
 *   the layout. */
#define CYC_WITH_SDL 1
#include "fax_controls.c"
#include "cyc_settings.h"

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
static CycModRegs cpu;
static int hook_on[3] = { -1, -1, -1 };

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
    return (!strcmp(id, "faxanadu.controls.menu") && addr == 0xE016 && cb == menu_hook) ||
           (!strcmp(id, "faxanadu.controls.pause") && addr == 0xE02D && cb == pause_hook) ||
           (!strcmp(id, "faxanadu.controls.close") && addr == 0x8AE8 && cb == close_hook);
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (int i = 0; i < 3; ++i)
        if (!strcmp(id, hook_ids[i])) {
            hook_on[i] = on;
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.controls") && cb == activate;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}
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

/* ---- the window's bindings, and the features' as each feature keeps them ---- */
static CycBindings window;
static const CycBindings *window_now = &window; /* during a load, the settings being loaded, as the window's are */
const CycBindings *cyc_window_bindings(void) { return window_now; }

typedef struct {
    const char *name, *key_name, *pad_name;
    int key, pad;
    unsigned changes;
    CycHostBinding b;
} Action;
static void changed(void);
enum { N_ACTIONS = 6 };
static Action actions[N_ACTIONS] = {
    { "Magic", "MagicKey", "MagicButton", 0, 0, 0, { 0 } },
    { "Use item", "UseItemKey", "UseItemButton", 0, 0, 0, { 0 } },
    { "Block (hold)", "BlockKey", "BlockButton", 0, 0, 0, { 0 } },
    { "Map", "MapKey", "MapButton", 0, 0, 0, { 0 } },
    { "Quest log", "QuestLogKey", "QuestLogButton", 0, 0, 0, { 0 } },
    { "Run (hold)", "RunKey", "RunButton", 0, 0, 0, { 0 } },
};
static unsigned changes;
static void changed(void) { ++changes; }
/* The features' defaults, layout 3's (fax_magic.c ... fax_quests.c, fax_run.c). */
static void defaults(void) {
    const int key[N_ACTIONS] = { SDL_SCANCODE_C, SDL_SCANCODE_S, SDL_SCANCODE_V,
                                 SDL_SCANCODE_M, SDL_SCANCODE_L, SDL_SCANCODE_LSHIFT };
    const int pad[N_ACTIONS] = {
        CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_Y),           CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B),
        CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1), CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK),
        CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK),   CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1)
    };
    for (int i = 0; i < N_ACTIONS; ++i) {
        Action *a = &actions[i];
        a->key = key[i];
        a->pad = pad[i];
        a->b = (CycHostBinding){ a->name, &a->key, &a->pad, key[i], pad[i], changed };
    }
    changes = 0;
}
const CycHostBinding *fax_magic_binding(void) { return &actions[0].b; }
const CycHostBinding *fax_use_item_binding(void) { return &actions[1].b; }
const CycHostBinding *fax_block_binding(void) { return &actions[2].b; }
const CycHostBinding *fax_map_binding(void) { return &actions[3].b; }
const CycHostBinding *fax_quests_binding(void) { return &actions[4].b; }
const CycHostBinding *fax_run_binding(void) { return &actions[5].b; }

/* [Game], as cyc_extras.c passes it on: the layout's key here, each binding to its feature. */
static void game_load(void *ctx, const char *key, const char *value) {
    (void)ctx;
    if (fax_controls_load_setting(key, value)) return;
    for (int i = 0; i < N_ACTIONS; ++i) {
        int v;
        if (!strcmp(key, actions[i].key_name) && cyc_key_parse(value, &v)) actions[i].key = v;
        if (!strcmp(key, actions[i].pad_name) && cyc_pad_parse(value, &v)) actions[i].pad = v;
    }
}
static void game_save(void *ctx, FILE *f) {
    (void)ctx;
    fax_controls_save_settings(f);
    for (int i = 0; i < N_ACTIONS; ++i) {
        char k[32], p[48];
        cyc_key_text(actions[i].key, k, sizeof k);
        cyc_pad_text(actions[i].pad, p, sizeof p);
        fprintf(f, "%s = %s\n%s = %s\n", actions[i].key_name, k, actions[i].pad_name, p);
    }
}
static bool game_loaded(void *ctx) {
    (void)ctx;
    return fax_controls_settings_loaded();
}
static const CycSettingsGame GAME = { NULL, game_load, game_save, game_loaded };

enum { A = 0x80, B = 0x40, SELECT = 0x20, START = 0x10, UP = 0x08, DOWN = 0x04 }; /* P_N and P_Z: fax_controls.c */
#define CHANGED ram[0x19]

static void fresh(void) {
    deactivate();
    activate();
    memset(ram, 0, sizeof ram);
    cpu = (CycModRegs){ .s = 0xFD };
    defaults();
    cyc_bindings_default(&window);
}

/* ---- the hooks ---- */

static void start_opens_the_menu(void) {
    fresh();
    CHECK(hook_on[0] == 1 && hook_on[1] == 1 && hook_on[2] == 1);
    /* GameLoop_CheckShowPlayerMenu: Start new is Select new; the rest stays. */
    CHANGED = START | A | UP;
    CHECK(menu_hook(0xE016) == 0);
    CHECK(CHANGED == (SELECT | A | UP) && starts == 1);
    /* Select (Backslash, or Back with the map unbound) still opens it. */
    CHANGED = SELECT;
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT && starts == 1);
    CHANGED = START | SELECT;
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT && starts == 2);
    CHANGED = 0;
    menu_hook(0xE016);
    CHECK(CHANGED == 0);
}

static void start_never_pauses(void) {
    fresh();
    /* GameLoop_CheckPauseGame: LDA Joy1_ChangedButtonMask, then the hook, then AND #$10. */
    cpu.a = START | B;
    CHECK(pause_hook(0xE02D) == 0);
    CHECK(cpu.a == B && !(cpu.a & 0x10) && unpaused == 1);
    cpu.a = A;
    pause_hook(0xE02D);
    CHECK(cpu.a == A && unpaused == 1);
}

static void start_closes_the_menu(void) {
    fresh();
    /* @_inputLoop: LDA Joy1_ChangedButtonMask, the hook, BMI (A), ASL A, BPL (no B), JMP TextBox_Close. */
    cpu.a = START;
    cpu.p = P_Z; /* whatever the load left, the hook leaves a load's flags */
    CHECK(close_hook(0x8AE8) == 0);
    CHECK(!(cpu.p & P_N) && !(cpu.p & P_Z)); /* BMI not taken */
    CHECK((uint8_t)(cpu.a << 1) & 0x80);     /* ASL: B in bit 7, BPL not taken: the menu closes */
    CHECK(closes == 1);
    /* A with Start: A chooses, as the game decides; nothing changed. */
    cpu.a = A | START;
    cpu.p = P_N;
    close_hook(0x8AE8);
    CHECK(cpu.a == (A | START) && cpu.p == P_N && closes == 1);
    /* Up or Down: the cursor's, no close. */
    cpu.a = DOWN;
    cpu.p = 0;
    close_hook(0x8AE8);
    CHECK(cpu.a == DOWN && closes == 1);
}

static void off_means_off(void) {
    fresh();
    deactivate();
    CHECK(hook_on[0] == 0 && hook_on[1] == 0 && hook_on[2] == 0);
    CHANGED = START;
    menu_hook(0xE016);
    cpu.a = START;
    pause_hook(0xE02D);
    CHECK(CHANGED == START && cpu.a == START);
}

/* ---- in play, Back is the map's ---- */

static void back_is_the_maps(void) {
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("controls_test: no SDL controllers here (%s): Back not checked\n", SDL_GetError());
        return;
    }
    fresh();
    defaults();
    cyc_bindings_default(&window); /* the NES Select on Back, as the host has it */
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    int index = SDL_JoystickAttachVirtualEx(&desc);
    SDL_Joystick *joy = SDL_JoystickOpen(index);
    SDL_GameController *c = SDL_GameControllerOpen(index);
    CHECK(joy && c);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    SDL_GameControllerUpdate();
    /* A Select with Back up (Backslash, a TCP action): the menu's, as ever. */
    CHANGED = SELECT | A;
    menu_hook(0xE016);
    CHECK(CHANGED == (SELECT | A) && kept == 0);
    /* Back held: the Select it presses is the Map button's, so no menu. */
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 1);
    SDL_GameControllerUpdate();
    CHANGED = SELECT | A;
    menu_hook(0xE016);
    CHECK(CHANGED == A && kept == 1);
    /* Start with it: Start still opens the menu. */
    CHANGED = SELECT | START;
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT && kept == 2 && starts == 1);
    /* The map rebound off Back: Back is the NES Select's alone again. */
    actions[3].pad = CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    CHANGED = SELECT;
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT && kept == 2);
    /* The NES Select moved to another button: Back held is no Select of its. */
    actions[3].pad = CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK);
    window.button[0][2].pad = CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT && kept == 2);
    /* Both on a trigger, held past half travel: the map's. */
    actions[3].pad = window.button[0][2].pad = CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 0);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MAX);
    SDL_GameControllerUpdate();
    menu_hook(0xE016);
    CHECK(CHANGED == 0 && kept == 3);
    /* The remaster off: nothing. */
    deactivate();
    CHANGED = SELECT;
    menu_hook(0xE016);
    CHECK(CHANGED == SELECT);
    SDL_GameControllerClose(c);
    SDL_JoystickClose(joy);
    SDL_JoystickDetachVirtual(index);
    SDL_Quit();
}

/* ---- config.ini from before the layout ---- */

static const char *const OLD_CONFIG =
    "[Display]\nWindowScale = 3\n"
    "[Keyboard.Shortcuts]\ndisk = D\nmenu = Escape\nfast_forward = Tab\nscreenshot = F12\nfullscreen = F11\n"
    "state_menu = F7\nrewind = F8\n"
    "[Gamepad.Shortcuts]\ndisk = leftshoulder\nmenu = rightshoulder\nfast_forward = leftstick\nscreenshot = none\n"
    "fullscreen = none\nstate_menu = back+leftstick\nrewind = back+rightstick\n"
    "[Game]\nViewMode = 0\nMagicKey = C\nMagicButton = leftshoulder\nBlockKey = V\nBlockButton = b\n"
    "MapKey = M\nMapButton = lefttrigger+\nQuestLogKey = L\nQuestLogButton = rightstick\n";

static char *slurp(const char *path) {
    static char all[8192];
    FILE *f = fopen(path, "r");
    CHECK(f);
    size_t n = fread(all, 1, sizeof all - 1, f);
    fclose(f);
    all[n] = 0;
    return all;
}

static void an_old_config(const char *dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/controls_test_config.ini", dir);
    FILE *f = fopen(path, "w");
    CHECK(f);
    fputs(OLD_CONFIG, f);
    fclose(f);
    defaults();
    CycSettings s;
    window_now = &s.bind;
    cyc_settings_default(&s);
    CHECK(cyc_settings_load(&s, path, stderr, &GAME));
    CHECK(s.migrated); /* the host saves it at once */
    /* The game's: each still on its old default moved, Run came in on LT and Left Shift; the player's Magic
     * on LB stays. The quest log's move to L3 gives way to the player's Fast-forward there: it keeps L alone. */
    CHECK(actions[2].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1));  /* Block: B -> RT */
    CHECK(actions[3].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK));         /* Map: LT -> Back */
    CHECK(actions[4].pad == 0);                                                  /* Quest log: R3 -> (L3) */
    CHECK(actions[5].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));   /* Run: LT */
    CHECK(actions[0].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); /* Magic: the player's */
    CHECK(actions[1].key == SDL_SCANCODE_S && actions[1].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B));
    for (int i = 0; i < N_ACTIONS; ++i) CHECK(actions[i].key == actions[i].b.default_key);
    CHECK(moved == 4 && changes == 4 && unbound == 1);
    /* The host's: the shortcuts on layout 1's defaults moved, Fast-forward on L3 the player's. */
    CHECK(s.bind.shortcut[CYC_SC_MENU].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    CHECK(s.bind.shortcut[CYC_SC_REWIND].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    CHECK(s.moved_shortcuts == (1u << CYC_SC_DISK | 1u << CYC_SC_MENU | 1u << CYC_SC_STATE_MENU | 1u << CYC_SC_REWIND));
    /* Rewind landed on LB, which the player gave Magic: the host gives way (the window does this after the
     * load, with the game's bindings), and Rewind keeps only F8. */
    int pads[N_ACTIONS];
    for (int i = 0; i < N_ACTIONS; ++i) pads[i] = actions[i].pad;
    cyc_settings_yield_moved(&s, pads, N_ACTIONS);
    CHECK(s.bind.shortcut[CYC_SC_REWIND].pad == 0 && s.bind.shortcut[CYC_SC_REWIND].key == SDL_SCANCODE_F8);
    CHECK(s.bind.shortcut[CYC_SC_MENU].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    CHECK(s.bind.shortcut[CYC_SC_FAST_FORWARD].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(s.bind.shortcut[CYC_SC_DISK].pad == 0 && s.bind.shortcut[CYC_SC_STATE_MENU].pad == 0);
    CHECK(s.window_scale == 3);
    /* Saved: both layouts written, the bindings as they now are. */
    CHECK(cyc_settings_save(&s, path, &GAME));
    const char *text = slurp(path);
    CHECK(strstr(text, "BindingsLayout = 2\n") && strstr(text, "ControlsLayout = 3\n"));
    CHECK(strstr(text, "BlockButton = righttrigger+\n") && strstr(text, "MapButton = back\n") &&
          strstr(text, "QuestLogButton = none\n") && strstr(text, "MagicButton = leftshoulder\n") &&
          strstr(text, "UseItemKey = S\nUseItemButton = b\n") &&
          strstr(text, "RunKey = Left Shift\nRunButton = lefttrigger+\n"));
    /* Once: Block set back to B in a file of the layout stays there. */
    actions[2].pad = CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B);
    CHECK(cyc_settings_save(&s, path, &GAME));
    defaults();
    cyc_settings_default(&s);
    CHECK(cyc_settings_load(&s, path, stderr, &GAME));
    CHECK(!s.migrated && moved == 0 && changes == 0);
    CHECK(actions[2].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B));
    CHECK(s.bind.shortcut[CYC_SC_MENU].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    /* A file the game never wrote (no [Game]): only Run comes in, and it is written with the layout. */
    f = fopen(path, "w");
    fputs("[Display]\nWindowScale = 2\n", f);
    fclose(f);
    defaults();
    cyc_settings_default(&s);
    CHECK(cyc_settings_load(&s, path, stderr, &GAME));
    CHECK(s.migrated && moved == 1 && unbound == 0 && actions[2].pad == actions[2].b.default_pad);
    /* A layout that is no number reads as 1, and moves. */
    f = fopen(path, "w");
    fputs("[Game]\nControlsLayout = new\nBlockButton = b\n", f);
    fclose(f);
    defaults();
    cyc_settings_default(&s);
    CHECK(cyc_settings_load(&s, path, NULL, &GAME));
    CHECK(moved == 2 && actions[2].pad == actions[2].b.default_pad);
    /* A move onto a button the player kept for something else gives way: Magic on Back, so the map stays off
     * the controller; the NES B on the right trigger, so Block does too. The quest log moves as ever. */
    f = fopen(path, "w");
    fputs("[Gamepad.Player1]\nb = righttrigger+\n[Game]\nMagicButton = back\nBlockButton = b\n"
          "MapButton = lefttrigger+\nQuestLogButton = rightstick\n",
          f);
    fclose(f);
    defaults();
    cyc_settings_default(&s);
    CHECK(cyc_settings_load(&s, path, NULL, &GAME));
    CHECK(moved == 4 && unbound == 2 && changes == 4);
    CHECK(actions[3].pad == 0 && actions[2].pad == 0 && actions[3].key == SDL_SCANCODE_M);
    CHECK(actions[4].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(actions[5].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));
    CHECK(actions[0].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK));
    window_now = &window;
    remove(path);
}

/* A file of layout 2: the left trigger goes to Run and the quest log to the
 * left stick's press, unless the player bound them; a move onto a button the
 * player kept gives way, and so does Run's key. */
static CycSettings layout2_settings;
static void load_layout2(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f);
    fputs(text, f);
    fclose(f);
    defaults();
    cyc_settings_default(&layout2_settings);
    window_now = &layout2_settings.bind;
    CHECK(cyc_settings_load(&layout2_settings, path, NULL, &GAME));
}
static void a_layout2_config(const char *dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/controls_test_layout2.ini", dir);
    const char *const DEFAULTS2 = "[Input]\nBindingsLayout = 2\n[Game]\nControlsLayout = 2\nMagicKey = C\n"
                                  "MagicButton = y\nUseItemKey = S\nUseItemButton = b\nBlockKey = V\n"
                                  "BlockButton = righttrigger+\nMapKey = M\nMapButton = back\nQuestLogKey = L\n"
                                  "QuestLogButton = lefttrigger+\n";
    load_layout2(path, DEFAULTS2);
    CHECK(layout2_settings.migrated && moved == 2 && unbound == 0 && changes == 2);
    CHECK(actions[4].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK) && actions[4].key == SDL_SCANCODE_L);
    CHECK(actions[5].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1) && actions[5].key == SDL_SCANCODE_LSHIFT);
    for (int i = 0; i < 4; ++i) CHECK(actions[i].pad == actions[i].b.default_pad);
    /* The player's own: the map on LT and the quest log on L3. Both stay, and Run's LT gives way to the map. */
    load_layout2(path, "[Game]\nControlsLayout = 2\nMapButton = lefttrigger+\nQuestLogButton = leftstick\n");
    CHECK(moved == 1 && unbound == 1);
    CHECK(actions[3].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));
    CHECK(actions[4].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(actions[5].pad == 0 && actions[5].key == SDL_SCANCODE_LSHIFT);
    /* Magic on L3 and the NES B on Left Shift, both the player's: the quest log's move to L3 gives way (L stays),
     * and so does Run's Left Shift (the trigger stays). */
    load_layout2(path, "[Keyboard.Player1]\nb = Left Shift\n[Game]\nControlsLayout = 2\nMagicButton = leftstick\n"
                       "QuestLogButton = lefttrigger+\n");
    CHECK(moved == 2 && unbound == 2 && changes == 2);
    CHECK(actions[0].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    CHECK(actions[4].pad == 0 && actions[4].key == SDL_SCANCODE_L);
    CHECK(actions[5].key == 0 && actions[5].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));
    /* Left Shift kept by a host shortcut, or by another of the game's actions, gives way too. */
    load_layout2(path, "[Keyboard.Shortcuts]\nscreenshot = Left Shift\n[Game]\nControlsLayout = 2\n");
    CHECK(actions[5].key == 0 && unbound == 1);
    load_layout2(path, "[Game]\nControlsLayout = 2\nBlockKey = Left Shift\n");
    CHECK(actions[5].key == 0 && actions[2].key == SDL_SCANCODE_LSHIFT && unbound == 1);
    /* A file of this layout: nothing moves, and Run's bindings are the file's. */
    load_layout2(path,
                 "[Display]\nDisplayDefaults = 2\n[Input]\nBindingsLayout = 2\n[Game]\nControlsLayout = 3\nRunKey = G\n"
                 "RunButton = rightshoulder\n"
                 "QuestLogButton = lefttrigger+\n");
    CHECK(!layout2_settings.migrated && moved == 0 && unbound == 0 && changes == 0);
    CHECK(actions[5].key == SDL_SCANCODE_G && actions[5].pad == CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    CHECK(actions[4].pad == CYC_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1));
    window_now = &window;
    remove(path);
}

static void the_menus_row(void) {
    cyc_bindings_default(&window);
    fax_controls_power_on();
    CHECK(!strcmp(fax_controls_menu_label(), "Select menu: RETURN / START"));
    window.button[0][3] = (CycBinding){ 0, 0 };
    fax_controls_power_on();
    CHECK(!strcmp(fax_controls_menu_label(), "Select menu: unbound"));
}

int main(int argc, char **argv) {
    start_opens_the_menu();
    start_never_pauses();
    start_closes_the_menu();
    off_means_off();
    back_is_the_maps();
    an_old_config(argc > 1 ? argv[1] : ".");
    a_layout2_config(argc > 1 ? argv[1] : ".");
    the_menus_row();
    printf("controls_test: %u checks passed\n", checks);
    return 0;
}
