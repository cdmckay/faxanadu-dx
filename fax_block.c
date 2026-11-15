/* Block. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Every frame the sprite loop (Sprites_UpdateAll, bank 14) checks each sprite
 * against the shield, then against the hero. Enemy magic (sprites of category
 * 6) that touches the shield's box runs Player_HandleShieldHitByMagic: the
 * shield takes the shot (it is removed, with sound $0C), but
 * Player_HandleHitByMagic still lets SPRITE_ENTITY_DAMAGES >> (shield + 1)
 * through. Magic that misses the shield and touches the hero runs
 * Player_HandleTouchEnemyMagic, the full damage. The shield check skips a
 * swing, and SelectedShield 3 and up: the Battle Helmet, which lives in the
 * shield slot but has no shield drawn (Player_DrawShield skips it too), so in
 * the original it never stops magic.
 *
 * Holding Block with anything in the shield slot (the Small, Large or Magic
 * Shield, or the Battle Helmet) raises it:
 *   - the hero stands still: the controller bytes lose the d-pad, A and B
 *     while the main loop runs (not in text boxes or menus);
 *   - magic on the shield lets nothing through (Player_HandleHitByMagic is
 *     skipped), and magic that reaches the hero from the side he faces is
 *     stopped the same way: the touch handler returns through the end of
 *     Player_HandleShieldHitByMagic, which removes it with the shield's sound.
 *     The Battle Helmet, with no shield box, stops magic only this way, which
 *     is the shield check's rule (from the front) without its box;
 *   - Sugata's flash (SpriteBehavior_FlashScreenHitPlayer) does no damage,
 *     from any side: its damage is skipped for the shield's sound, and the
 *     behavior finishes as the game's own does. fax_sugata.c, when on, decides
 *     range and the Magic Shield's reflection first, at the same site.
 * Contact damage and everything else is untouched.
 *
 * The first time the hero comes to own anything for the shield slot (a shop,
 * or the Battle Helmet's pickup, whose script runs before the helmet is
 * added), the script's end is steered to one more line naming the bound key,
 * in Guru travel's SAY step and message slot (fax_travel.h). That it has been
 * said is remembered (remembered()/remember(): a sidecar file until save
 * slots, #24, keep named fields), so it is said once. See
 * docs/GAME_INTERNALS.md. */
#include "fax_block.h"
#include "fax_travel.h"
#include "cyc_mod.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_input.h"
#include "cyc_tcp.h"
#include <SDL.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RAM */
#define PLAYER_POS_X          0x009Eu   /* Player_PosX_Block: the hero's left edge; he is 16 pixels wide */
#define PLAYER_FLAGS          0x00A4u   /* $40: facing right */
#define ISCRIPT_ADDR          0x00DBu   /* IScriptOrCHRAddr; the next action is at it + IScriptOffset */
#define ISCRIPT_OFFSET        0x00DDu
#define CURRENT_SPRITE_INDEX  0x0378u
#define SHIELD_INVENTORY      0x03A5u   /* the shields not in hand, NumberOfShields of them */
#define SELECTED_SHIELD       0x03BFu   /* 0 Small, 1 Large, 2 Magic Shield, 3 Battle Helmet, $FF none */
#define NUMBER_OF_SHIELDS     0x03C4u
#define SPRITE_BOX_LEFT       0x03E2u   /* Sprite_GetBounds: the current sprite's box, this frame */
#define SPRITE_BOX_WIDTH      0x03E4u
#define PLAYER_IS_DEAD        0x0438u
/* Bank 14 */
#define PLAYER_HANDLE_HIT_BY_MAGIC      0x87DCu  /* LDX CurrentSpriteIndex, LDY CurrentSprites_Entities,X,
                                                    LDA SPRITE_ENTITY_DAMAGES,Y, LDY SelectedShield */
#define SHIELD_STOPS_MAGIC              0x87CEu  /* Player_HandleShieldHitByMagic after JSR Player_HandleHitByMagic:
                                                    LDX CurrentSpriteIndex, LDA #$FF, STA CurrentSprites_Entities,X,
                                                    LDA #$0C, JSR Sound_PlayEffect, RTS */
