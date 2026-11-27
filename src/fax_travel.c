/* Guru travel. USA 57dd23d1, chipx86 byte-exact disassembly.
 * After the mantra (or a new title), a temple's Guru offers to send the hero to
 * another Guru the hero has visited. With Save files and a file in play, Save
 * files asks first (fax_files.c) and starts this part through
 * fax_travel_begin. The ROM image is never changed (save states hash it): the
 * offer runs in the game's own interaction-script interpreter and text box,
 * steered from C at IScripts_InvokeNextAction. Two script steps and one
 * message slot sit in unused ROM, supplied through read overrides, and C
 * rewrites the slot before each line. The journey is the respawn path without
 * its penalties: HP, MP, gold, experience, timed items and quests are kept,
 * and so is the spawn point. The visited Gurus are kept in the file in play
 * (its `gurus` field), or without Save files in faxanadu_gurus.txt.
 * See docs/GAME_INTERNALS.md. */
#include "fax_travel.h"
#include "fax_files.h"
#include "fax_save.h"
#include "cyc_mod.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEMPLES = 8, SLOT_LEN = FAX_TRAVEL_SLOT_LEN };

/* RAM */
#define TEMPLE_SPAWN_POINT 0x0439u   /* the original's furthest temple; SET_SPAWN_POINT never lowers it */
#define CURRENT_ROM_BANK   0x0100u
#define ISCRIPT_ADDR       0x00DBu   /* IScriptOrCHRAddr; the next action is at it + IScriptOffset */
#define ISCRIPT_OFFSET     0x00DDu
/* Fixed bank */
#define SCREEN_FADE_TO_BLACK    0xDA2Fu
#define GAME_INIT_STATE_SPAWN   0xDA7Du
#define PLAYER_SPAWN_IN_TEMPLE  0xDB1Du   /* Player_Spawn's JSR Game_SpawnInTemple; then
                                             JSR Game_SetupEnterBuilding, JMP Game_MainLoop */
/* Bank 12: where the Gurus' scripts end, and two steps of ours in unused ROM. */
#define GURU_PASSWORD_END   0xA5FFu   /* ISCRIPT_COMMON_GURU_PASSWORD: 03 22, 14, here 00 */
#define GURU_NEW_RANK_END   0xA602u   /* ISCRIPT_COMMON_GURU_NEW_RANK: 01 23, here 00 */
#define EOLIS_GURU_HAS_RING 0xA0C6u   /* Eolis's Guru, who sets no spawn point: 03 2D, 00 */
#define SAY FAX_TRAVEL_SAY            /* $AE00: 03 C3 00: show the slot, then END */
#define ASK FAX_TRAVEL_ASK            /* $AE03: 16 C3 SAY: ask; A jumps to SAY, B goes on to ASK + 4 */
/* Bank 13: Messages_Load counts $FF terminators from $8300. Message 193 is
 * followed by glyph data whose run ends at the $FF at $B45A, so message $C3
 * starts at $B45B, in unused ROM. */
#define SLOT_ID FAX_TRAVEL_SLOT_ID    /* $C3 */
#define SLOT    FAX_TRAVEL_SLOT       /* $B45B */
/* IScript actions */
enum { END = 0x00, SHOW_MESSAGE = 0x03, SET_SPAWN_POINT = 0x06, QUESTION_JUMP_IF_ACCEPTED = 0x16 };

static const char *const town[TEMPLES] = {
    "Eolis", "Apolune", "Forepaw", "Mascon", "Victim", "Conflate", "Daybreak", "Dartmoor" };
/* The Guru's lines: 16 columns, 4 lines to a box; '\n' breaks a line. */
enum { TEXT_INTRO, TEXT_OFFER, TEXT_FAREWELL, TEXT_DEPART, TEXT_TO };   /* TEXT_TO + temple */
static const char *const lines[] = {
    "All Gurus\nmeditate as one.", "Shall I send\nyou to another\nGuru?",
    "Then go in\npeace.", "Close your eyes\nand clear your\nmind." };

/* One conversation with a Guru. INTRO_TOWNS is the intro before asking about
 * the towns at once, when Save files' menu chose travel. */
typedef enum { IDLE, INTRO, OFFER, ASKING, FAREWELL, DEPART, INTRO_TOWNS } Phase;
typedef struct { uint8_t phase, offer, asking; } Talk;
/* What the interpreter reads next: the address (or the one it was going to
 * read), the line for the slot (or -1) and a chosen temple (or -1). */
typedef struct { uint16_t at; int text, go; } Steer;

static bool enabled, served, failed;
static uint8_t visited = 1;          /* bit per temple; Eolis is where every game begins */
static Talk talk;
static int text = -1;                /* the line in the slot */
static int dest = -1;                /* chosen temple, until the journey starts */
static bool journey;                 /* the respawn path is running toward dest */

