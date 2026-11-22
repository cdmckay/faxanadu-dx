/* Charges: the Elixir on the status bar. USA 57dd23d1, chipx86 byte-exact
 * disassembly.
 *
 * Recharging items (fax_recharge.c) keeps the Elixir after it revives the
 * hero, spent until a Guru restores it, so whether it will catch him is
 * something the player needs at a glance, as Super Metroid shows whether a
 * reserve tank will catch Samus. It stands left of the status bar's M: and
 * P: bars, level with them, since it refills both when it revives him
 * (Player_FillHPAndMP). It is drawn with the item box's own picture (four
 * tiles of bank 10, as UI_DrawSelectedItem copies them) in the status bar's
 * palette when it is charged, in greys a step darker when it is spent, and
 * not at all when the hero does not have it (fax_recharge_charge). Its place
 * stays, so nothing moves when he finds it. The moment it revives him it
 * greys and blinks: through its "I've used Elixir." line, and 1.5 s after,
 * so the save is noticed. The Wing Boots and the Hour Glass are not drawn:
 * they run on MP with no cooldown (#124), and T: counts the selected one's
 * run down.
 *
 * Seamless's view is 424 wide, and lays the status bar and the Elixir out
 * centred together (fax_world_hud_x): the Elixir is drawn against the status
 * bar's left edge, wherever that is. The stock 256-wide picture has no room
 * for it, so there it draws nothing. It shows only beside the status bar, by
 * what the last frame showed (fax_text_hud_showing): not on the title or the
 * ending, nor while the game has the screen off.
 *
 * No hooks, no ROM, nothing written to the machine: it reads RAM and the
 * frame at the end of each frame and draws over the presented picture. Its
 * record keeps the blink. See docs/GAME_INTERNALS.md, "The status bar". */
#include "fax_charges.h"
#include "fax_recharge.h"
#include "fax_text.h"
#include "fax_world.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAX_CHARGES_LOG
#define FAX_CHARGES_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t hw_palette_argb[512];   /* the host's NES colors, emphasis in bits 6-8 */

/* Bank 10 */
#define ITEM_TILEMAP_INDEXES 0xB4E4u    /* four tile numbers an item, by ITEMINVENTORY_* (TextBox_LoadItemSourceTiles' table for Items) */
#define ITEM_TILES           0x8500u    /* the items' pictures, 16 bytes a tile (UI_Maybe_GetItemSpritePPUTileAddr) */
#define ITEMINVENTORY_ELIXIR 0x12u
/* RAM */
#define INTERRUPT_COUNTER       0x001Au   /* +1 each NMI */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du   /* the status bar's and the text box's palette, 0-3 */

enum { ITEM_BANK = 10, BANK_SIZE = 16384, HUD_LINE = 24, TILES = 4 };
enum { UNKNOWN = 0xFF };

typedef struct {
    uint8_t last;    /* the Elixir's charge at the end of the last frame, or UNKNOWN */
    uint8_t blink;   /* frames of blinking left */
} Track;

static bool enabled, failed, loaded;
static Track t;
static uint8_t picture_px[16][16];   /* the Elixir's picture, pixel values 0-3 */
static uint32_t *canvas;
static size_t capacity;
/* What the last present drew, for TCP: whether the Elixir showed, and where its place is. */
static bool shown;
static int elixir_x = -1;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }
static FaxCharge elixir(void) { return fax_recharge_charge(FAX_RECHARGE_ELIXIR); }

int fax_charges_width(void) { return enabled ? FAX_CHARGES_ITEM : 0; }

/* ---- the rules ---- */

/* The NES's greys, darkest first; a color's place among them is its
 * brightness (its row of the palette), and the grey it takes is one place
 * darker, but never black. */