#define PLAYER_HANDLE_TOUCH_ENEMY_MAGIC 0x8999u  /* LDA Player_InvincibilityPhase, BNE RETURN_89AD,
                                                    LDA DurationOintment, BPL RETURN_89AD */
#define FLASH_HIT                       0xAB44u  /* SpriteBehavior_FlashScreenHitPlayer, greyscale just cleared:
                                                    LDA #$04, JSR Sound_PlayEffect, iframes, knockback, 10 HP */
#define FLASH_FINISH                    0xAB60u  /* LDX CurrentSpriteIndex, JMP Sprite_FinishBehavior */
/* Bank 12 */
#define ISCRIPTS_INVOKE_NEXT_ACTION     0x826Eu  /* reads the action at IScriptOrCHRAddr + IScriptOffset */
#define ISCRIPT_ACTION_END_SCRIPT       0x82B4u  /* JSR IScripts_UpdatePortraitAnimation, LDA Temp_0201: close the box */
#define ISCRIPT_GOT_BATTLE_HELMET       0xA698u  /* IScript $8C, from Player_PickUpBattleHelmet before it adds the
                                                    helmet: GENERIC, SHOW_UNSKIPPABLE_MESSAGE $B6, END */
/* Fixed bank */
#define GAME_LOOP_COUNTDOWN_ITEMS       0xC88Fu  /* JSR, JSR, JSR, JMP: the timed items */
#define MAIN_LOOP_COUNTDOWN_PUSHED      0xDB6Au  /* Game_MainLoop's JSR GameLoop_CountdownItems ($DB68), after
                                                    Sprites_UpdateAll: a pass of the main loop got this far */
#define SOUND_PLAY_EFFECT               0xD0E4u  /* A: the sound. Keeps X and Y; memory only */
#define SOUND_SHIELD                    0x0Cu    /* what Player_HandleShieldHitByMagic plays */
/* Values */
#define FACING_RIGHT  0x40u
#define BATTLE_HELMET 3u
/* What the hero cannot do while he blocks: A, B, Up, Down, Left, Right. */
#define STAND_STILL   0xCFu

/* The defaults as SDL numbers them: SDL_SCANCODE_V, and SDL_CONTROLLER_BUTTON_B
 * in the host's binding encoding (cyc_input.h CYC_PAD_BUTTON: 1 + the button). */
enum { DEFAULT_KEY = 25, DEFAULT_PAD = 1 + 1, AXIS_PRESS = 16384 };
enum { KEYBOARD, CONTROLLER };

/* The tutorial, one interaction script at a time. */
enum { QUIET, SAYING };

static bool enabled, failed;
static bool key_held, pad_held, axis_held;
static uint64_t frames;                 /* machine frames ended */
static uint64_t pass;                   /* 1 + the frame in which a main-loop pass got to its timed items */
static bool owned;                      /* a shield-slot item, when the main loop last ran */
static uint8_t phase = QUIET;
static bool taught;                     /* remembered("block.taught") */
static char line[80];                   /* the tutorial's line, '\n' between rows */
static unsigned magic_blocked, shield_saved, flashes_blocked, tutorials, stood;
static char label[80] = "Block: V / B";

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }
static uint32_t prg(unsigned bank, uint16_t addr) { return bank * 16384u + (addr - 0x8000u); }

/* ---- the rules ---- */

/* What the shield slot holds (SelectedShield, or a ShieldInventory entry): the
 * Small, Large or Magic Shield, or the Battle Helmet; $FF is none. Any of
 * them raises Block. */
static bool shield_slot_item(uint8_t shield) { return shield <= BATTLE_HELMET; }

static bool owns_shield_slot_item(void) {
    if (shield_slot_item(peek(SELECTED_SHIELD))) return true;
    unsigned n = peek(NUMBER_OF_SHIELDS);
    for (unsigned i = 0; i < n && i < 4; ++i)
        if (shield_slot_item(peek((uint16_t)(SHIELD_INVENTORY + i)))) return true;
    return false;
}

static bool held(void) { return key_held || pad_held || axis_held; }

bool fax_block_raised(void) {
    return enabled && held() && shield_slot_item(peek(SELECTED_SHIELD)) && !cyc_mod_isolated();
}

/* Magic coming from the side the hero faces: its box's middle is on that side
 * of his (the same as or ahead of it). */
