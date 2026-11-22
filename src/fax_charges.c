/* Charges: the recharging items on the status bar. USA 57dd23d1, chipx86
 * byte-exact disassembly.
 *
 * Recharging items (fax_recharge.c) keeps the Wing Boots, the Hour Glass and
 * the Elixir after use, so whether each is ready is something the player
 * needs at a glance, as Super Metroid shows whether a reserve tank will catch
 * Samus. Each has a fixed slot in a row past the item box: the Wing Boots,
 * the Hour Glass, the Elixir. An item is drawn there with the item box's own
 * picture (four tiles of bank 10, as UI_DrawSelectedItem copies them) in the
 * status bar's palette when it is ready, in greys a step darker when it is
 * used (the Wing Boots flying or recharging, the Hour Glass freezing or
 * recharging, the Elixir spent until a Guru restores it), and not at all when
 * the hero does not have it (fax_recharge_charge). An empty slot keeps its
 * place, so nothing moves when he finds his first. The moment an item is
 * used it greys and blinks: through its "I've used ..." line, and 1.5 s
 * after, so an Elixir that just saved him is noticed.
 *
 * Seamless's view is 424 wide, and lays the status bar and the row out
 * centred together (fax_world_hud_x): the row is drawn past the status
 * bar's right edge, wherever that is. The stock 256-wide picture has no room
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
/* RAM */
#define INTERRUPT_COUNTER       0x001Au   /* +1 each NMI */
#define UI_ATTRIBUTE_DATA_INDEX 0x038Du   /* the status bar's and the text box's palette, 0-3 */

enum { ITEM_BANK = 10, BANK_SIZE = 16384, NATIVE_W = 256, HUD_LINE = 24, TILES = 4 };
enum { UNKNOWN = 0xFF };
/* ITEMINVENTORY_WING_BOOTS, _HOUR_GLASS, _ELIXIR, in fax_recharge.h's order */
static const uint8_t ITEM_ID[FAX_RECHARGE_ITEMS] = { 0x0F, 0x0D, 0x12 };
static const char *const ITEM_NAME[FAX_RECHARGE_ITEMS] = { "boots", "glass", "elixir" };

typedef struct {
    uint8_t last[FAX_RECHARGE_ITEMS];    /* each item's charge at the end of the last frame, or UNKNOWN */
    uint8_t blink[FAX_RECHARGE_ITEMS];   /* frames of blinking left */
} Track;

static bool enabled, failed, loaded;
static Track t;
static uint8_t picture_px[FAX_RECHARGE_ITEMS][16][16];   /* each item's picture, pixel values 0-3 */
static uint32_t *canvas;
static size_t capacity;
/* What the last present drew, for TCP: each slot's item shown, the row's left. */
static bool shown[FAX_RECHARGE_ITEMS];
static int row_x = -1;

static uint8_t q(uint16_t addr) { return cyc_mod_peek(addr); }

int fax_charges_row(void) { return enabled ? FAX_CHARGES_ROW : 0; }

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

/* One frame: an item that went from ready to used starts blinking; the blink
 * counts down while no box is up, and ends once the item is not used. */
static void step(Track *k, const FaxCharge now[FAX_RECHARGE_ITEMS], bool box) {
    for (int i = 0; i < FAX_RECHARGE_ITEMS; ++i) {
        if (k->last[i] == FAX_CHARGE_READY && now[i] == FAX_CHARGE_USED) k->blink[i] = FAX_CHARGES_BLINK;
        else if (now[i] != FAX_CHARGE_USED) k->blink[i] = 0;
        else if (k->blink[i] && !box) --k->blink[i];
        k->last[i] = (uint8_t)now[i];
    }
}
static bool lit(const Track *k, int i, uint8_t counter) {
    return !k->blink[i] || !(counter & FAX_CHARGES_BLINK_OFF);
}

/* ---- the machine ---- */

static void forget(void) {
    memset(t.last, UNKNOWN, sizeof t.last);
    memset(t.blink, 0, sizeof t.blink);
}

void fax_charges_frame_end(void) {
    if (!enabled) return;
    FaxCharge now[FAX_RECHARGE_ITEMS];
    for (int i = 0; i < FAX_RECHARGE_ITEMS; ++i) now[i] = fax_recharge_charge(i);
    step(&t, now, fax_text_box_showing());
}

/* The three pictures from bank 10, as UI_DrawSelectedItem copies an item's
 * into the item box: tiles top left, top right, bottom left, bottom right. */
static bool load_pictures(void) {
    size_t need = cyc_mod_copy_cart_payload(NULL, 0);
    if (need < (size_t)(ITEM_BANK + 1) * BANK_SIZE) return false;
    uint8_t *payload = malloc(need);
    if (!payload) return false;
    bool ok = cyc_mod_copy_cart_payload(payload, need) == need;
    const uint8_t *bank = payload + ITEM_BANK * BANK_SIZE;
    for (int i = 0; ok && i < FAX_RECHARGE_ITEMS; ++i) {
        bool any = false;
        for (int k = 0; k < TILES; ++k) {
            const uint8_t *tile = bank + (ITEM_TILES - 0x8000u) + bank[ITEM_TILEMAP_INDEXES - 0x8000u + ITEM_ID[i] * TILES + k] * 16u;
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    uint8_t v = (uint8_t)((tile[r] >> (7 - c) & 1u) | (tile[r + 8] >> (7 - c) & 1u) << 1);
                    picture_px[i][(k / 2) * 8 + r][(k % 2) * 8 + c] = v;
                    any |= v != 0;
                }
        }
        ok = any;
    }
    free(payload);
    return ok;
}