static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

/* Encode a line in the dialogue font, $FF-terminated; false if it would not
 * fit the box or the slot. */
static bool encode(const char *s, uint8_t out[SLOT_LEN]) {
    unsigned n = 0, col = 0, row = 1;
    for (; *s; ++s) {
        uint8_t v;
        if (*s == '\n') v = 0xFE, col = 0, ++row;
        else if (*s == ' ') v = 0xFD, ++col;
        else if (*s > 0x20 && *s < 0x7F) v = (uint8_t)*s, ++col;
        else return false;
        if (col > 16 || row > 4 || n + 1 >= SLOT_LEN) return false;
        out[n++] = v;
    }
    out[n++] = 0xFF;
    while (n < SLOT_LEN) out[n++] = 0xFF;
    return true;
}
static bool line_for(int which, uint8_t out[SLOT_LEN]) {
    char buf[40];
    if (which >= TEXT_TO) snprintf(buf, sizeof buf, "To the Guru of\n%s?", town[which - TEXT_TO]);
    return encode(which >= TEXT_TO ? buf : lines[which], out);
}

/* Gurus to offer from `here`: those visited up to the furthest temple. */
static uint8_t offered(unsigned here, unsigned furthest, uint8_t seen) {
    if (furthest >= TEMPLES) furthest = 0;
    if (here > furthest) furthest = here;
    uint8_t reach = (uint8_t)((2u << furthest) - 1);
    return (uint8_t)(((seen & reach) | 1u | 1u << furthest) & ~(1u << here));
}
static int first_after(int t, uint8_t offer) {
    for (int u = t + 1; u < TEMPLES; ++u) if (offer >> u & 1) return u;
    return -1;
}

/* The conversation, one interpreter step at a time. */
static Steer steer(Talk *k, uint16_t at) {
    Steer s = { at, -1, -1 };
    if (at == GURU_PASSWORD_END || at == GURU_NEW_RANK_END || at == EOLIS_GURU_HAS_RING + 2) {
        if (fax_files_guru_choice()) return s;      /* Save files asks first, and calls fax_travel_begin */
        if (k->offer) k->phase = INTRO, s.at = SAY, s.text = TEXT_INTRO;
        else k->phase = IDLE;
    } else if (at == SAY + 2) {                      /* a line of ours has been read */
        if (k->phase == INTRO) k->phase = OFFER, s.at = ASK, s.text = TEXT_OFFER;
        else if (k->phase == INTRO_TOWNS) {
            k->phase = ASKING; k->asking = (uint8_t)first_after(-1, k->offer);
            s.at = ASK; s.text = TEXT_TO + k->asking;
        } else k->phase = IDLE;                      /* the farewell or the departure: END */
    } else if (at == SAY) {                          /* A on ASK */
        if (k->phase == OFFER) {
            k->phase = ASKING; k->asking = (uint8_t)first_after(-1, k->offer);
            s.at = ASK; s.text = TEXT_TO + k->asking;
        } else if (k->phase == ASKING) {
            k->phase = DEPART; s.text = TEXT_DEPART; s.go = k->asking;
        }
    } else if (at == ASK + 4) {                      /* B on ASK */
        if (k->phase == OFFER) {
            k->phase = IDLE; s.at = SAY + 2;         /* an END: B on the offer closes the box */
        } else if (k->phase == ASKING) {
            int u = first_after(k->asking, k->offer);
            if (u >= 0) k->asking = (uint8_t)u, s.at = ASK, s.text = TEXT_TO + u;
            else k->phase = FAREWELL, s.at = SAY, s.text = TEXT_FAREWELL;
        }
    }
    return s;
}

static bool serve(int which) {
    uint8_t bytes[SLOT_LEN];
    if (!line_for(which, bytes)) return false;
    for (unsigned i = 0; i < SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, bytes[i])) return false;
    text = which;
    return true;
}
static bool serve_script(bool on) {
    static const uint8_t steps[] = { SHOW_MESSAGE, SLOT_ID, END,
                                     QUESTION_JUMP_IF_ACCEPTED, SLOT_ID, SAY & 0xFF, SAY >> 8 };
    for (unsigned i = 0; i < sizeof steps; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(SAY + i)), 0xFF, on ? steps[i] : 0xFF)) return false;
    return true;
}
static void unserve(void) {
    serve_script(false);
    for (unsigned i = 0; i < SLOT_LEN; ++i) cyc_mod_set_prg_read_override(prg(13, (uint16_t)(SLOT + i)), 0xFF, 0xFF);
    text = -1;
}

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
/* faxanadu_gurus.txt: the towns whose Gurus this playthrough has visited. */
static void load_visited(void) {
    char path[1100], line[64];
    visited = 1;
    if (!cyc_host_saves_enabled() || !sidecar_path("faxanadu_gurus.txt", path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        for (int t = 0; t < TEMPLES; ++t) if (!strcmp(line, town[t])) visited |= (uint8_t)(1u << t);
    }
    fclose(f);
}
static void save_visited(void) {
    char path[1100], temp[1120];
    if (fax_save_enabled()) return;                  /* the file in play keeps them (on_save) */
    if (!cyc_host_saves_enabled() || !sidecar_path("faxanadu_gurus.txt", path)) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f) return;
    for (int t = 0; t < TEMPLES; ++t) if (visited >> t & 1) fprintf(f, "%s\n", town[t]);
    if (fclose(f) != 0) { remove(temp); return; }
#ifdef _WIN32
    extern bool faxanadu_replace_file(const char *, const char *);
    if (!faxanadu_replace_file(temp, path)) remove(temp);
#else
    if (rename(temp, path) != 0) remove(temp);
#endif
}