uint8_t fax_charges_grey(uint8_t c) {
    static const uint8_t GREYS[] = { 0x0F, 0x2D, 0x00, 0x10, 0x20 };
    unsigned hue = c & 0x0Fu, row = (c >> 4) & 3u, level;
    if (hue >= 0x0E || c == 0x0D || c == 0x1D) return 0x0F;      /* the blacks stay black */
    if (hue == 0x0D) level = row == 2 ? 1 : 3;                    /* $2D dark grey, $3D light grey */
    else if (hue == 0x00) level = row < 2 ? row + 2 : 4;          /* $00 grey, $10 light grey, $20 and $30 white */
    else level = row + 1;                                         /* a color: dark, middle, light, pale */
    return GREYS[level > 1 ? level - 1 : 1];
}

/* One frame: an Elixir that went from charged to spent starts blinking; the
 * blink counts down while no box is up, and ends once it is not spent. */
static void step(Track *k, FaxCharge now, bool box) {
    if (k->last == FAX_CHARGE_READY && now == FAX_CHARGE_USED) k->blink = FAX_CHARGES_BLINK;
    else if (now != FAX_CHARGE_USED) k->blink = 0;
    else if (k->blink && !box) --k->blink;
    k->last = (uint8_t)now;
}
static bool lit(const Track *k, uint8_t counter) {
    return !k->blink || !(counter & FAX_CHARGES_BLINK_OFF);
}

/* ---- the machine ---- */

static void forget(void) {
    t.last = UNKNOWN;
    t.blink = 0;
}

void fax_charges_frame_end(void) {
    if (!enabled) return;
    step(&t, elixir(), fax_text_box_showing());
}

/* The Elixir's picture from bank 10, as UI_DrawSelectedItem copies an item's
 * into the item box: tiles top left, top right, bottom left, bottom right. */
static bool load_picture(void) {
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(ITEM_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need, any = false;
    const uint8_t *bank = payload + ITEM_BANK * BANK_SIZE;
    for (int k = 0; ok && k < TILES; ++k) {
        const uint8_t *tile = bank + (ITEM_TILES - 0x8000u) + bank[ITEM_TILEMAP_INDEXES - 0x8000u + ITEMINVENTORY_ELIXIR * TILES + k] * 16u;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                uint8_t v = (uint8_t)((tile[r] >> (7 - c) & 1u) | (tile[r + 8] >> (7 - c) & 1u) << 1);
                picture_px[(k / 2) * 8 + r][(k % 2) * 8 + c] = v;
                any |= v != 0;
            }
    }
    free(payload);
    return ok && any;
}

const uint32_t *fax_charges_present(const uint32_t *picture, int *w, int *h) {
    shown = false;
    elixir_x = -1;
    if (!enabled || failed) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || height < FAX_CHARGES_TOP + FAX_CHARGES_ITEM) return picture;
    int x = fax_world_hud_x(width) - FAX_CHARGES_ITEM;   /* against the status bar's left edge */
    if (x < 0 || x + FAX_CHARGES_ITEM > width || !fax_text_hud_showing()) return picture;
    if (!loaded && !(loaded = load_picture())) {
        FAX_CHARGES_LOG("[charges] cannot read the Elixir's picture; the status bar's Elixir is off\n");
        failed = true;
        return picture;
    }
    elixir_x = x;
    FaxCharge c = elixir();
    if (c == FAX_CHARGE_NONE || !lit(&t, q(INTERRUPT_COUNTER))) return picture;
    /* The status bar's palette, as its row was drawn: emphasis and greyscale too. */
    unsigned palette = q(UI_ATTRIBUTE_DATA_INDEX) & 3u, mask = cyc_render_line_mask(HUD_LINE);
    unsigned keep = mask & 1u ? 0x30u : 0x3Fu, emphasis = (mask >> 5) << 6;
    uint32_t ink[4];
    for (int v = 1; v < 4; ++v) {
        uint8_t nes = (uint8_t)(cyc_render_palette((int)(palette * 4 + (unsigned)v)) & 0x3F);
        if (c == FAX_CHARGE_USED) nes = fax_charges_grey(nes);
        ink[v] = hw_palette_argb[((nes & keep) | emphasis) & 511] | 0xFF000000u;
    }
    size_t n = (size_t)width * (size_t)height;
    if (n > capacity) {
        uint32_t *grown = realloc(canvas, n * sizeof *grown);
        if (!grown) return picture;
        canvas = grown; capacity = n;
    }
    memcpy(canvas, from, n * sizeof *canvas);
    for (int r = 0; r < FAX_CHARGES_ITEM; ++r)
        for (int col = 0; col < FAX_CHARGES_ITEM; ++col) {
            uint8_t v = picture_px[r][col];
            if (v) canvas[(size_t)(FAX_CHARGES_TOP + r) * (size_t)width + (size_t)(x + col)] = ink[v];
        }
    shown = true;
    *w = width; *h = height;
    return canvas;
}

