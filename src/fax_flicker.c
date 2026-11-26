/* No sprite flicker (#161). USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * The game spreads its sprites over the shadow OAM ($0700) so that the NES's
 * 8 sprites a line flicker rather than vanish: Sprites_IncNextSpriteSlot
 * hands out slots 1, 33, 2, 34, ... 31, 63, alternating halves, and
 * Sprite_Draw writes a slot at (slot x 4) EOR Sprites_StartSlotRange ($1C),
 * which Sprites_Reset flips between $00 and $80 at the start of every frame.
 * So the n-th sprite drawn sits in one half on one frame and in the other on
 * the next. The NES puts the lower OAM index in front, so wherever two
 * sprites overlap, which is in front swaps every frame. Beside a villager the
 * hero blinked behind him at 30 Hz, and read as see-through; so did the
 * hero's own shield and body, and any two actors that overlap.
 *
 * Seamless composes the remaster's picture itself, from each room's shadow
 * OAM (fax_world.c's actors()), and draws all 63 sprites with no limit to a
 * line. This gives it the order to paint them in: the order the game drew
 * them, the first in front, read back from each sprite's index and the
 * room's $1C. Every frame draws in the same order: Game_MainLoop draws the
 * shield, the body and the weapon (Player_DrawShield, Player_DrawBody,
 * Player_DrawWeapon), then cast magic, then Sprites_UpdateAll draws actor
 * slots 7 down to 0; Game_DrawScreenInFrozenState, the scene a conversation
 * keeps, draws the hero first too. So the hero is always in front of the
 * actors, his shield in front of his body and his body in front of his
 * weapon, and two actors keep their order from frame to frame.
 *
 * Sprites_FlipRanges swaps the halves without $1C, between frames, while the
 * game is paused and while a Red Potion or the Elixir fills the bars. Seamless
 * captures none of those frames (it does not while paused, nor in the item's
 * IScript's script mode, which lasts through the fill), so every OAM it paints
 * has its halves where $1C says.
 *
 * And it takes a frame's sprites when the NMI would. OnInterrupt copies the
 * shadow OAM to the PPU only when the CPU waits in WaitForNextFrame
 * (Game_InterruptsHandledLatch clear), or Game_NeedOAMReset asks: on a frame
 * the game lagged (five actors in a room are enough), the PPU keeps the last
 * frame's sprites. Seamless captured the active room at every frame's end,
 * so on a lagging frame it painted a shadow OAM half drawn, and the actors
 * drawn last vanished for a frame. fax_flicker_frame_drawn() tells it to keep
 * its last capture then, as the PPU does.
 *
 * Only the order of painting, and which complete frame is painted, change.
 * Which sprites are in OAM, and so the game's own blinking (the Fire spell
 * drawn 2 frames in 4), is the game's. Each sprite's behind-the-background
 * bit is handled where it is painted, as before. The machine is untouched:
 * OAM, the PPU (its 8 a line, the overflow flag, sprite 0 hit), RAM, save
 * states and rewind are the game's, and the stock view (the remaster off) is
 * the machine's own picture, with the original's flicker. No hooks, no ROM,
 * no save-state record; TCP no_flicker_state counts the frames kept. */
#include "fax_flicker.h"
#include "cyc_mod.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>

/* RAM */
#define GAME_INTERRUPTS_HANDLED_LATCH 0x0010u   /* WaitForNextFrame clears it and waits; OnInterrupt sets it */
#define GAME_NEED_OAM_RESET           0x001Bu   /* nonzero: the NMI copies the shadow OAM even on a lagging frame */
#define START_RANGE_FLIP 0x80u   /* Sprites_StartSlotRange: $00, or $80 (EOR the slot's byte offset: 32 slots) */

static bool enabled;
static unsigned long kept;   /* frame ends Seamless kept its last capture through, for the TCP command */

unsigned fax_flicker_paint_order(uint8_t start_slot_range, uint8_t order[FAX_FLICKER_SPRITES]) {
    unsigned n = 0;
    if (!enabled) {
        for (unsigned index = 63; index >= 1; --index) order[n++] = (uint8_t)index;
        return n;
    }
    unsigned flip = start_slot_range & START_RANGE_FLIP ? 32u : 0u;
    /* Sprites_IncNextSpriteSlot hands out slots 0, 32, 1, 33, 2, 34, ... 31,
     * 63 (from 1 while sprite 0 is the status bar's): the slot of the
     * place-th draw is place / 2, in the upper half when place is odd. Back to
     * front is the last drawn first, every slot once. */
    for (unsigned place = 64; place-- > 0;) {
        unsigned slot = place >> 1 | (place & 1u) << 5;
        unsigned index = slot ^ flip;             /* where Sprite_Draw's EOR put it this frame */
        if (index == 0) continue;                 /* the status bar's sprite 0, written there directly */
        order[n++] = (uint8_t)index;
    }
    return n;
}

bool fax_flicker_frame_drawn(void) {
    if (!enabled || !cyc_mod_peek(GAME_INTERRUPTS_HANDLED_LATCH) || cyc_mod_peek(GAME_NEED_OAM_RESET)) return true;
    ++kept;
    return false;
}

static void deactivate(void) { enabled = false; }
static void activate(void) { enabled = true; }

#ifdef CYC_WITH_SDL
static void flicker_state(int id, const char *line) {
    (void)line;
    char fields[64];
    snprintf(fields, sizeof fields, "\"enabled\":%s,\"kept\":%lu", enabled ? "true" : "false", kept);
    cyc_tcp_ok(id, fields);
}
void fax_flicker_tcp_setup(void) {
    cyc_tcp_register("no_flicker_state",
                     "No sprite flicker: whether on, and the lagging frames Seamless kept its last capture through (kept)",
                     flicker_state);
}
#else
void fax_flicker_tcp_setup(void) {}
#endif

NES_MOD_CONSTRUCTOR(register_flicker) {
    if (!nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.no-flicker", activate)) abort();
}