static bool from_the_front(uint8_t hero_x, bool facing_right, uint8_t box_left, uint8_t box_width) {
    int hero = hero_x + 8, magic = box_left + box_width / 2;
    return facing_right ? magic >= hero : magic <= hero;
}

/* In play: a pass of Game_MainLoop reached its timed items in the last frame,
 * so no text box, menu or screen change holds it. */
static bool in_play(void) { return pass && pass == frames && !peek(PLAYER_IS_DEAD); }

void fax_block_input(uint8_t buttons[2]) {
    if (!fax_block_raised() || !in_play()) return;
    if (buttons[0] & STAND_STILL) ++stood;
    buttons[0] &= (uint8_t)~STAND_STILL;
}

/* ---- the machine, from a hook ---- */

void fax_block_return_via(uint16_t to) {
    CycModRegs regs;
    cyc_mod_regs(&regs);
    uint16_t r = (uint16_t)(to - 1);                 /* an RTS adds one */
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)(r >> 8)); regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_poke((uint16_t)(0x100u | regs.s), (uint8_t)r);        regs.s = (uint8_t)(regs.s - 1);
    cyc_mod_set_regs(&regs);
}

bool fax_block_play_sound(uint8_t sound) {
    CycModRegs regs, call;
    cyc_mod_regs(&regs);
    call = regs;
    call.a = sound;
    bool ok = cyc_mod_call_commit(SOUND_PLAY_EFFECT, &call);
    cyc_mod_set_regs(&regs);
    return ok;
}

static uint16_t jsr_pushed(uint8_t s) {   /* what the JSR that entered this routine pushed */
    return (uint16_t)(peek((uint16_t)(0x100u | (uint8_t)(s + 1))) | peek((uint16_t)(0x100u | (uint8_t)(s + 2))) << 8);
}

/* ---- the hooks ---- */

/* GameLoop_CountdownItems, from Game_MainLoop: the hero is in play. */
static int main_loop_hook(uint16_t addr) {
    (void)addr;
    if (cyc_mod_isolated()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    if (jsr_pushed(regs.s) == MAIN_LOOP_COUNTDOWN_PUSHED) pass = frames + 1;
    return 0;
}

/* Player_HandleHitByMagic, from the shield: with Block up nothing gets
 * through. Its caller then removes the magic with the shield's sound. */
static int shield_hit_hook(uint16_t addr) {
    (void)addr;
    if (!fax_block_raised()) return 0;
    ++shield_saved;
    return 1;
}

/* Player_HandleTouchEnemyMagic: magic that reaches the hero from the front is
 * stopped as the shield stops it. With the Battle Helmet, all of it comes this
 * way: the shield check skips the helmet. */
static int touch_magic_hook(uint16_t addr) {
    (void)addr;
    if (!fax_block_raised() ||
        !from_the_front(peek(PLAYER_POS_X), peek(PLAYER_FLAGS) & FACING_RIGHT, peek(SPRITE_BOX_LEFT),
                        peek(SPRITE_BOX_WIDTH)))
        return 0;
    fax_block_return_via(SHIELD_STOPS_MAGIC);
    ++magic_blocked;
    return 1;
}

/* Sugata's flash, about to hurt: the shield takes it, from any side. */
static int flash_hook(uint16_t addr) {
    (void)addr;
    if (!fax_block_raised()) return 0;
    fax_block_play_sound(SOUND_SHIELD);
    fax_block_return_via(FLASH_FINISH);
    ++flashes_blocked;
    return 1;
}

/* ---- the tutorial ---- */

/* "Hold V to raise your shield against magic.", word-wrapped to the text box:
 * 16 columns, 4 rows. False if it does not fit. */
static bool wrap(const char *key, char out[80]) {
    const char *words[] = { "Hold", key, "to", "raise", "your", "shield", "against", "magic." };
    size_t n = 0, col = 0, rows = 1;
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        size_t len = strlen(words[i]);
        if (!len || len > 16) return false;
        if (col && col + 1 + len > 16) { out[n++] = '\n'; col = 0; ++rows; }
        else if (col) { out[n++] = ' '; ++col; }
        if (rows > 4 || n + len + 1 >= 80) return false;
        memcpy(out + n, words[i], len);
        n += len; col += len;
    }
    out[n] = 0;
    return true;
}