/* Save files: the visited Gurus are the file's `gurus` field. */
static void on_save(FaxSaveEvent event) {
    char gurus[FAX_SAVE_VALUE];
    if (event == FAX_SAVE_NEW_GAME) visited = 1;
    else if (event == FAX_SAVE_CONTINUED) visited = (uint8_t)(fax_save_gurus_parse(fax_save_get("gurus")) | 1u);
    else { fax_save_gurus_format(visited, gurus); fax_save_set("gurus", gurus); }
}

/* The furthest temple: the highest visited Guru's, or the spawn point, which
 * is the furthest temple in the original but the Guru last saved with under
 * Save files. */
static unsigned furthest_temple(void) {
    unsigned furthest = cyc_mod_peek(TEMPLE_SPAWN_POINT);
    if (furthest >= TEMPLES) furthest = 0;
    for (unsigned t = furthest + 1; t < TEMPLES; ++t) if (visited >> t & 1) furthest = t;
    return furthest;
}

static void meet(unsigned here) {
    if (!(visited >> here & 1)) { visited |= (uint8_t)(1u << here); save_visited(); }
    talk.phase = IDLE;
    talk.offer = offered(here, furthest_temple(), visited);
}

/* IScripts_InvokeNextAction: meet the Guru, and steer the conversation. */
static int next_action_hook(uint16_t addr) {
    (void)addr;
    if (!served) return 0;
    uint16_t at = (uint16_t)((cyc_mod_peek(ISCRIPT_ADDR) | cyc_mod_peek(ISCRIPT_ADDR + 1) << 8) +
                             cyc_mod_peek(ISCRIPT_OFFSET));
    /* A script begins (IScripts_Begin leaves IScriptOffset 1; every step of
     * ours is entered at 0): a conversation of ours that B ended is over. B on
     * "All Gurus meditate as one." leaves INTRO or INTRO_TOWNS, and the next
     * END of the SAY step that anyone else shows a line through (Block's
     * tutorial, the quest log, the Guru's Elixir line) would have been taken
     * for the intro's, and steered to the offer. */
    if (cyc_mod_peek(ISCRIPT_OFFSET) == 1) talk.phase = IDLE;
    if (cyc_mod_peek(at) == SET_SPAWN_POINT && cyc_mod_peek((uint16_t)(at + 1)) < TEMPLES)
        meet(cyc_mod_peek((uint16_t)(at + 1)));
    else if (at == EOLIS_GURU_HAS_RING)
        meet(0);
    Steer s = steer(&talk, at);
    if (s.text >= 0 && !serve(s.text)) {
        fprintf(stderr, "[travel] cannot supply the Guru's line; Guru travel is off\n");
        failed = true; talk.phase = IDLE; s.at = at; s.go = -1;
    }
    if (s.go >= 0) dest = s.go;
    if (s.at != at) {
        cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)s.at);
        cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(s.at >> 8));
        cyc_mod_poke(ISCRIPT_OFFSET, 0);
    }
    return 0;
}

/* GameLoop_CheckShowPlayerMenu, once the conversation has closed: return
 * into the respawn path with a fresh stack. */
static int main_loop_hook(uint16_t addr) {
    (void)addr;
    if (dest < 0 || journey || cyc_mod_peek(CURRENT_ROM_BANK) != 14) return 0;
    static const uint16_t path[] = { SCREEN_FADE_TO_BLACK, GAME_INIT_STATE_SPAWN, PLAYER_SPAWN_IN_TEMPLE };
    uint16_t sp = 0x1FF;
    for (int i = 2; i >= 0; --i) {
        uint16_t r = (uint16_t)(path[i] - 1);
        cyc_mod_poke(sp--, (uint8_t)(r >> 8));
        cyc_mod_poke(sp--, (uint8_t)r);
    }
    CycModRegs regs;
    cyc_mod_regs(&regs);
    regs.s = (uint8_t)sp;
    cyc_mod_set_regs(&regs);
    journey = true;
    return 1;
}

