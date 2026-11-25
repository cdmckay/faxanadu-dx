/* Faxanadu DX on the cycle host: every feature's power-on, frame,
 * present, input, settings and TCP calls, and the content tools' options
 * (text overrides, tile overrides). Saving is Save files' (fax_files.c). */
#include "cyc_host_extras.h"
#include "cyc_mod.h"
#include "cyc_core.h"
#include "override_text.h"
#include "override_chr.h"
#include "fax_world.h"
#include "fax_travel.h"
#include "fax_death.h"
#include "fax_pendant.h"
#include "fax_magic.h"
#include "fax_banner.h"
#include "fax_speaker.h"
#include "fax_keys.h"
#include "fax_falling.h"
#include "fax_jumps.h"
#include "fax_ladders.h"
#include "fax_ladder_hold.h"
#include "fax_text_speed.h"
#include "fax_files.h"
#include "fax_title.h"
#include "fax_dagger.h"
#include "fax_block.h"
#include "fax_sugata.h"
#include "fax_shops.h"
#include "fax_respawn.h"
#include "fax_recharge.h"
#include "fax_charges.h"
#include "fax_item_box.h"
#include "fax_mana.h"
#include "fax_lesson.h"
#include "fax_potions.h"
#include "fax_gold.h"
#include "fax_slain.h"
#include "fax_indoor.h"
#include "fax_damage.h"
#include "fax_inventory.h"
#include "fax_fixes.h"
#include "fax_finds.h"
#include "fax_shaft.h"
#include "fax_map.h"
#include "fax_quests.h"
#include "fax_graphics.h"
#include "fax_flicker.h"
#include "fax_entrances.h"
#include "fax_spell_cost.h"
#include "fax_found.h"
#include "fax_menu.h"
#include "fax_controls.h"
#include "fax_use_item.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <string.h>
#include <stdlib.h>
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"
#endif

static uint64_t frame;
static bool text_enabled, tiles_enabled, tile_dump;
static char text_path[1100] = "text_overrides.json", tiles_path[1100] = "tiles", compile_path[1100];

static uint8_t title_encode(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (uint8_t)(0xE0 + ch - 'A') : ch == ' ' ? 0x20 : 0xFF;
}
static uint8_t dialogue_encode(char ch) {
    return ch >= 0x21 && ch <= 0x7E ? (uint8_t)ch : ch == ' ' ? 0xFD : ch == '\n' ? 0xFC : 0xFF;
}
static uint8_t ascii_encode(char ch) { return ch >= 0x20 && ch <= 0x7E ? (uint8_t)ch : 0xFF; }