/* The dialogue font: $FD a space, $FE a new row, $FF the end. */
static bool encode(const char *s, uint8_t out[FAX_TRAVEL_SLOT_LEN]) {
    unsigned n = 0;
    for (; *s; ++s) {
        uint8_t v = *s == '\n' ? 0xFE : *s == ' ' ? 0xFD : (uint8_t)*s;
        if (v != 0xFE && v != 0xFD && (*s <= 0x20 || *s >= 0x7F)) return false;
        if (n + 1 >= FAX_TRAVEL_SLOT_LEN) return false;
        out[n++] = v;
    }
    while (n < FAX_TRAVEL_SLOT_LEN) out[n++] = 0xFF;
    return true;
}

static void compose(void);   /* the line, from the bindings */

/* Our line into message $C3's slot, and the SAY step that shows it. */
static bool serve(void) {
    static const uint8_t say[] = { 0x03, FAX_TRAVEL_SLOT_ID, 0x00 };   /* SHOW_MESSAGE $C3, END */
    uint8_t bytes[FAX_TRAVEL_SLOT_LEN];
    if (!line[0]) compose();
    if (!encode(line, bytes)) return false;
    for (unsigned i = 0; i < sizeof say; ++i)
        if (!cyc_mod_set_prg_read_override(prg(12, (uint16_t)(FAX_TRAVEL_SAY + i)), 0xFF, say[i])) return false;
    for (unsigned i = 0; i < FAX_TRAVEL_SLOT_LEN; ++i)
        if (!cyc_mod_set_prg_read_override(prg(13, (uint16_t)(FAX_TRAVEL_SLOT + i)), 0xFF, bytes[i])) return false;
    return true;
}

/* ---- what the game remembers: a seam for save slots ----
 *
 * Whether the line has been said is one named field of the player's progress.
 * Save slots (#24) will keep named fields in the save (fax_save_get/set);
 * until they land, a field that is set is a line naming it in
 * faxanadu_block.txt beside the game, as Guru travel keeps faxanadu_gurus.txt.
 * Only remembered() and remember() know where it lives: swap their bodies for
 * fax_save_get/set then. With saves off (--no-save) nothing is kept,
 * and the line is said once a session. */
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
#define SIDECAR "faxanadu_block.txt"
static bool remembered(const char *field) {
    char path[1100], text[64];
    bool found = false;
    if (!cyc_host_saves_enabled() || !sidecar_path(SIDECAR, path)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    while (!found && fgets(text, sizeof text, f)) {
        text[strcspn(text, "\r\n")] = 0;
        found = !strcmp(text, field);
    }
    fclose(f);
    return found;
}
static void remember(const char *field) {
    char path[1100], temp[1120], kept[1024] = "", text[64];
    if (remembered(field) || !cyc_host_saves_enabled() || !sidecar_path(SIDECAR, path)) return;
    FILE *f = fopen(path, "r");                       /* the fields already kept, kept */
    if (f) {
        size_t n = 0;
        while (fgets(text, sizeof text, f)) {
            text[strcspn(text, "\r\n")] = 0;
            size_t len = strlen(text);
            if (len && n + len + 2 < sizeof kept) { memcpy(kept + n, text, len); n += len; kept[n++] = '\n'; kept[n] = 0; }
        }
        fclose(f);
    }
    snprintf(temp, sizeof temp, "%s.tmp", path);
    f = fopen(temp, "w");
    if (!f) return;
    bool ok = fprintf(f, "%s%s\n", kept, field) > 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(temp); return; }
#ifdef _WIN32
    extern bool faxanadu_replace_file(const char *, const char *);
    if (!faxanadu_replace_file(temp, path)) remove(temp);
#else
    if (rename(temp, path) != 0) remove(temp);
#endif
}
#define TAUGHT "block.taught"

/* The script running is the Battle Helmet's pickup ("You got the Battle
 * Helmet"): the helmet is added only after it ends. */
static bool getting_battle_helmet(void) {
    return (peek(ISCRIPT_ADDR) | peek(ISCRIPT_ADDR + 1) << 8) == ISCRIPT_GOT_BATTLE_HELMET;
}