/* ---- the save-state record: the blink ---- */

/* Version 2: a version, the Elixir's state at the last frame's end and its
 * blink left. Version 1 (#115's row) had each of the three items' states,
 * then their blinks, the Wing Boots, the Hour Glass and the Elixir: it loads
 * with the Elixir's. */
enum { VERSION = 2, RECORD = 3, VERSION_ROW = 1, RECORD_ROW = 7, ROW_ITEMS = 3 };
static bool valid(uint8_t last, uint8_t blink) {
    return (last <= FAX_CHARGE_USED || last == UNKNOWN) && blink <= FAX_CHARGES_BLINK;
}
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    buf[1] = t.last;
    buf[2] = t.blink;
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len == RECORD && buf[0] == VERSION) return valid(buf[1], buf[2]);
    if (len != RECORD_ROW || buf[0] != VERSION_ROW) return 0;
    for (int i = 0; i < ROW_ITEMS; ++i)
        if (!valid(buf[1 + i], buf[1 + ROW_ITEMS + i])) return 0;
    return 1;
}
/* Length 0, a state saved without the Elixir's record: it does not blink,
 * and the next frame only learns where it stands. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    forget();
    if (len == RECORD) t.last = buf[1], t.blink = buf[2];
    else if (len == RECORD_ROW) t.last = buf[1 + FAX_RECHARGE_ELIXIR], t.blink = buf[1 + ROW_ITEMS + FAX_RECHARGE_ELIXIR];
    return 1;
}

#ifdef CYC_WITH_SDL
static void charges_state(int id, const char *line) {
    (void)line;
    char fields[256];
    /* The Elixir: its charge (0 not his, 1 charged, 2 spent), frames of blinking left, drawn in the last picture. */
    snprintf(fields, sizeof fields,
             "\"enabled\":%s,\"width\":%d,\"hud\":%s,\"x\":%d,\"y\":%d,\"palette\":%u,"
             "\"elixir\":{\"charge\":%d,\"blink\":%u,\"shown\":%s}",
             enabled ? "true" : "false", fax_charges_width(), fax_text_hud_showing() ? "true" : "false", elixir_x,
             FAX_CHARGES_TOP, q(UI_ATTRIBUTE_DATA_INDEX) & 3u, (int)elixir(), t.blink, shown ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_charges_tcp_setup(void) {
    cyc_tcp_register("charges_state",
                     "Charges: the Elixir on the status bar (charge, blink, drawn), and where it is",
                     charges_state);
}
#else
void fax_charges_tcp_setup(void) {}
#endif

static void deactivate(void) {
    enabled = failed = false;
    forget();
}
static void activate(void) {
    enabled = true;
    forget();
}

NES_MOD_CONSTRUCTOR(register_charges) {
    forget();
    if (!nes_mod_register_reset_callback(deactivate) || !nes_mod_register_activation_plugin("faxanadu.charges", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.charges", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.charges", state_validate))
        abort();
}