static bool sidecar_path(const char *name, char out[1100]) {
#ifdef CYC_WITH_SDL
    char *base = SDL_GetBasePath();
    if (!base) return false;
    int n = snprintf(out, 1100, "%s%s", base, name);
    SDL_free(base);
#else
    int n = snprintf(out, 1100, "%s", name);
#endif
    return n > 0 && n < 1100;
}
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
bool faxanadu_replace_file(const char *from, const char *to) {
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#endif

static void power_on(void *ctx) {
    (void)ctx; frame = 0;
    fax_world_reset();
    fax_travel_step();
    fax_banner_reset();
    fax_files_power_on();
    fax_title_power_on();
    fax_controls_power_on();
    fax_dagger_step();
    if (compile_path[0]) {
        int n = chr_override_compile_dir(compile_path);
        exit(n < 0 ? 2 : 0);
    }
    if (text_enabled) {
        text_override_init();
        text_override_register_encoding("FAXANADU_1", title_encode, 0);
        text_override_register_encoding("FAXANADU_DIALOGUE", dialogue_encode, 0xFF);
        text_override_register_encoding("ASCII", ascii_encode, 0);
        text_override_load_json(text_path);
    }
    if (!tiles_enabled) {
        char path[1100];
        if (sidecar_path("tiles/manifest.json", path)) {
            FILE *f = fopen(path, "r");
            if (f) { fclose(f); tiles_enabled = sidecar_path("tiles", tiles_path); }
        }
    }
    if (tiles_enabled || tile_dump) {
        chr_override_init();
        if (tile_dump) chr_override_set_dump(1);
        if (tiles_enabled) chr_override_load_manifest(tiles_path);
    }
}
static void frame_begin(void *ctx) {
    (void)ctx;
    if (text_enabled) { text_override_reload_if_changed(); text_override_apply(); }
    if (tiles_enabled) chr_override_reload_if_changed();
}
#ifdef CYC_WITH_SDL
/* The end-to-end tests' frame-exact wait (tests/e2e/fax_e2e.py's wait()):
 * `wait` answers once `frames` more frames have run, at the end of the last
 * one, so a test waits on the game without polling it and without a frame
 * more than it asked for. One at a time: the tests' client is one line. */
static int wait_id = -1;
static long wait_left;
static void tcp_wait(int id, const char *line) {
    long n = 1;
    cyc_tcp_long(line, "frames", &n);
    if (wait_id >= 0) { cyc_tcp_err(id, "wait: another wait is running"); return; }
    if (n < 1 || n > 36000) { cyc_tcp_err(id, "wait: frames is 1-36000"); return; }
    wait_id = id; wait_left = n;
}
static void wait_frame_end(void) {
    if (wait_id < 0 || --wait_left > 0) return;
    int id = wait_id;
    wait_id = -1;
    cyc_tcp_ok(id, NULL);
}
#endif
static void frame_end(void *ctx) {
    (void)ctx; ++frame;
#ifdef CYC_WITH_SDL
    wait_frame_end();
#endif
    fax_world_step();
    fax_travel_step();
    fax_magic_frame_end();
    fax_use_item_frame_end();
    fax_mana_frame_end();
    fax_banner_step();
    fax_slain_frame_end();
    fax_charges_frame_end();
    fax_files_frame_end();
    fax_title_frame_end();
    fax_dagger_step();
    fax_block_frame_end();
    fax_sugata_frame_end();
    fax_damage_frame_end();
    fax_map_frame_end();
    fax_quests_frame_end();
    if (tiles_enabled || tile_dump) chr_override_frame_end();
}
static bool option(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if(!strncmp(name,"--world-",8))return fax_world_option(name,value);
    char *path = NULL;
    if (!strcmp(name, "--text-overrides")) { path = text_path; text_enabled = true; }
    else if (!strcmp(name, "--tiles")) { path = tiles_path; tiles_enabled = true; }
    else if (!strcmp(name, "--tile-compile")) path = compile_path;
    else if (!strcmp(name, "--tile-dump")) { tile_dump = true; return true; }
    if (!path || !value || strlen(value) >= 1100) return false;
    snprintf(path, 1100, "%s", value);
    return true;
}
#ifdef CYC_WITH_SDL
static void tcp_setup(void *ctx) {
    (void)ctx;
    cyc_tcp_register("wait", "Answer once `frames` more frames have run (default 1): a frame-exact wait", tcp_wait);
    fax_world_tcp_setup();
    fax_travel_tcp_setup();
    fax_death_tcp_setup();
    fax_pendant_tcp_setup();
    fax_magic_tcp_setup();
    fax_banner_tcp_setup();
    fax_speaker_tcp_setup();
    fax_keys_tcp_setup();
    fax_falling_tcp_setup();
    fax_jumps_tcp_setup();
    fax_ladders_tcp_setup();
    fax_ladder_hold_tcp_setup();
    fax_text_speed_tcp_setup();
    fax_files_tcp_setup();
    fax_title_tcp_setup();
    fax_dagger_tcp_setup();
    fax_block_tcp_setup();
    fax_sugata_tcp_setup();
    fax_shops_tcp_setup();
    fax_respawn_tcp_setup();
    fax_recharge_tcp_setup();
    fax_charges_tcp_setup();
    fax_item_box_tcp_setup();
    fax_mana_tcp_setup();
    fax_lesson_tcp_setup();
    fax_potions_tcp_setup();
    fax_gold_tcp_setup();
    fax_slain_tcp_setup();
    fax_indoor_tcp_setup();
    fax_damage_tcp_setup();
    fax_inventory_tcp_setup();
    fax_fixes_tcp_setup();
    fax_map_tcp_setup();
    fax_quests_tcp_setup();
    fax_graphics_tcp_setup();
    fax_flicker_tcp_setup();
    fax_finds_tcp_setup();
    fax_shaft_tcp_setup();
    fax_entrances_tcp_setup();
    fax_spell_cost_tcp_setup();
    fax_found_tcp_setup();
    fax_menu_tcp_setup();
    fax_controls_tcp_setup();
    fax_use_item_tcp_setup();
}
#endif
static const uint32_t *present(void *ctx,int *w,int *h) {
    (void)ctx;
    const uint32_t *picture=fax_quests_marks_present(fax_damage_present(fax_sugata_present(fax_spell_cost_present(fax_item_box_present(fax_charges_present(fax_world_present(w,h,cyc_frame_argb()),w,h),w,h),w,h),w,h),w,h),w,h);
    picture=fax_files_present(fax_title_present(fax_speaker_present(fax_recharge_present(fax_quests_banner_present(fax_slain_present(fax_banner_present(picture,w,h),w,h),w,h),w,h),w,h),w,h),w,h);
    return fax_quests_log_present(fax_map_present(fax_shops_present(picture,w,h),w,h),w,h);
}
static void event(void *ctx,const void *e,int player) {
    (void)ctx;(void)player;
    fax_world_event(e);
    fax_magic_event(e);
    fax_use_item_event(e);
    fax_block_event(e);
    fax_map_event(e);
    fax_quests_event(e);
}
static void input(void *ctx, uint8_t buttons[2]) { (void)ctx; fax_block_input(buttons); fax_use_item_input(buttons); }
static void load_setting(void *ctx, const char *key, const char *value) {
    (void)ctx;
    if (!fax_controls_load_setting(key, value) && !fax_magic_load_setting(key, value) &&
        !fax_use_item_load_setting(key, value) && !fax_block_load_setting(key, value) &&
        !fax_map_load_setting(key, value))
        fax_quests_load_setting(key, value);
}
static void save_settings(void *ctx, FILE *f) {
    (void)ctx;
    fax_controls_save_settings(f);
    fax_magic_save_settings(f); fax_use_item_save_settings(f); fax_block_save_settings(f);
    fax_map_save_settings(f); fax_quests_save_settings(f);
}
static bool settings_loaded(void *ctx) { (void)ctx; return fax_controls_settings_loaded(); }
/* The launcher's Controls page lists these after the host's shortcuts (a
 * window build: each feature's binding is there). */
static CycHostBinding bindings[5];
static void list_bindings(void) {
    const CycHostBinding *(*const each[])(void) = {
        fax_magic_binding, fax_use_item_binding, fax_block_binding, fax_map_binding, fax_quests_binding,
    };
    for (size_t i = 0; i < sizeof each / sizeof each[0]; ++i) {
        const CycHostBinding *b = each[i]();
        if (b) bindings[i] = *b;
    }
}
#ifdef CYC_WITH_RECOMP_UI
/* The runtime menu's Controls section: Start's Select menu, then the Magic, Use item, Block, Map and
 * Quest log buttons' bindings, information rows like the host's own shortcuts. */
static RecompRuntimeUiItem menu_rows[] = {
    { "faxanadu.select-menu", "Controls", "Select menu", "Opens QUESTS, MAP and your gear. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
    { "faxanadu.magic", "Controls", "Magic", "Casts like Up+B. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
    { "faxanadu.use-item", "Controls", "Use item", "Uses your item like Down+B. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
    { "faxanadu.block", "Controls", "Block", "Hold with a shield to stop magic. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
    { "faxanadu.map", "Controls", "Map", "Shows the screens you've been to. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
    { "faxanadu.quests", "Controls", "Quest log", "Lists your quests. Rebind it in the launcher.",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
};
static int menu_row_enabled(void *ctx, const RecompRuntimeUiItem *it) { (void)ctx; (void)it; return 0; }
static const RecompRuntimeUiCallbacks menu_callbacks = { .is_enabled = menu_row_enabled };
#endif
static void state_loaded(void *ctx) {(void)ctx;fax_world_loaded();fax_block_loaded();fax_files_state_loaded();fax_quests_loaded();fax_recharge_loaded();fax_gold_loaded();fax_lesson_loaded();fax_potions_loaded();}
const CycHostExtras *cyc_host_extras(void) {
    static const CycHostOption options[] = {
        { "--text-overrides", true, "Text replacement JSON, with hot reload" },
        { "--tiles", true, "Tile override directory, with hot reload" },
        { "--tile-dump", false, "Dump unique CHR transfers as PNG and binary assets" },
        { "--tile-compile", true, "Compile tile PNGs to CHR caches and exit" },
        { "--world-view", true, "World camera: off, seamless or atlas" },
    };
#ifdef CYC_WITH_RECOMP_UI
    menu_rows[0].label = fax_controls_menu_label();
    menu_rows[1].label = fax_magic_controls_label();
    menu_rows[2].label = fax_use_item_controls_label();
    menu_rows[3].label = fax_block_controls_label();
    menu_rows[4].label = fax_map_controls_label();
    menu_rows[5].label = fax_quests_controls_label();
#endif
    list_bindings();
    static const CycHostExtras extras = {
        .present=present,.event=event,.input=input,
        .load_setting = load_setting, .save_settings = save_settings, .settings_loaded = settings_loaded,
        .bindings = bindings, .binding_count = sizeof bindings / sizeof bindings[0],
#ifdef CYC_WITH_RECOMP_UI
        .menu_items = menu_rows, .menu_item_count = sizeof(menu_rows) / sizeof(menu_rows[0]),
        .menu_callbacks = &menu_callbacks,
#endif
        .state_loaded=state_loaded,
        .power_on = power_on, .frame_begin = frame_begin, .frame_end = frame_end,
        .options = options, .option_count = sizeof(options) / sizeof(options[0]), .option = option,
#ifdef CYC_WITH_SDL
        .tcp_setup = tcp_setup,
#endif
    };
    return &extras;
}