/* Game_SpawnInTemple: lay out the chosen temple, keeping the furthest one as
 * the spawn point. */
static int spawn_hook(uint16_t addr) {
    if (!journey) return 0;
    uint8_t furthest = cyc_mod_peek(TEMPLE_SPAWN_POINT);
    cyc_mod_poke(TEMPLE_SPAWN_POINT, (uint8_t)dest);
    CycModRegs regs;
    cyc_mod_regs(&regs);
    bool ok = cyc_mod_call_commit(addr, &regs);
    cyc_mod_poke(TEMPLE_SPAWN_POINT, furthest);
    dest = -1; journey = false;
    return ok;
}

/* Game_Start: a new game has met no Guru but Eolis's. */
static int new_game_hook(uint16_t addr) {
    (void)addr;
    if (visited != 1) { visited = 1; save_visited(); }
    return 0;
}

static const char *const hook_ids[] = {
    "faxanadu.travel.next-action", "faxanadu.travel.main-loop", "faxanadu.travel.spawn", "faxanadu.travel.new-game" };
static void hooks(bool on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    if (served) unserve();
    enabled = served = failed = journey = false;
    dest = -1;
    memset(&talk, 0, sizeof talk);
    hooks(false);
}
static void activate(void) {
    enabled = true;
    load_visited();
    hooks(true);
    nes_mod_set_local_only("Guru travel", 1);
}

/* Supply our script steps once the cart is loaded: at power-on, before any
 * save state is loaded, or on the first frame after activation in play. */
void fax_travel_step(void) {
    if (!enabled || served || failed) return;
    if (!serve_script(true)) {
        fprintf(stderr, "[travel] unexpected ROM layout; Guru travel is off\n");
        unserve(); failed = true; hooks(false);
        return;
    }
    served = true;
}

static int state_get(uint8_t *buf, int cap) {
    if (cap < 6) return -1;
    buf[0] = (uint8_t)(dest < 0 ? 0xFF : dest);
    buf[1] = journey;
    buf[2] = talk.phase; buf[3] = talk.offer; buf[4] = talk.asking;
    buf[5] = (uint8_t)(text < 0 ? 0xFF : text);
    return 6;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == 6 && (buf[0] < TEMPLES || buf[0] == 0xFF) && buf[1] <= 1 && buf[2] <= INTRO_TOWNS &&
                        buf[4] < TEMPLES && (buf[5] < TEXT_TO + TEMPLES || buf[5] == 0xFF));
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    dest = len && buf[0] != 0xFF ? buf[0] : -1;
    journey = len && buf[1];
    talk = len ? (Talk){ buf[2], buf[3], buf[4] } : (Talk){ 0 };
    if (served && len && buf[5] != 0xFF) serve(buf[5]);   /* overrides are not machine state */
    return 1;
}

/* For the map: the Gurus visited, a bit per temple (Eolis's always). 0 with Guru travel off. */
uint8_t fax_travel_visited(void) { return enabled ? visited : 0; }

/* For Save files, which offers saving first while a file is in play. */
bool fax_travel_can_offer(void) { return enabled && served && !failed && talk.offer; }
uint16_t fax_travel_say_step(void) { return enabled && served && !failed ? SAY : 0; }
uint16_t fax_travel_begin(bool towns) {
    if (!fax_travel_can_offer()) return 0;
    if (!serve(TEXT_INTRO)) {
        fprintf(stderr, "[travel] cannot supply the Guru's line; Guru travel is off\n");
        failed = true; talk.phase = IDLE;
        return 0;
    }
    talk.phase = towns ? INTRO_TOWNS : INTRO;
    return SAY;
}

#ifdef CYC_WITH_SDL
static void travel_state(int id, const char *line) {
    (void)line;
    char fields[256];
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"served\":%s,\"visited\":%u,\"furthest\":%u,\"phase\":%u,\"offer\":%u,"
             "\"asking\":%u,\"text\":%d,\"dest\":%d,\"journey\":%s",
             enabled ? "true" : "false", served ? "true" : "false", visited, cyc_mod_peek(TEMPLE_SPAWN_POINT),
             talk.phase, talk.offer, talk.asking, text, dest, journey ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_travel_tcp_setup(void) {
    cyc_tcp_register("travel_state", "Guru travel: visited Gurus (bit per temple), conversation, journey", travel_state);
}
#else
void fax_travel_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_travel) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.travel.next-action", 0x826e, next_action_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.main-loop", 0xe016, main_loop_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.spawn", 0xdd61, spawn_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.travel.new-game", 0xdb26, new_game_hook) ||
        !nes_mod_register_reset_callback(deactivate) || !nes_mod_register_activation_plugin("faxanadu.guru-travel", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.travel", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.travel", state_validate) || !fax_save_listen(on_save)) abort();
}