/* IScriptAction_EndScript, which every way out of a script takes (its END
 * action, B on a line, a shop's B): if this script gave the hero his first
 * shield-slot item, say the line first. It runs as the script's next action,
 * at the same depth: then END comes back here, and the box closes. */
static int end_script_hook(uint16_t addr) {
    (void)addr;
    if (!enabled || failed || cyc_mod_isolated()) return 0;
    if (phase == SAYING) { phase = QUIET; return 0; }   /* our line has been said (or dismissed) */
    if (taught || owned || !(owns_shield_slot_item() || getting_battle_helmet())) return 0;
    if (!serve()) {
        fprintf(stderr, "[block] cannot supply the Block tutorial's line; it is off\n");
        failed = true;
        return 0;
    }
    cyc_mod_poke(ISCRIPT_ADDR, (uint8_t)FAX_TRAVEL_SAY);
    cyc_mod_poke(ISCRIPT_ADDR + 1, (uint8_t)(FAX_TRAVEL_SAY >> 8));
    cyc_mod_poke(ISCRIPT_OFFSET, 0);
    fax_block_return_via(ISCRIPTS_INVOKE_NEXT_ACTION);
    phase = SAYING;
    taught = true;
    remember(TAUGHT);
    ++tutorials;
    return 1;
}

/* A pass of the main loop that got to its timed items has finished any
 * script (they run inside Sprites_UpdateAll, before them): note what the hero
 * owns before the next one. */
void fax_block_frame_end(void) {
    ++frames;
    if (pass != frames) return;
    phase = QUIET;
    owned = owns_shield_slot_item();
}

void fax_block_loaded(void) {
    if (enabled && phase == SAYING && !serve()) phase = QUIET;
}

static const char *const hook_ids[] = {
    "faxanadu.block.main-loop", "faxanadu.block.shield-hit", "faxanadu.block.touch-magic",
    "faxanadu.block.flash", "faxanadu.block.end-script" };
static void hooks(int on) {
    for (unsigned i = 0; i < sizeof hook_ids / sizeof hook_ids[0]; ++i) nes_mod_set_function_hook_enabled(hook_ids[i], on);
}
static void deactivate(void) {
    enabled = failed = false;
    key_held = pad_held = axis_held = false;
    frames = pass = 0;
    owned = true;
    phase = QUIET;
    magic_blocked = shield_saved = flashes_blocked = tutorials = stood = 0;
    hooks(0);
}
static void activate(void) {
    enabled = true;
    taught = remembered(TAUGHT);
    hooks(1);
    nes_mod_set_local_only("Block", 1);   /* host input the other side would not see */
}

/* Saved with the machine: what the hero owned before the script that is
 * running, and whether our line is showing. */
static int state_get(uint8_t *buf, int cap) {
    if (cap < 3) return -1;
    buf[0] = 1;   /* version */
    buf[1] = owned;
    buf[2] = phase;
    return 3;
}
static int state_validate(const uint8_t *buf, int len) {
    return len == 0 || (len == 3 && buf[0] == 1 && buf[1] <= 1 && buf[2] <= SAYING);
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (len) { owned = buf[1]; phase = buf[2]; }
    else { owned = true; phase = QUIET; }   /* a state from before Block: say nothing */
    pass = 0;
    return 1;
}

/* ---- the bindings (config.ini [Game]) ---- */

const char *fax_block_controls_label(void) { return label; }

#ifdef CYC_WITH_SDL
static int key = DEFAULT_KEY, pad = DEFAULT_PAD;   /* 0: unbound */
static int device = KEYBOARD;           /* the last one played with: the tutorial names its binding */

static void hint(int k, int p, char *out, size_t n) {
    CycBinding b = { k, p };
    cyc_binding_hint(&b, out, n);
}
static void describe(void) {   /* as the host shows its own shortcuts: "V / B" */
    char text[64] = "unbound";
    if (key || pad) hint(key, pad, text, sizeof text);
    snprintf(label, sizeof label, "Block: %s", text);
}
/* The line names the binding of what the player plays with. */
static void compose(void) {
    char name[48] = "";
    bool pad_first = device == CONTROLLER;
    if (pad_first ? pad : !key && pad) hint(0, pad, name, sizeof name);
    else if (key) hint(key, 0, name, sizeof name);
    if (!name[0] || !wrap(name, line)) wrap("Block", line);
}