const uint32_t *fax_charges_present(const uint32_t *picture, int *w, int *h) {
    memset(shown, 0, sizeof shown);
    row_x = -1;
    if (!enabled || failed) return picture;
    int width = 0, height = 0;
    const uint32_t *from = picture;
    if (from) width = *w, height = *h;
    else from = cyc_render_present(&width, &height);
    if (!from || height < FAX_CHARGES_TOP + FAX_CHARGES_ITEM) return picture;
    int x0 = fax_world_hud_x(width) + NATIVE_W;   /* the status bar's right edge */
    if (x0 < NATIVE_W || x0 + FAX_CHARGES_ROW > width || !fax_text_hud_showing()) return picture;
    if (!loaded && !(loaded = load_pictures())) {
        FAX_CHARGES_LOG("[charges] cannot read the items' pictures; the status bar's row is off\n");
        failed = true;
        return picture;
    }
    /* The status bar's palette, as its row was drawn: emphasis and greyscale too. */
    unsigned palette = q(UI_ATTRIBUTE_DATA_INDEX) & 3u, mask = cyc_render_line_mask(HUD_LINE);
    unsigned keep = mask & 1u ? 0x30u : 0x3Fu, emphasis = (mask >> 5) << 6;
    uint32_t ready[4], used[4];
    for (int v = 1; v < 4; ++v) {
        uint8_t c = (uint8_t)(cyc_render_palette((int)(palette * 4 + (unsigned)v)) & 0x3F);
        ready[v] = hw_palette_argb[((c & keep) | emphasis) & 511] | 0xFF000000u;
        used[v] = hw_palette_argb[((fax_charges_grey(c) & keep) | emphasis) & 511] | 0xFF000000u;
    }
    uint8_t counter = q(INTERRUPT_COUNTER);
    bool copied = false;
    row_x = x0;
    for (int i = 0; i < FAX_RECHARGE_ITEMS; ++i) {
        FaxCharge c = fax_recharge_charge(i);
        if (c == FAX_CHARGE_NONE || !lit(&t, i, counter)) continue;
        if (!copied) {
            size_t n = (size_t)width * (size_t)height;
            if (n > capacity) {
                uint32_t *grown = realloc(canvas, n * sizeof *grown);
                if (!grown) return picture;
                canvas = grown; capacity = n;
            }
            memcpy(canvas, from, n * sizeof *canvas);
            copied = true;
        }
        const uint32_t *ink = c == FAX_CHARGE_READY ? ready : used;
        int x = x0 + i * FAX_CHARGES_SLOT;
        for (int r = 0; r < FAX_CHARGES_ITEM; ++r)
            for (int col = 0; col < FAX_CHARGES_ITEM; ++col) {
                uint8_t v = picture_px[i][r][col];
                if (v) canvas[(size_t)(FAX_CHARGES_TOP + r) * (size_t)width + (size_t)(x + col)] = ink[v];
            }
        shown[i] = true;
    }
    if (!copied) return picture;
    *w = width; *h = height;
    return canvas;
}

/* ---- the save-state record: the blink ---- */

enum { VERSION = 1, RECORD = 1 + 2 * FAX_RECHARGE_ITEMS };
static int state_get(uint8_t *buf, int cap) {
    if (cap < RECORD) return -1;
    buf[0] = VERSION;
    memcpy(buf + 1, t.last, FAX_RECHARGE_ITEMS);
    memcpy(buf + 1 + FAX_RECHARGE_ITEMS, t.blink, FAX_RECHARGE_ITEMS);
    return RECORD;
}
static int state_validate(const uint8_t *buf, int len) {
    if (len == 0) return 1;
    if (len != RECORD || buf[0] != VERSION) return 0;
    for (int i = 0; i < FAX_RECHARGE_ITEMS; ++i) {
        uint8_t last = buf[1 + i], blink = buf[1 + FAX_RECHARGE_ITEMS + i];
        if ((last > FAX_CHARGE_USED && last != UNKNOWN) || blink > FAX_CHARGES_BLINK) return 0;
    }
    return 1;
}
/* Length 0, a state saved without the row: nothing blinks, and the next frame
 * only learns where each item stands. */
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    forget();
    if (len) {
        memcpy(t.last, buf + 1, FAX_RECHARGE_ITEMS);
        memcpy(t.blink, buf + 1 + FAX_RECHARGE_ITEMS, FAX_RECHARGE_ITEMS);
    }
    return 1;
}

#ifdef CYC_WITH_SDL
static void charges_state(int id, const char *line) {
    (void)line;
    char fields[512];
    int n = snprintf(fields, sizeof fields, "\"enabled\":%s,\"row\":%d,\"hud\":%s,\"x\":%d,\"y\":%d,\"palette\":%u",
                     enabled ? "true" : "false", fax_charges_row(), fax_text_hud_showing() ? "true" : "false", row_x,
                     FAX_CHARGES_TOP, q(UI_ATTRIBUTE_DATA_INDEX) & 3u);
    /* Each item: its charge (0 not his, 1 ready, 2 used), frames of blinking left, drawn in the last picture. */
    for (int i = 0; i < FAX_RECHARGE_ITEMS && n > 0 && (size_t)n < sizeof fields; ++i)
        n += snprintf(fields + n, sizeof fields - (size_t)n, ",\"%s\":{\"charge\":%d,\"blink\":%u,\"shown\":%s}",
                      ITEM_NAME[i], (int)fax_recharge_charge(i), t.blink[i], shown[i] ? "true" : "false");
    cyc_tcp_ok(id, fields);
}
void fax_charges_tcp_setup(void) {
    cyc_tcp_register("charges_state",
                     "Charges: each recharging item on the status bar (charge, blink, drawn), and where the row is",
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