bool fax_block_load_setting(const char *name, const char *value) {
    int v;
    if (!strcmp(name, "BlockKey")) {
        if (cyc_key_parse(value, &v)) key = v;
        else fprintf(stderr, "[block] BlockKey: SDL has no key named \"%s\"; keeping the default\n", value);
    } else if (!strcmp(name, "BlockButton")) {
        if (cyc_pad_parse(value, &v) && !CYC_PAD_IS_COMBO(v)) pad = v;
        else fprintf(stderr, "[block] BlockButton: \"%s\" is not one controller button or axis; keeping the default\n", value);
    } else {
        return false;
    }
    describe();
    line[0] = 0;
    return true;
}
void fax_block_save_settings(FILE *f) {
    char k[32], p[48];
    cyc_key_text(key, k, sizeof k);
    cyc_pad_text(pad, p, sizeof p);
    fprintf(f, "BlockKey = %s\nBlockButton = %s\n", k, p);
}

void fax_block_event(const void *ev) {
    const SDL_Event *e = ev;
    int was = device;
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        if (e->type == SDL_KEYDOWN) device = KEYBOARD;
        if (key && e->key.keysym.scancode == key) key_held = e->type == SDL_KEYDOWN;
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
        if (e->type == SDL_CONTROLLERBUTTONDOWN) device = CONTROLLER;
        if (CYC_PAD_IS_BUTTON(pad) && e->cbutton.button == pad - 1) pad_held = e->type == SDL_CONTROLLERBUTTONDOWN;
    } else if (e->type == SDL_CONTROLLERAXISMOTION) {
        if (CYC_PAD_IS_AXIS(pad) && e->caxis.axis == (pad - 100) / 2) {
            int value = (pad - 100) & 1 ? e->caxis.value : -e->caxis.value;
            axis_held = value >= AXIS_PRESS;
            if (axis_held) device = CONTROLLER;
        }
    }
    if (device != was && phase == QUIET) line[0] = 0;
}

static void block_state(int id, const char *line_in) {
    (void)line_in;
    char fields[640], text[96], said[160];
    if (!line[0]) compose();
    cyc_tcp_quote(label + 7, text, sizeof text);
    cyc_tcp_quote(line, said, sizeof said);
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"binding\":%s,\"held\":%s,\"raised\":%s,\"in_play\":%s,\"stood\":%u,"
             "\"magic_blocked\":%u,\"shield_saved\":%u,\"flashes_blocked\":%u,"
             "\"taught\":%s,\"tutorials\":%u,\"saying\":%s,\"owned\":%s,\"line\":%s",
             enabled ? "true" : "false", text, held() ? "true" : "false", fax_block_raised() ? "true" : "false",
             in_play() ? "true" : "false", stood, magic_blocked, shield_saved, flashes_blocked,
             taught ? "true" : "false", tutorials, phase == SAYING ? "true" : "false", owned ? "true" : "false", said);
    cyc_tcp_ok(id, fields);
}
void fax_block_tcp_setup(void) {
    cyc_tcp_register("block_state",
                     "Block: bindings, held and raised, magic and flashes stopped, the tutorial", block_state);
}
#else
static void compose(void) { wrap("V", line); }
bool fax_block_load_setting(const char *name, const char *value) { (void)name; (void)value; return false; }
void fax_block_save_settings(FILE *f) { (void)f; }
void fax_block_event(const void *ev) { (void)ev; }
void fax_block_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_block) {
    if (!nes_mod_register_function_entry_plugin(hook_ids[0], GAME_LOOP_COUNTDOWN_ITEMS, main_loop_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[1], PLAYER_HANDLE_HIT_BY_MAGIC, shield_hit_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[2], PLAYER_HANDLE_TOUCH_ENEMY_MAGIC, touch_magic_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[3], FLASH_HIT, flash_hook) ||
        !nes_mod_register_function_entry_plugin(hook_ids[4], ISCRIPT_ACTION_END_SCRIPT, end_script_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.block", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.block", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.block", state_validate)) abort();
}
