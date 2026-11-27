/* Continuous Faxanadu world. USA 57dd23d1, chipx86 byte-exact disassembly.
 * The ROM owns terrain decoding, collision, actors, combat and progression.
 * Adjacent rooms execute original actor routines on isolated machines. Only
 * room-local state travels at a seam; inventory, player, stack and audio do not.
 * Door/region/script transitions retain their original ownership.
 * Boss screens are arenas: while a boss lives, its room joins no neighbour. */
#include "fax_world.h"
#include "fax_camera.h"
#include "fax_seams.h"
#include "fax_entrances.h"
#include "fax_charges.h"
#include "fax_flicker.h"
#include "fax_menu.h"
#include "fax_text.h"
#include "cyc_mod.h"
#include "cyc_core.h"
#include "cyc_render.h"
#include "cyc_tcp.h"
#include "cyc_state.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#ifdef CYC_WITH_SDL
#include <SDL.h>
#else
#include <time.h>
static uint64_t SDL_GetPerformanceCounter(void) { return (uint64_t)clock(); }
static uint64_t SDL_GetPerformanceFrequency(void) { return CLOCKS_PER_SEC; }
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern uint32_t hw_palette_argb[512];

/* The view is fixed at 424x240, whatever the window: the original's full
 * 240 lines (its 32-line HUD and a room's 208), 53 of its 8-pixel tiles
 * wide, as Sonic Mania widens its own. The host scales it to the window by
 * its one fixed rule, sharp at 1080p's 4.5x (#107: fax_world_composed). */
enum { MAX_ROOMS = 83, RW = 256, RH = 208, HUD = 32, VIEW_W = 424, VIEW_H = 240 };
typedef struct {
    uint8_t ram[2048], chr[8192], palette[32];
    uint8_t valid, live, owned, edges[4];
    int16_t x, y;
    uint64_t ticks;
    uint8_t seams
        [FAX_WORLD_SEAMS_BYTES]; /* Seams' own (fax_seams.c): its actors' wake, crossings, tiles and cells past the edges */
} Room;
static Room rooms[MAX_ROOMS];
static const unsigned counts[8] = { 9, 64, 83, 14, 10, 40, 32, 20 };
static uint8_t payload[262144], local[2048];
static bool rom_ok, enabled, atlas, mask_ready, speculative;
static int area = -1, active = -1;
static int pre_x, pre_y, pending = -1, direction;
static int pending_y;
static unsigned ticks, last_tick, handoffs, failures, portals;
static bool player_tick;
static bool collision_active;
/* Inside tick_room's isolated pass of a neighbour's sprite loop, where its
 * actors meet the projected hero (shadow()): fax_world_ticking(), and the
 * room ticking (fax_world_seams_room()). */
static bool ticking;
static int tick_id = -1;
/* The game loaded a screen's sprite list in play (fax_world_screen_loaded):
 * the next refresh forgets every other room of the area. */
static bool screen_loaded;
/* The view's top-left in world pixels for the frame being run
 * (fax_world_view), and whether there is one. */
static int view_x, view_y;
static bool view_ok;
/* The hero's place in world pixels for that frame (fax_world_hero), from the
 * live game: a neighbour's tick sees him projected (shadow()). */
static int hero_wx, hero_wy;
static bool death_mode, script_mode;
/* The active room came in by a load the game shows before its main loop
 * runs there (arrive()): until the loop's first tick, the picture shows the
 * game's own picture of the room at its place. */
static bool arriving;
static int camera_x, camera_y, picture_w, picture_h;
static int origin_x, origin_y;
static bool origin_valid;
static int hud_x = -1, hud_w; /* where the status bar's column 0 went in the last picture, and its width */
static uint32_t *canvas;
static size_t capacity;
/* The last fax_world_present composed the canvas (fax_world_composed), and
 * whether it composed the world rather than the game's own picture centred
 * (leaving()). */
static bool composed, world_shown;
/* The status bar band (the native picture's top 32 lines) of the last frame
 * that showed it, live in play and through conversations (fax_world_step). */
static uint32_t hud_band[RW * HUD];
static bool hud_valid;
static Room *transfer;
/* Each screen's special event (CurrentScreen_SpecialEventID, $042E, as
 * Screen_LoadAllScreenInfo reads it from its sprite list in bank 11) for the
 * area in events_area: $FF none, 0 the path to Mascon, 1 a boss
 * (SCREEN_EVENT_BOSS), 2 the final boss. fighting: the active room was an
 * arena when the map around it was laid out. */
static uint8_t events[MAX_ROOMS];
static int events_area = -1;
static bool fighting;
static void hooks(bool on);
static uint8_t q(unsigned a);
static uint8_t collision_read(uint16_t pc, uint16_t addr, uint8_t value) {
    (void)pc;
    if (!enabled || cyc_mod_isolated() || active < 0 || addr < 0x600 || addr >= 0x6d0) return value;
    /* The original horizontal collision has a neighbor column, but floor,
     * ladder and jump sampling wrap X inside ScreenBuffer. While the body
     * straddles a seam, those wrapped columns belong to the right neighbor. */
    unsigned x = q(0x9e), col = addr & 15;
    if ((x > 240 && col == 0) || (pre_x > 240 && x < 16)) {
        unsigned next = rooms[active].edges[1];
        if (next < MAX_ROOMS && rooms[next].valid) return rooms[next].ram[addr];
    }
    unsigned row = (addr - 0x600) / 16;
    if (q(0xa1) >= 176 && row >= 13) {
        unsigned next = rooms[active].edges[3];
        if (next < MAX_ROOMS && rooms[next].valid) return rooms[next].ram[0x600 + (row - 13) * 16 + col];
    }
    return value;
}
static uint32_t rasters[MAX_ROOMS][RW * RH];
static uint8_t raster_blocks[MAX_ROOMS][256];
static uint8_t raster_palette[MAX_ROOMS][32], raster_chr[MAX_ROOMS][4096];
static bool raster_valid[MAX_ROOMS];
static double sim_ms, render_ms, max_seam_ms;
static FILE *trace;
static uint8_t q(unsigned a) { return cyc_mod_peek((uint16_t)a); }
static void p(unsigned a, unsigned v) { cyc_mod_poke((uint16_t)a, (uint8_t)v); }
static unsigned word(unsigned a) { return q(a) | (q(a + 1) << 8); }
static unsigned data(unsigned bank, unsigned addr) { return payload[bank * 16384 + (addr & 16383)]; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool call(unsigned pc, unsigned a, unsigned x) {
    CycModRegs r = { 0 };
    r.a = (uint8_t)a;
    r.x = (uint8_t)x;
    r.s = 0xfd;
    r.p = 0x24;
    return cyc_mod_call((uint16_t)pc, &r);
}
static bool bank(unsigned x) { return call(0xcc1a, 0, x); }
static void range(unsigned a, unsigned b) {
    for (; a <= b; a++) local[a] = 1;
}
static void make_mask(void) {
    if (mask_ready) return;
    range(0xba, 0xc9);
    range(0x2bc, 0x373);
    range(0x3e6, 0x425);
    range(0x43c, 0x4bb);
    range(0x600, 0x6ff);
    range(0x66, 0x69);
    range(0xcc, 0xcd);
    local[0x383] = local[0xda] = local[0x42e] = 1;
    range(0x379, 0x37e); /* multipart actor drawing handlers */
    mask_ready = true;
}
static bool supported(void) {
    if (rom_ok) return true;
    if (cyc_mod_copy_cart_payload(payload, sizeof payload) != sizeof payload) return false;
    uint32_t crc = ~0u;
    for (size_t i = 0; i < sizeof payload; i++) {
        crc ^= payload[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320u);
    }
    rom_ok = (~crc == 0x57dd23d1u);
    return rom_ok;
}
static bool scene_ready(void) { return q(0xe) == 0 && q(0x54) == 255 && (q(0x3c7) & 128) && !q(0x438) && q(0x13); }
static bool playing(void) { return !death_mode && !script_mode && scene_ready() && !q(0x120) && q(0x100) == 14; }
static int death_hook(uint16_t pc) {
    (void)pc;
    if (enabled && !cyc_mod_isolated()) death_mode = true;
    return 0;
}
/* Drain only scratch-machine PPU packets. Graphics loads are original native
 * routines; their normal NMI queue is collected without waiting for hardware.
 * These hooks are enabled only inside our isolated scope. */
static void drain(void) {
    if (!transfer) return;
    unsigned rp = q(0x1f), wp = q(0x20), guard = 0;
    while (rp != wp && guard++ < 256) {
        unsigned n = q(0x500 + rp);
        rp = (rp + 1) & 255;
        if (!n || n > 0x40) {
            failures++;
            break;
        }
        unsigned addr = q(0x500 + rp) << 8;
        rp = (rp + 1) & 255;
        addr |= q(0x500 + rp);
        rp = (rp + 1) & 255;
        for (unsigned i = 0; i < n; i++) {
            unsigned v = q(0x500 + rp);
            rp = (rp + 1) & 255;
            if (addr < 8192) transfer->chr[addr] = (uint8_t)v;
            else if (addr >= 0x3f00 && addr < 0x3f20) transfer->palette[addr & 31] = (uint8_t)v;
            addr++;
        }
    }
    p(0x1f, wp);
}
static int queue_hook(uint16_t pc) {
    (void)pc;
    if (!speculative) return 0;
    drain();
    return 0;
}
static int wait_hook(uint16_t pc) {
    (void)pc;
    if (!speculative) return 0;
    drain();
    return 1;
}
/* how: Seams' cells and OAM owners for the room come from the live game's
 * sprite loop (FAX_SEAMS_LIVE), from its tick (FAX_SEAMS_TICK), or the room
 * is fresh (FAX_SEAMS_FRESH: its actors just started). */
static void capture(unsigned id, bool graphics, int how) {
    Room *r = &rooms[id];
    memcpy(r->ram, cyc_cpu_ram(), 2048);
    if (graphics) {
        for (unsigned i = 0; i < 8192; i++) r->chr[i] = cyc_render_chr((uint16_t)i);
        memcpy(r->palette, cyc_ppu_palette(), 32);
    }
    r->live = r->valid = 1;
    fax_seams_captured(r->seams, how);
}
static void load(unsigned id) {
    Room *r = &rooms[id];
    for (unsigned i = 0; i < 2048; i++)
        if (local[i]) p(i, r->ram[i]);
    p(0x63, id);
    p(0x64, id);
}
static void (*room_fixup)(uint8_t, uint8_t, uint8_t *);
void fax_world_set_room_fixup(void (*fixup)(uint8_t area, uint8_t screen, uint8_t *ram)) { room_fixup = fixup; }
static void fixup_room(unsigned id) {
    if (room_fixup) room_fixup(rooms[id].ram[0x24], (uint8_t)id, rooms[id].ram);
}
static bool decode(unsigned id) {
    if (!cyc_mod_isolate_begin()) return false;
    bool ok = bank(q(0x8c)) && call(0xd1f6, id, 0);
    if (ok) {
        memcpy(rooms[id].ram, cyc_cpu_ram(), 2048);
        fixup_room(id);
        rooms[id].valid = 1;
    }
    cyc_mod_isolate_end();
    if (!ok) failures++;
    return ok;
}
/* Every screen's event for the current area, read once per area with the
 * game's own Screen_LoadAllScreenInfo ($C154, bank 11), which sets
 * CurrentScreen_SpecialEventID from the screen's sprite list. It leaves the
 * variable alone for an area with no lists, so it starts at $FF. */
static void load_events(void) {
    if (events_area == area) return;
    memset(events, 255, sizeof events);
    events_area = area;
    if (!cyc_mod_isolate_begin()) {
        failures++;
        return;
    }
    for (unsigned id = 0; id < counts[area]; id++) {
        p(0x63, id);
        p(0x42e, 255);
        if (bank(11) && call(0xc154, 0, 0)) events[id] = q(0x42e);
        else failures++;
    }
    cyc_mod_isolate_end();
}
/* A boss's screen (event 1, or the final boss's, 2) is an arena while its
 * boss lives: it joins no neighbour, so the hero comes in through the
 * original's own screen change and fights on one screen, as in the
 * original. ScreenEvents_HandleBoss ($EF9E) sets the event to $FF once
 * Sprites_HasBoss finds no boss in the room's slots: from then on the room
 * joins the world again. A room not entered since its area loaded has its
 * boss, as the original reloads it. */
static bool arena(unsigned id) {
    load_events();
    if (events[id] != 1 && events[id] != 2) return false;
    return !(rooms[id].live && rooms[id].ram[0x42e] == 255);
}
static void topology(unsigned current) {
    unsigned n = counts[area], queue[MAX_ROOMS], head = 0, tail = 0;
    int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
    unsigned reverse[4] = { 1, 0, 3, 2 };
    for (unsigned i = 0; i < n; i++) {
        rooms[i].owned = 0;
        memset(rooms[i].edges, 255, 4);
    }
    rooms[current].owned = 1;
    rooms[current].x = rooms[current].y = 0;
    queue[tail++] = current;
    fighting = arena(current);
    unsigned scroll = word(0x8a);
    while (head < tail) {
        unsigned id = queue[head++];
        Room *r = &rooms[id];
        if (arena(id)) continue;
        for (unsigned d = 0; d < 4; d++) {
            unsigned next = data(3, scroll + id * 4 + d);
            if (next >= n || data(3, scroll + next * 4 + reverse[d]) != id || arena(next)) continue;
            int x = r->x + dx[d], y = r->y + dy[d];
            bool occupied = false;
            for (unsigned j = 0; j < n; j++)
                if (rooms[j].owned && rooms[j].x == x && rooms[j].y == y && j != next) occupied = true;
            if (occupied) continue;
            Room *s = &rooms[next];
            if (!s->owned) {
                s->owned = 1;
                s->x = (int16_t)x;
                s->y = (int16_t)y;
                queue[tail++] = next;
            }
            if (s->x == x && s->y == y) r->edges[d] = (uint8_t)next;
        }
    }
}
static void refresh(bool completed) {
    int a = q(0x24), id = q(0x63);
    if (a < 0 || a > 7 || id >= (int)counts[a]) return;
    if (area != a) {
        memset(rooms, 0, sizeof rooms);
        memset(raster_valid, 0, sizeof raster_valid);
        area = a;
        active = -1;
        fighting = false;
    }
    /* A load within the area (a door, a zone respawn, the original's own
     * scroll into an arena) spawns its screen fresh from its list, as the
     * original does every screen it loads: Seamless forgets every other room
     * too, so each comes back decoded and spawned fresh as it comes near.
     * So nothing meets itself (a monster that wandered out, and its room's
     * new one), and a retry finds the rooms round it as fresh as its own. An
     * area change has emptied them all already. */
    if (screen_loaded) {
        screen_loaded = false;
        for (unsigned i = 0; i < counts[a]; i++) {
            Room *r = &rooms[i];
            r->live = r->valid = 0;
            r->ticks = 0;
            memset(r->seams, 0, sizeof r->seams);
            raster_valid[i] = false;
        }
    }
    bool changed = active != id;
    if (changed) {
        if (active >= 0) portals++;
        active = id;
    }
    /* Player entry runs after the ROM clears OAM. Keep the last completed
     * scene until this tick actually finishes drawing, including when an
     * actor interrupts the tick to start a conversation. */
    if (completed || changed || !rooms[id].live) capture(id, true, rooms[id].live ? FAX_SEAMS_LIVE : FAX_SEAMS_FRESH);
    /* Lay the map out from here when this room is not on it, and again when
     * its boss has died (join it) or when the original's own load has
     * brought a boss back into a room whose old copy had none (seal it).
     * The live room is captured first, so the arena test reads it. */
    if (!rooms[id].owned || arena(id) != fighting) topology(id);
    for (unsigned i = 0; i < counts[area]; i++)
        if (rooms[i].owned && !rooms[i].valid) decode(i);
}
static int script_hook(uint16_t pc) {
    (void)pc;
    if (!enabled || cyc_mod_isolated() || death_mode || script_mode || active < 0 || area != q(0x24) ||
        active != q(0x63))
        return 0;
    /* Dialogue can begin in the middle of the actor loop. The original
     * paused drawing routine completes the whole pose without advancing
     * AI or interactions. Run it before portrait loading clears OAM and
     * replaces sprite graphics/palettes, then retain that scene throughout
     * opening, text, and closing. Never change the live guest here. */
    if (cyc_mod_isolate_begin()) {
        if (bank(14) && call(0xcb47, 0, 0) && call(0xdc46, 0, 0)) capture((unsigned)active, true, FAX_SEAMS_LIVE);
        else failures++;
        cyc_mod_isolate_end();
    } else failures++;
    script_mode = true;
    collision_active = false;
    cyc_mod_set_ram_read_hook(NULL);
    return 0;
}
static int freeze_hook(uint16_t pc) {
    unsigned button = pc == 0xe02b ? 0x10 : 0x20;
    if (enabled && !cyc_mod_isolated() && playing() && (q(0x19) & button) && active >= 0 && area == q(0x24) &&
        active == q(0x63))
        /* The completed actor draw exists here. At the player-entry hook
         * the original game has already cleared its OAM staging buffer. */
        capture((unsigned)active, true, FAX_SEAMS_LIVE);
    return 0;
}
/* The hero as a neighbour's actors see him: his place in that room's
 * coordinates, clamped to its edge (a monster coming for him across a seam
 * keeps coming until it crosses, Seams, fax_seams.c): his X to 0-255, his Y
 * to 0-239, so a room above sees him past its bottom (slice 5) and not, as
 * the game's sprites read a Y of $F0 and up, wrapped past its top;
 * invincible, not swinging, with no spell and no buttons. What measures his
 * distance or places itself by him goes by his true place instead
 * (fax_world_hero(); Seams' slice 6). */
static void shadow(unsigned id) {
    Room *r = &rooms[id], *a = &rooms[active];
    p(0x9e, clamp(a->x * RW + q(0x9e) - r->x * RW, 0, 255));
    p(0xa1, clamp(a->y * RH + q(0xa1) - r->y * RH, 0, 239));
    p(0xad, 200);
    p(0xae, 0);
    p(0x2b3, 255);
    p(0x16, 0);
    p(0x19, 0);
}
static bool initialize(unsigned id) {
    Room *r = &rooms[id];
    if (!cyc_mod_isolate_begin()) return false;
    for (unsigned i = 0; i < 8192; i++) r->chr[i] = cyc_render_chr((uint16_t)i);
    memcpy(r->palette, cyc_ppu_palette(), 32);
    p(0x63, id);
    p(0x64, id);
    p(0x1f, 0);
    p(0x20, 0);
    bool ok = call(0xd127, 0, 0);    /* Area_LoadScrollData, table + four collision borders */
    if (ok) ok = call(0xd276, 0, 0); /* Area_LoadBlockProperties */
    if (ok) ok = bank(14) && call(0xc130, 0, 0) && call(0xc1b4, 0, 0);
    /* Hooks are off in this scope: Fair entrances moves its enemies here. */
    if (ok) fax_entrances_room_loaded();
    transfer = r;
    speculative = true;
    cyc_mod_allow_isolated_hooks(true);
    if (ok) ok = call(0xc28d, 0, 0);
    drain();
    speculative = false;
    transfer = NULL;
    if (ok) {
        capture(id, false, FAX_SEAMS_FRESH);
        fixup_room(id);
        r->ticks = 0;
    }
    cyc_mod_isolate_end();
    if (!ok) failures++;
    return ok;
}
static void tick_room(unsigned id) {
    Room *r = &rooms[id];
    if (!r->live && !initialize(id)) return;
    if (!cyc_mod_isolate_begin()) {
        failures++;
        return;
    }
    load(id);
    shadow(id);
    p(0x54, 255);
    p(0xc, 0);
    p(0xd, 0);
    p(0x57, 0);
    p(0x58, 0);
    p(0x59, 0);
    p(0x9f, 0);
    p(0xa2, 0);
    for (unsigned i = 0x700; i < 0x800; i += 4) p(i, 0xf0);
    transfer = r;
    speculative = true;
    cyc_mod_allow_isolated_hooks(true);
    p(0x1f, 0);
    p(0x20, 0);
    ticking = true;
    tick_id = (int)id;
    bool ok = bank(14) && call(0xcb17, 0, 0) && call(0x8000, 0, 0);
    ticking = false;
    tick_id = -1;
    drain();
    speculative = false;
    transfer = NULL;
    if (ok) {
        capture(id, false, FAX_SEAMS_TICK);
        r->ticks++;
    }
    cyc_mod_isolate_end();
    if (!ok) failures++;
}
static int player_hook(uint16_t pc) {
    (void)pc;
    if (!enabled || cyc_mod_isolated() || !supported()) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned caller = q(0x100 | (uint8_t)(regs.s + 1)) | (q(0x100 | (uint8_t)(regs.s + 2)) << 8);
    player_tick = caller >= 0xdb45 && caller < 0xdbef;
    if (!player_tick) {
        collision_active = false;
        cyc_mod_set_ram_read_hook(NULL);
        return 0;
    }
    if (scene_ready() && q(0x100) == 14) death_mode = script_mode = arriving = false; /* main loop resumed */
    if (playing()) refresh(false);
    pre_x = q(0x9e);
    pre_y = q(0xa1);
    pending = -1;
    ticks++;
    collision_active = true;
    cyc_mod_set_ram_read_hook(collision_read);
    return 0;
}
static int seam_hook(uint16_t pc) {
    (void)pc;
    if (!enabled || cyc_mod_isolated() || active < 0 || area != q(0x24)) return 0;
    CycModRegs regs;
    cyc_mod_regs(&regs);
    unsigned d = regs.x, next = q(0x63);
    if (d > 3 || rooms[active].edges[d] != next) return 0;
    /* The native right scroll begins when the player's right edge crosses.
     * Retain source ownership until his origin wraps; this keeps position
     * continuous instead of teleporting the 16-pixel body at x=241. */
    if (!rooms[next].live && !initialize(next)) return 0;
    pending = (int)next;
    direction = (int)d;
    pending_y = q(0xa1);
    p(0x63, active);
    p(0x54, d);
    return 1;
}
static bool adopt(unsigned next, int x, int y) {
    uint64_t start = SDL_GetPerformanceCounter();
    capture((unsigned)active, true, FAX_SEAMS_LIVE);
    load(next);
    for (unsigned i = 0x900; i < 0x1000; i++) cyc_mod_chr_poke((uint16_t)i, rooms[next].chr[i]);
    p(0x9e, x);
    p(0xa1, y);
    p(0x9f, 0);
    p(0xa2, 0);
    p(0xc, 0);
    p(0xd, 0);
    p(0x57, 0);
    p(0x58, 0);
    p(0x59, 0);
    p(0x54, 255);
    p(0xe, 0);
    p(0xb4, 0);
    /* The spell flies on through a crossing sideways (Seams' slice 3,
     * fax_seams.c): its side moves by the rooms' places. Up or down, or with
     * Seams off, CastMagic_Type goes $FF, as it always did here. */
    fax_seams_adopted(active, (int)next);
    active = (int)next;
    handoffs++;
    double cost = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
    if (cost > max_seam_ms) max_seam_ms = cost;
    return true;
}
static void sync_nametable(void) {
    uint8_t nt[2048];
    size_t n;
    const uint8_t *old = cyc_ppu_ciram(&n);
    if (n != sizeof nt) {
        failures++;
        return;
    }
    memcpy(nt, old, sizeof nt);
    Room *r = &rooms[active];
    for (unsigned page = 0; page < 2; page++)
        for (unsigned by = 0; by < 13; by++)
            for (unsigned bx = 0; bx < 16; bx++) {
                unsigned block = r->ram[0x600 + by * 16 + bx];
                for (unsigned dy = 0; dy < 2; dy++)
                    for (unsigned dx = 0; dx < 2; dx++) {
                        unsigned x = bx * 2 + dx, y = 4 + by * 2 + dy, ptr = word(0x80 + (dy * 2 + dx) * 2);
                        nt[page * 1024 + y * 32 + x] = (uint8_t)data(3, ptr + block);
                        unsigned index = page * 1024 + 960 + (y / 4) * 8 + x / 4,
                                 shift = (y & 2 ? 4 : 0) + (x & 2 ? 2 : 0);
                        unsigned pal = data(3, word(0x7e) + block) & 3;
                        nt[index] = (uint8_t)((nt[index] & ~(3u << shift)) | (pal << shift));
                    }
            }
    if (!cyc_mod_ciram_upload(0, nt, sizeof nt)) failures++;
}
static int draw_hook(uint16_t pc) {
    (void)pc;
    if (!enabled || cyc_mod_isolated() || active < 0) return 0;
    cyc_mod_set_ram_read_hook(NULL);
    collision_active = false;
    if (death_mode || script_mode) {
        pending = -1;
        return 0;
    }
    if (!player_tick || q(0x24) != area || q(0x63) != active) {
        pending = -1;
        return 0;
    }
    int x = q(0x9e), y = q(0xa1), next = pending;
    if (next >= 0) {
        if (direction == 0) x = 255;
        else if (direction == 1) {
            next = -1;
        } else if (direction == 2) y = (pending_y + RH) & 255;
        else if (pending_y >= RH) y = pending_y - RH;
        else {
            p(0xa1, pending_y);
            next = -1;
        }
        p(0x54, 255);
        p(0x9f, 0);
        p(0xa2, 0);
        pending = -1;
    }
    if (next < 0 && pre_x > 240 && x < 16) {
        next = rooms[active].edges[1];
        if (next == 255) next = -1;
    }
    if (next >= 0) {
        uint64_t start = SDL_GetPerformanceCounter();
        if (!rooms[next].live && !initialize((unsigned)next)) return 0;
        adopt((unsigned)next, x, y);
        sync_nametable();
        double cost = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
        if (cost > max_seam_ms) max_seam_ms = cost;
    }
    return 0;
}
static void step(void);
static void camera_at(int hx, int hy, int *cx, int *cy);
/* Seamless takes a room in play (refresh, from step and player_hook) only on
 * a frame that ends in the main loop's bank, its screen set up, with neither
 * its script nor its death mode on, and those go off only at Game_MainLoop's
 * next tick. A door whose key says "I've used key." leaves script_mode on, a
 * zone respawn death_mode; and the game turns the new room's picture back on
 * (Screen_ResetForGamePlay) a frame before it waits in
 * GameLoop_LoadSpriteImages for the sprites' tiles. So the room showed 8 to
 * 15 frames before that tick, as the game's own picture centred, and jumped
 * to its place as the loop began (#204). Now the map is laid out from the new
 * room on the first frame its picture shows the status bar, the game's
 * screen set up (scene_ready()): the room is at its place under the camera
 * from that frame, the rooms round it with it. Until the loop's tick it is
 * the game's own picture there (arriving, fax_world_present), as through a
 * death: the room's capture has no sprites yet, and a script may run on. */
static void arrive(void) {
    if ((active >= 0 && area == q(0x24) && active == q(0x63)) || !scene_ready() || q(0x120) || !fax_text_hud_showing())
        return;
    refresh(false);
    arriving = active >= 0 && area == q(0x24) && active == q(0x63);
}
/* NESRECOMP_WORLD_TRACE: a line a frame, after Seamless's step: the live
 * game's room and hero, then the camera the frame's picture has
 * (fax_world_present's, F4's map aside) with the hero it is worked out from
 * (the room's capture: the last frame's on a frame the game lagged), and the
 * view Seams wakes actors in (fax_world_view), in world pixels; blank where
 * there is none. Then where the room's top-left sits in that picture
 * (fax_world_origin's), blank with no camera, and whether the room is still
 * arriving (arrive(): the game's own picture of it, at that place). */
void fax_world_step(void) {
    if (!enabled || !supported()) return;
    if (!trace) {
        const char *path = getenv("NESRECOMP_WORLD_TRACE");
        if (path && *path) {
            trace = fopen(path, "w");
            if (trace)
                fprintf(
                    trace,
                    "cycles,area,room,x,y,scroll,ready,handoffs,failures,simulation_ms,camera_x,camera_y,shown_x,shown_y,view_x,view_y,origin_x,origin_y,arriving\n");
        }
    }
    step();
    if (!trace) return;
    char shown[48] = ",,,", seen[32] = ",", placed[32] = ",";
    int cx, cy;
    if (active >= 0 && area == q(0x24) && active == q(0x63)) {
        const uint8_t *r = rooms[active].ram;
        camera_at(r[0x9e], r[0xa1], &cx, &cy);
        snprintf(shown, sizeof shown, "%d,%d,%u,%u", cx, cy, r[0x9e], r[0xa1]);
        snprintf(placed, sizeof placed, "%d,%d", rooms[active].x * RW - cx, HUD + rooms[active].y * RH - cy);
    }
    if (view_ok) snprintf(seen, sizeof seen, "%d,%d", view_x, view_y);
    fprintf(trace, "%llu,%u,%u,%u,%u,%u,%u,%u,%u,%.4f,%s,%s,%s,%d\n", (unsigned long long)cyc_cycle_count(), q(0x24),
            q(0x63), q(0x9e), q(0xa1), q(0x54), q(0xe), handoffs, failures, sim_ms, shown, seen, placed, arriving);
    fflush(trace);
}
static void step(void) {
    /* The status bar band is the game's own picture's, taken every frame
     * that picture shows it (fax_text_hud_showing), not only in play: in a
     * conversation, a shop or a script the world keeps its pose, but gold
     * counts down as a purchase is paid and up as the King gives it, and HP,
     * MP and T: change, as in the original. Pausing and the Select menu take
     * it too; the game changes nothing on it then. A frame without it (the
     * screen off while one loads) keeps the last band shown (#132). */
    if (fax_text_hud_showing()) {
        memcpy(hud_band, cyc_frame_argb(), sizeof hud_band);
        hud_valid = true;
    }
    /* A frame that ended inside Seams' visitors' pass (fax_seams.c, a lag
     * frame): its visitors' rooms tick, and the live room is captured, once
     * the pass has written them back. */
    if (fax_seams_visiting()) return;
    if (!playing()) {
        arrive();
        return;
    }
    /* A frame the game lagged keeps the last capture, as the PPU keeps its
     * last sprites then (No sprite flicker, fax_flicker.c). */
    uint64_t start = SDL_GetPerformanceCounter();
    refresh(fax_flicker_frame_drawn());
    if (active < 0) return;
    if (ticks == last_tick) return;
    last_tick = ticks;
    int w = VIEW_W, h = VIEW_H;
    Room *a = &rooms[active];
    fax_world_update_view();
    for (unsigned id = 0; id < counts[area]; id++)
        if (id != (unsigned)active && rooms[id].owned) {
            Room *r = &rooms[id];
            int dx = abs(r->x - a->x) * RW, dy = abs(r->y - a->y) * RH;
            if (atlas || (dx < w / 2 + RW && dy < (h - HUD) / 2 + RH)) tick_room(id);
        }
    /* Between frames, once every room has run: actors that crossed a seam
     * move to the room they entered, and shots that left the view go. */
    fax_seams_step();
    sim_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
}
/* The box, in rooms, round every room on the map: F4's map shows it all. */
static void region(int *l, int *t, int *r, int *b) {
    Room *a = &rooms[active];
    *l = *r = a->x;
    *t = *b = a->y;
    for (unsigned i = 0; i < counts[area]; i++)
        if (rooms[i].owned) {
            Room *v = &rooms[i];
            if (v->x < *l) *l = v->x;
            if (v->x > *r) *r = v->x;
            if (v->y < *t) *t = v->y;
            if (v->y > *b) *b = v->y;
        }
}
/* The map as the camera (fax_camera.c, #166's slice 4) sees it: the room
 * on the map at a place, drawn there; -1 for none. */
static int room_at_place(int col, int row) {
    for (unsigned i = 0; i < counts[area]; i++) {
        Room *v = &rooms[i];
        if (v->owned && v->valid && v->x == col && v->y == row) return (int)i;
    }
    return -1;
}
static bool camera_room(void *ctx, int col, int row) {
    (void)ctx;
    return room_at_place(col, row) >= 0;
}
/* Where the hero can pass between the room at (col, row - 1) and the one
 * under it, joined up and down: the block columns where the upper room's
 * bottom row and the lower room's top row are both passable
 * (BLOCK_PROPERTY_IMPASSABLE_MAP, $E8D9: a ladder is), as ScreenBuffer
 * and BlockProperties ($0600, $043C) have them in each room's record. */
static unsigned camera_openings(void *ctx, int col, int row) {
    (void)ctx;
    int d = room_at_place(col, row), u = room_at_place(col, row - 1);
    if (d < 0 || u < 0 || rooms[d].edges[2] != u) return 0;
    unsigned bits = 0;
    for (unsigned bx = 0; bx < 16; bx++) {
        const uint8_t *ru = rooms[u].ram, *rd = rooms[d].ram;
        unsigned bu = ru[0x600 + 0xc0 + bx], bd = rd[0x600 + bx];
        unsigned pu = bu & 1 ? ru[0x43c + (bu >> 1)] >> 4 : ru[0x43c + (bu >> 1)] & 15,
                 pd = bd & 1 ? rd[0x43c + (bd >> 1)] >> 4 : rd[0x43c + (bd >> 1)] & 15;
        if (!data(15, 0xe8d9 + pu) && !data(15, 0xe8d9 + pd)) bits |= 1u << bx;
    }
    return bits;
}
static const FaxCameraMap camera_map = { camera_room, camera_openings, NULL };
/* The view's top-left in world pixels (the status bar above it), with the
 * hero at (hx,hy) of the active room: Seamless's camera, over rooms only
 * (fax_camera.c). */
static void camera_at(int hx, int hy, int *cx, int *cy) {
    Room *a = &rooms[active];
    fax_camera_place(&camera_map, a->x * RW + hx, a->y * RH + hy, cx, cy);
}
void fax_world_update_view(void) {
    view_ok = false;
    if (!enabled || cyc_mod_isolated() || active < 0 || area != q(0x24) || active != q(0x63)) return;
    camera_at(q(0x9e), q(0xa1), &view_x, &view_y);
    view_ok = true;
    hero_wx = rooms[active].x * RW + q(0x9e);
    hero_wy = rooms[active].y * RH + q(0xa1);
}
bool fax_world_view(int *x, int *y) {
    if (!view_ok) return false;
    *x = view_x;
    *y = view_y;
    return true;
}
bool fax_world_hero(int *x, int *y) {
    if (!view_ok) return false;
    *x = hero_wx;
    *y = hero_wy;
    return true;
}
static unsigned terrain(Room *r, int x, int y) {
    unsigned block = r->ram[0x600 + (y / 16) * 16 + x / 16], quadrant = (x & 8 ? 1 : 0) + (y & 8 ? 2 : 0);
    unsigned ptr = word(0x80 + quadrant * 2), tile = data(3, ptr + block);
    unsigned attr = data(3, word(0x7e) + block) & 3, off = 0x1000 + tile * 16 + (y & 7), shift = 7 - (x & 7);
    unsigned v = ((r->chr[off] >> shift) & 1) | (((r->chr[off + 8] >> shift) & 1) << 1);
    return v ? attr * 4 + v : 0;
}
static void raster(unsigned id) {
    Room *r = &rooms[id];
    if (!r->live) {
        Room *a = &rooms[active];
        memcpy(r->chr, a->chr, 8192);
        memcpy(r->palette, a->palette, 32);
    }
    /* Background palette is global to the region, including fades. Isolated
     * NPC/actor loaders may temporarily select other palettes; those belong
     * only to their instance's sprites, never the neighboring terrain. */
    const uint8_t *pal = cyc_ppu_palette();
    unsigned emphasis = cyc_render_line_mask(239) & 0xe9;
    if (raster_valid[id] && !memcmp(raster_blocks[id], r->ram + 0x600, 256) && !memcmp(raster_palette[id], pal, 16) &&
        raster_palette[id][16] == emphasis && !memcmp(raster_chr[id], r->chr + 0x1000, 4096))
        return;
    uint32_t colors[16];
    for (unsigned i = 0; i < 16; i++)
        colors[i] = hw_palette_argb[(pal[i] & ((emphasis & 1) ? 0x30 : 0x3f)) | ((emphasis >> 5) << 6)];
    if (!(emphasis & 8))
        for (unsigned i = 1; i < 16; i++) colors[i] = colors[0];
    for (int y = 0; y < RH; y++)
        for (int x = 0; x < RW; x++) rasters[id][y * RW + x] = colors[terrain(r, x, y)];
    memcpy(raster_blocks[id], r->ram + 0x600, 256);
    memcpy(raster_palette[id], pal, 16);
    raster_palette[id][16] = (uint8_t)emphasis;
    memcpy(raster_chr[id], r->chr + 0x1000, 4096);
    raster_valid[id] = true;
}
/* The CHR an actor's tiles are in: its room's, unless it came through a
 * seam into a room that has no tiles for it (Seams' tiles from home). */
static const uint8_t *tiles_of(Room *r, int from) {
    return from >= 0 && from < MAX_ROOMS && rooms[from].live ? rooms[from].chr : r->chr;
}
/* The room joined to r one step along edge e (0 left, 1 right, 2 above, 3
 * below), decoded: NULL for none. */
static Room *joined_room(Room *r, unsigned e) {
    unsigned n = r ? r->edges[e] : 255;
    return n < MAX_ROOMS && rooms[n].valid ? &rooms[n] : NULL;
}
/* The room pixel (*lx, *ly) of room r lies in, through the rooms joined to
 * it, sideways, up or down, or both (a corner: either way round, the map is
 * a grid), with the pixel moved into its coordinates: NULL past a side with
 * no room joined, or further than one room. */
static Room *room_under(Room *r, int *lx, int *ly) {
    int dx = *lx < 0 ? -1 : *lx >= RW ? 1 : 0, dy = *ly < 0 ? -1 : *ly >= RH ? 1 : 0;
    if (*lx < -RW || *lx >= 2 * RW || *ly < -RH || *ly >= 2 * RH) return NULL;
    unsigned ex = dx < 0 ? 0 : 1, ey = dy < 0 ? 2 : 3;
    Room *u = r;
    if (dx && dy) {
        Room *a = joined_room(joined_room(r, ex), ey);
        u = a ? a : joined_room(joined_room(r, ey), ex);
    } else if (dx) u = joined_room(r, ex);
    else if (dy) u = joined_room(r, ey);
    *lx -= dx * RW, *ly -= dy * RH;
    return u;
}
/* One 8x8 sprite cell of room r at (sx,sy) of its picture (sy as OAM Y + 1,
 * the status bar included). A pixel past the room's side, top or bottom
 * falls in the room joined there (Seams' drawn whole, fax_seams.c), whose
 * terrain its behind-the-background bit is tested against; past one with no
 * room joined it isn't drawn. */
static void cell(Room *r, const uint8_t *chr, int sx, int sy, unsigned tile, unsigned attr, int w, int h,
                 const uint8_t *pal, unsigned mask) {
    int ox = r->x * RW - camera_x, oy = HUD + r->y * RH - camera_y;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int dx = ox + sx + x, dy = oy + sy - HUD + y;
            if (dx < 0 || dx >= w || dy < HUD || dy >= h) continue;
            unsigned py = attr & 128 ? 7 - y : y, shift = attr & 64 ? x : 7 - x, off = tile * 16 + py;
            unsigned v = ((chr[off] >> shift) & 1) | (((chr[off + 8] >> shift) & 1) << 1);
            if (!v) continue;
            int lx = sx + x, ly = sy - HUD + y;
            Room *under = room_under(r, &lx, &ly);
            if (!under) continue;
            if ((attr & 32) && terrain(under, lx, ly)) continue;
            canvas[(size_t)dy * w + dx] =
                hw_palette_argb[(pal[16 + (attr & 3) * 4 + v] & ((mask & 1) ? 0x30 : 0x3f)) | ((mask >> 5) << 6)];
        }
}
static void actors(unsigned id, int w, int h) {
    Room *r = &rooms[id];
    if (!r->live) return;
    /* Sprite palettes are global to the connected region just like terrain
     * palettes. A neighbor initialized during an arrival may have inherited
     * a building/fade palette; it must use the active scene's colors. */
    const uint8_t *pal = rooms[active].palette;
    unsigned mask = cyc_render_line_mask(239);
    /* The cells the room's actors drew past its sides, top and bottom, which
     * the NES skips or hides (Seams' drawn whole), behind its own sprites. */
    const FaxSeamsCell *cells;
    unsigned n = fax_seams_cells(r->seams, &cells);
    for (unsigned k = 0; k < n; k++) {
        const FaxSeamsCell *c = &cells[k];
        cell(r, tiles_of(r, fax_seams_slot_tiles(r->seams, c->slot)), c->x, c->y + 1, c->tile, c->attr, w, h, pal,
             mask);
    }
    /* Every sprite, back to front in the order the game drew them (No sprite
     * flicker, fax_flicker.c, from the room's own Sprites_StartSlotRange). */
    uint8_t order[FAX_FLICKER_SPRITES];
    unsigned count = fax_flicker_paint_order(r->ram[0x1c], order);
    for (unsigned k = 0; k < count; k++) {
        int s = order[k];
        const uint8_t *o = r->ram + 0x700 + s * 4;
        int sy = o[0] + 1, sx = o[3];
        int slot = fax_seams_oam_slot(r->seams, (unsigned)s);
        /* Hidden (line 240 and down: those an actor drew are among its cells
         * past the bottom), or over the status bar and no actor's (an
         * actor's cell there lies in the room above). */
        if (sy >= 240 || (sy < HUD && slot < 0)) continue;
        /* The hero's own cells that wrap round the screen as he stands past
         * x 240; never an actor's. */
        if (id == (unsigned)active && slot < 0) {
            int px = r->ram[0x9e];
            if (px > 240 && sx < 32) sx += 256;
        }
        cell(r, tiles_of(r, slot < 0 ? -1 : fax_seams_slot_tiles(r->seams, (unsigned)slot)), sx, sy, o[1], o[2], w, h,
             pal, mask);
    }
}
/* The NES clips player tiles at its 256x240 raster even though the world
 * continues there. Ask the original drawing routines for an unclipped pose
 * at a safe scratch position, then supply just its off-room pixels. */
static void player_edge(int w, int h) {
    Room *r = &rooms[active];
    int px = r->ram[0x9e], py = r->ram[0xa1];
    if (px >= 8 && px <= 232 && py >= 8 && py <= 176) return;
    uint8_t oam[256], range = 0;
    if (!cyc_mod_isolate_begin()) return;
    /* Use the same retained player pose during dialogue, even while native
     * portrait code temporarily changes drawing state. */
    for (unsigned i = 0; i < 2048; i++) p(i, r->ram[i]);
    p(0x9e, 128);
    p(0xa1, 96);
    p(0x9f, 0);
    p(0xa2, 0);
    p(0xc, 0);
    p(0x57, 0);
    bool ok = bank(14) && call(0xcb17, 0, 0) && call(0xb982, 0, 0) && call(0xebee, 0, 0) && call(0xb7d6, 0, 0);
    if (ok) {
        for (unsigned i = 0; i < 256; i++) oam[i] = q(0x700 + i);
        range = q(0x1c);
    }
    cyc_mod_isolate_end();
    if (!ok) {
        failures++;
        return;
    }
    int ox = r->x * RW - camera_x, oy = HUD + r->y * RH - camera_y;
    unsigned mask = cyc_render_line_mask(239);
    uint8_t order[FAX_FLICKER_SPRITES];
    unsigned count = fax_flicker_paint_order(range, order);
    for (unsigned k = 0; k < count; k++) {
        int s = order[k];
        const uint8_t *o = oam + s * 4;
        if (o[0] >= 239) continue;
        int sx = o[3] + px - 128, sy = o[0] + 1 - HUD + py - 96;
        unsigned attr = o[2], tile = o[1];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int lx = sx + x, ly = sy + y;
                if (lx >= 0 && lx < RW && ly >= 0 && ly < RH) continue;
                int dx = ox + lx, dy = oy + ly;
                if (dx < 0 || dx >= w || dy < HUD || dy >= h) continue;
                unsigned cy = attr & 128 ? 7 - y : y, shift = attr & 64 ? x : 7 - x, off = tile * 16 + cy;
                unsigned v = ((r->chr[off] >> shift) & 1) | (((r->chr[off + 8] >> shift) & 1) << 1);
                if (!v) continue;
                canvas[(size_t)dy * w + dx] =
                    hw_palette_argb[(r->palette[16 + (attr & 3) * 4 + v] & ((mask & 1) ? 0x30 : 0x3f)) |
                                    ((mask >> 5) << 6)];
            }
    }
}
/* A native text box or menu panel, in native tile units, copied over the
 * world at the native picture's place: centred, under the HUD. Every view
 * (424x240, or F4's map) is at least a room wide and tall. */
static void menu_rect(int width, const uint32_t *native, int x, int y, int w, int h) {
    int px = (width - RW) / 2;
    for (int sy = y < HUD ? HUD : y; sy < y + h && sy < 240; sy++)
        for (int sx = x < 0 ? 0 : x; sx < x + w && sx < RW; sx++)
            canvas[(size_t)sy * width + px + sx] = native[sy * RW + sx];
}
static bool ui_pixel(int x, int y) {
    if ((!script_mode && q(0x100) != 12) || q(0x20c)) return false;
    int bx = q(0x208) * 8, by = q(0x209) * 8;
    if (bx == 32 && by == 64 && x >= 16 && x < 96 && y >= 48 && y < 48 + (int)fax_menu_height() * 8) return true;
    return x >= bx && x < bx + q(0x20a) * 8 && y >= by && y < by + q(0x20b) * 8;
}
/* The status bar band (its 32 lines) and the Elixir left of it
 * (fax_charges.h) are centred as one group while the status bar shows,
 * whether he has an Elixir or not; with no Elixir's place (the remaster off)
 * or no status bar (the title), the band is centred like the rest. Only the
 * band moves: rooms, the native picture below the band, and its text boxes
 * and menus stay centred in the view. */
static int layout_x(int width, bool hud) {
    int left = fax_charges_width();
    return hud && left && width >= RW + left ? (width - RW - left) / 2 + left : (width - RW) / 2;
}
int fax_world_hud_x(int width) {
    if (enabled && hud_x >= 0 && width == hud_w) return hud_x;
    return cyc_render_native_origin(width);
}
/* A load's first frame can still show the room it leaves: at a town's edge
 * (Game_SetupNewArea) Area_CurrentArea is the new area's on the frame its
 * picture still shows the old room, which goes dark only on the next. The
 * game's own picture centred would move that room for a frame (#204), so
 * while the game is loading (Game_InterruptHandlersEnabled 0) and its picture
 * is still lit, Seamless goes on composing the room it holds, as it did on
 * the frame before. After a dark frame it never does again before it takes a
 * room: what lights up then is the next room. */
static bool leaving(const uint32_t *native) {
    if (!world_shown || active < 0 || q(0x13)) return false;
    for (int i = HUD * RW; i < 240 * RW; i++)
        if (native[i] & 0xffffff) return true;
    return false;
}
const uint32_t *fax_world_present(int *w, int *h, const uint32_t *native) {
    /* Start pauses native simulation, not the expanded picture. Select's
     * native inventory is composited over the frozen world at the same scale.
     * Never show a cached region while an original portal is replacing it. */
    origin_valid = composed = false;
    if (!enabled || !supported()) return NULL;
    uint64_t start = SDL_GetPerformanceCounter();
    *w = VIEW_W;
    *h = VIEW_H;
    bool same = (active >= 0 && area == q(0x24) && active == q(0x63)) || leaving(native);
    world_shown = same;
    Room *a = same ? &rooms[active] : NULL;
    int l = 0, t = 0, r = 0, b = 0;
    if (same) {
        region(&l, &t, &r, &b);
        if (atlas) {
            *w = clamp((r - l + 1) * RW, 128, 4096);
            *h = clamp((b - t + 1) * RH + HUD, 96, 2048);
        }
    }
    int width = *w, height = *h;
    size_t n = (size_t)width * height;
    if (n > capacity) {
        uint32_t *v = realloc(canvas, n * sizeof *v);
        if (!v) return NULL;
        canvas = v;
        capacity = n;
    }
    composed = true;
    for (size_t i = 0; i < n; i++) canvas[i] = 0xff000000;
    /* Native scripts and region loaders keep their original image, centred
     * in the same fixed canvas, with its status bar band where play has it.
     * No frame may change the viewport to 4:3. */
    hud_w = width;
    hud_x = layout_x(width, same || fax_text_hud_showing());
    if (!same) {
        int nx = (width - RW) / 2;
        for (int y = 0; y < 240; y++)
            memcpy(canvas + (size_t)y * width + (y < HUD ? hud_x : nx), native + y * RW, RW * sizeof *canvas);
        goto finished;
    }
    if (atlas) {
        camera_x = l * RW;
        camera_y = t * RH;
    } else camera_at(a->ram[0x9e], a->ram[0xa1], &camera_x, &camera_y);
    for (unsigned id = 0; id < counts[area]; id++)
        if (rooms[id].owned && rooms[id].valid) {
            Room *v = &rooms[id];
            int ox = v->x * RW - camera_x, oy = HUD + v->y * RH - camera_y;
            if (ox >= width || ox + RW <= 0 || oy >= height || oy + RH <= HUD) continue;
            raster(id);
            int x0 = ox < 0 ? -ox : 0, x1 = width - ox < RW ? width - ox : RW;
            for (int y = 0; y < RH; y++) {
                int dy = oy + y;
                if (dy < HUD || dy >= height) continue;
                memcpy(canvas + (size_t)dy * width + ox + x0, rasters[id] + y * RW + x0, (size_t)(x1 - x0) * 4);
            }
        }
    origin_x = a->x * RW - camera_x;
    origin_y = HUD + a->y * RH - camera_y;
    origin_valid = true;
    /* A death, and a room the game shows before its main loop runs there
     * (arriving): the game's own picture, at the room's place. */
    bool original_scene = death_mode || arriving;
    if (original_scene) {
        int ox = a->x * RW - camera_x, oy = HUD + a->y * RH - camera_y;
        for (int y = HUD; y < 240; y++)
            for (int x = 0; x < RW; x++) {
                int dx = ox + x, dy = oy + y - HUD;
                if (dx >= 0 && dx < width && dy >= HUD && dy < height && !ui_pixel(x, y))
                    canvas[(size_t)dy * width + dx] = native[y * RW + x];
            }
    } else {
        for (unsigned id = 0; id < counts[area]; id++)
            if (rooms[id].owned && id != (unsigned)active) actors(id, width, height);
        actors((unsigned)active, width, height);
        player_edge(width, height);
    }
    if ((script_mode || q(0x100) == 12) && !q(0x20c)) {
        /* Overlay native text boxes only. Copying the entire NES image
         * would erase frozen actors inside its rectangle while leaving
         * the neighboring actors visible. Inventory submenus retain the
         * top menu underneath them (10 tiles wide, and 18 tall with the
         * Select menu's QUESTS and MAP: fax_menu.h). All coordinates are
         * native tile units. */
        int x = q(0x208) * 8, y = q(0x209) * 8;
        if (x == 32 && y == 64) menu_rect(width, native, 16, 48, 80, (int)fax_menu_height() * 8);
        if (q(0x3c7) < 128) menu_rect(width, native, 16, 112, 64, 80);
        menu_rect(width, native, x, y, q(0x20a) * 8, q(0x20b) * 8);
    }
    /* The rooms and actors may hold a pose, but the band is this frame's
     * whenever the game shows it (hud_band, fax_world_step). */
    const uint32_t *hud = hud_valid && !original_scene ? hud_band : native;
    for (int y = 0; y < HUD; y++) memcpy(canvas + (size_t)y * width + hud_x, hud + y * RW, RW * sizeof *canvas);
finished:
    picture_w = width;
    picture_h = height;
    render_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
    return canvas;
}
void fax_world_reset(void) {
    hud_x = -1;
    hud_w = 0;
    make_mask();
    memset(rooms, 0, sizeof rooms);
    memset(raster_valid, 0, sizeof raster_valid);
    area = active = -1;
    pending = -1;
    events_area = -1;
    fighting = false;
    rom_ok = false;
    hud_valid = death_mode = script_mode = arriving = world_shown = false;
    ticks = last_tick = handoffs = failures = portals = 0;
    player_tick = collision_active = false;
    screen_loaded = view_ok = false;
    tick_id = -1;
    cyc_mod_set_ram_read_hook(NULL);
}
bool fax_world_ticking(void) { return ticking; }
bool fax_world_composed(void) { return composed; }
static bool on_map(int id) { return enabled && area >= 0 && id >= 0 && id < (int)counts[area] && rooms[id].owned; }
int fax_world_seams_room(void) {
    if (!enabled || active < 0) return -1;
    return ticking ? tick_id : cyc_mod_isolated() ? -1 : active;
}
int fax_world_active_room(void) { return enabled ? active : -1; }
int fax_world_area(void) { return enabled ? area : -1; }
bool fax_world_room_live(int id) { return on_map(id) && rooms[id].live; }
int fax_world_joined(int id, int side) {
    if (!on_map(id) || side < 0 || side > 3) return -1;
    unsigned n = rooms[id].edges[side];
    return n < MAX_ROOMS && fax_world_room_live((int)n) ? (int)n : -1;
}
uint8_t *fax_world_seams_block(int id) { return on_map(id) ? rooms[id].seams : NULL; }
uint8_t *fax_world_room_record(int id) { return on_map(id) ? rooms[id].ram : NULL; }
void fax_world_place(int id, int *x, int *y) {
    *x = on_map(id) ? rooms[id].x * RW : 0;
    *y = on_map(id) ? rooms[id].y * RH : 0;
}
uint8_t fax_world_rom(unsigned b, unsigned addr) { return supported() ? (uint8_t)data(b & 15, addr) : 0; }
void fax_world_screen_loaded(void) {
    if (enabled && !cyc_mod_isolated()) screen_loaded = true;
}
bool fax_world_origin(int *x, int *y) {
    if (!origin_valid) return false;
    *x = origin_x;
    *y = origin_y;
    return true;
}
bool fax_world_room_origin(int a, int id, int *x, int *y) {
    if (!origin_valid || a != area || id < 0 || id >= (int)counts[area] || !rooms[id].owned) return false;
    *x = rooms[id].x * RW - camera_x;
    *y = HUD + rooms[id].y * RH - camera_y;
    return true;
}
const uint8_t *fax_world_room_ram(int a, int id) {
    if (!enabled || !origin_valid || a != area || id < 0 || id >= (int)counts[area] || id == active ||
        !rooms[id].owned || !rooms[id].live)
        return NULL;
    return rooms[id].ram;
}
void fax_world_loaded(void) {
    memset(raster_valid, 0, sizeof raster_valid);
    world_shown = false; /* a loaded state's first frame is never one a load leaves (leaving()) */
    cyc_mod_set_ram_read_hook(collision_active ? collision_read : NULL);
}
bool fax_world_option(const char *name, const char *value) {
    if (!strcmp(name, "--world-view")) {
        if (strcmp(value, "off") && strcmp(value, "seamless") && strcmp(value, "atlas")) return false;
        enabled = strcmp(value, "off") != 0;
        hooks(enabled);
        atlas = !strcmp(value, "atlas");
        if (enabled) nes_mod_set_local_only("Seamless world", 1);
        return true;
    }
    return false;
}
void fax_world_event(const void *ev) {
#ifdef CYC_WITH_SDL
    const SDL_Event *e = ev;
    if (!enabled) return;
    if (e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.sym == SDLK_F4) atlas = !atlas;
#else
    (void)ev;
#endif
}
#ifdef CYC_WITH_SDL
static void status(int id, const char *line) {
    (void)line;
    char out[850];
    unsigned live = 0, owned = 0, actors_count = 0;
    uint64_t sum = 0;
    for (unsigned i = 0; i < MAX_ROOMS; i++) {
        Room *r = &rooms[i];
        owned += r->owned;
        live += r->live;
        sum += r->ticks;
        if (r->live)
            for (unsigned j = 0; j < 8; j++) actors_count += r->ram[0x2cc + j] < 128;
    }
    snprintf(
        out, sizeof out,
        "\"enabled\":%s,\"area\":%d,\"room\":%d,\"owned\":%u,\"live\":%u,\"actors\":%u,\"neighbor_ticks\":%llu,\"handoffs\":%u,\"portals\":%u,\"failures\":%u,\"camera\":[%d,%d],\"size\":[%d,%d],\"simulation_ms\":%.3f,\"render_ms\":%.3f,\"max_seam_ms\":%.3f,\"playing\":%s,\"arena\":%s,\"hud_x\":%d",
        enabled ? "true" : "false", area, active, owned, live, actors_count, (unsigned long long)sum, handoffs, portals,
        failures, camera_x, camera_y, picture_w, picture_h, sim_ms, render_ms, max_seam_ms,
        playing() ? "true" : "false", fighting ? "true" : "false", fax_world_hud_x(picture_w));
    cyc_tcp_ok(id, out);
}
static void room_status(int id, const char *line) {
    long n = active;
    cyc_tcp_long(line, "room", &n);
    if (n < 0 || n >= MAX_ROOMS) {
        cyc_tcp_err(id, "invalid room");
        return;
    }
    Room *r = &rooms[n];
    char out[1100];
    /* The active room's slots are the live game's; a neighbour's its record's:
     * entity, X, Y, HP and hit counter. */
    static const unsigned at[5] = { 0x2cc, 0xba, 0xc2, 0x344, 0x34c };
    uint8_t slots[5][8];
    for (unsigned k = 0; k < 5; k++)
        for (unsigned j = 0; j < 8; j++) {
            bool here = n == active && enabled;
            slots[k][j] = here ? q(at[k] + j) : r->ram[at[k] + j];
        }
    char seams[200];
    fax_seams_describe(r->seams, seams, sizeof seams);
    char arrays[5][48];
    static const char *names[5] = { "entities", "x", "y", "hp", "hit_counter" };
    for (unsigned k = 0; k < 5; k++)
        snprintf(arrays[k], sizeof arrays[k], "[%u,%u,%u,%u,%u,%u,%u,%u]", slots[k][0], slots[k][1], slots[k][2],
                 slots[k][3], slots[k][4], slots[k][5], slots[k][6], slots[k][7]);
    snprintf(
        out, sizeof out,
        "\"room\":%ld,\"owned\":%u,\"event\":%u,\"xy\":[%d,%d],\"edges\":[%u,%u,%u,%u],\"live\":%u,\"ticks\":%llu,\"%s\":%s,\"%s\":%s,\"%s\":%s,\"%s\":%s,\"%s\":%s,%s,\"town\":%s",
        n, r->owned, events_area == area && n < MAX_ROOMS ? events[n] : 255u, r->x, r->y, r->edges[0], r->edges[1],
        r->edges[2], r->edges[3], r->live, (unsigned long long)r->ticks, names[0], arrays[0], names[1], arrays[1],
        names[2], arrays[2], names[3], arrays[3], names[4], arrays[4], seams,
        fax_seams_town((int)n) ? "true" : "false");
    cyc_tcp_ok(id, out);
}
static void probe(int id, const char *line) {
    (void)line;
    uint8_t *before = NULL, *after = NULL;
    size_t nb = 0, na = 0, n = 0;
    char err[256];
    bool saved = cyc_state_save(&before, &nb, err, sizeof err);
    const uint8_t *ciram = cyc_ppu_ciram(&n);
    uint8_t byte = ciram[0], changed = byte ^ 255;
    bool invalid = !cyc_mod_ciram_upload(SIZE_MAX, &changed, 1) && !cyc_mod_ciram_upload(n, &changed, 1) &&
                   !cyc_mod_ciram_upload(0, NULL, 1);
    bool isolated = false;
    if (cyc_mod_isolate_begin()) {
        isolated = !cyc_mod_ciram_upload(0, &changed, 1);
        cyc_mod_isolate_end();
    }
    bool valid =
        cyc_mod_ciram_upload(0, &changed, 1) && cyc_ppu_ciram(&n)[0] == changed && cyc_mod_ciram_upload(0, &byte, 1);
    bool exact = saved && cyc_state_save(&after, &na, err, sizeof err) && na == nb && !memcmp(before, after, nb);
    free(before);
    free(after);
    char out[256];
    snprintf(
        out, sizeof out,
        "\"invalid_inputs_rejected\":%s,\"isolated_upload_rejected\":%s,\"upload_and_restore\":%s,\"machine_state_unchanged\":%s",
        invalid ? "true" : "false", isolated ? "true" : "false", valid ? "true" : "false", exact ? "true" : "false");
    cyc_tcp_ok(id, out);
}
void fax_world_tcp_setup(void) {
    cyc_tcp_register("fax_world", "Continuous world status and component timings", status);
    cyc_tcp_register("fax_world_room", "Persistent neighboring room state", room_status);
    cyc_tcp_register("fax_world_probe", "CIRAM API bounds, isolation and exact machine restoration", probe);
}
#else
void fax_world_tcp_setup(void) {}
#endif
enum { STATE_HEADER = 32 };
static int state_get(uint8_t *buf, int cap) {
    if (!enabled) return 0;
    int len = STATE_HEADER + (int)sizeof rooms;
    if (len > cap) return -1;
    memset(buf, 0, STATE_HEADER);
    memcpy(buf, "FAXWORLD", 8);
    buf[8] = 6;
    buf[9] = (uint8_t)(area + 1);
    buf[10] = (uint8_t)(active + 1);
    buf[18] = death_mode;
    buf[19] = script_mode;
    buf[11] = (uint8_t)(pending + 1);
    buf[12] = (uint8_t)direction;
    buf[13] = player_tick;
    buf[14] = collision_active;
    buf[15] = (uint8_t)pending_y;
    buf[16] = (uint8_t)pre_x;
    buf[17] = (uint8_t)pre_y;
    memcpy(buf + 20, &ticks, 4);
    memcpy(buf + 24, &last_tick, 4);
    buf[28] = fighting;
    buf[29] = screen_loaded;
    buf[30] = arriving;
    memcpy(buf + STATE_HEADER, rooms, sizeof rooms);
    return len;
}
/* A record is checked whether Seamless is on or not: with the remaster off
 * (the original game) a state saved with it on still loads, and Seamless's
 * record is set aside (state_set), as the other features' are. Version 5
 * (#166's slice 1) kept each room's Seams block and the screen load not yet
 * acted on; version 6 (slice 5) has the block grown to 512 bytes, its cells
 * past a room's top and bottom too. Older records don't load. Byte 30 is
 * arriving (#204), 0 in a record saved before it, which loads as it was. */
static int state_validate(const uint8_t *buf, int len) {
    if (!len) return 1;
    if (!buf || len != STATE_HEADER + sizeof rooms || memcmp(buf, "FAXWORLD", 8) || buf[8] != 6 || buf[9] > 8 ||
        buf[10] > MAX_ROOMS || buf[11] > MAX_ROOMS || buf[12] > 3 || buf[13] > 1 || buf[14] > 1 || buf[18] > 1 ||
        buf[19] > 1 || buf[28] > 1 || buf[29] > 1 || buf[30] > 1)
        return 0;
    if (!buf[9] && (buf[10] || buf[11])) return 0;
    if (buf[9] && (buf[10] > counts[buf[9] - 1] || buf[11] > counts[buf[9] - 1])) return 0;
    for (unsigned i = 0; i < MAX_ROOMS; i++) {
        Room r;
        memcpy(&r, buf + STATE_HEADER + i * sizeof r, sizeof r);
        if (r.live > 1 || r.valid > 1 || r.owned > 1 || r.x < -83 || r.x > 83 || r.y < -83 || r.y > 83 ||
            !fax_seams_valid(r.seams, MAX_ROOMS))
            return 0;
        for (unsigned j = 0; j < 4; j++)
            if (r.edges[j] != 255 && r.edges[j] >= MAX_ROOMS) return 0;
    }
    return 1;
}
static int state_set(const uint8_t *buf, int len) {
    if (!state_validate(buf, len)) return 0;
    if (!len || !enabled) {
        fax_world_reset();
        return 1;
    }
    area = buf[9] - 1;
    active = buf[10] - 1;
    pending = buf[11] - 1;
    direction = buf[12];
    player_tick = buf[13] != 0;
    collision_active = buf[14] != 0;
    death_mode = buf[18] != 0;
    script_mode = buf[19] != 0;
    pending_y = buf[15];
    pre_x = buf[16];
    pre_y = buf[17];
    fighting = buf[28] != 0;
    screen_loaded = buf[29] != 0;
    arriving = buf[30] != 0;
    memcpy(&ticks, buf + 20, 4);
    memcpy(&last_tick, buf + 24, 4);
    memcpy(rooms, buf + STATE_HEADER, sizeof rooms);
    fax_world_loaded();
    return 1;
}
static int hud_get(uint8_t *buf, int cap) {
    if (!enabled || !hud_valid) return 0;
    int n = 4 + (int)sizeof hud_band;
    if (cap < n) return -1;
    memcpy(buf, "HUD1", 4);
    memcpy(buf + 4, hud_band, sizeof hud_band);
    return n;
}
static int hud_validate(const uint8_t *buf, int len) {
    return !len || (buf && len == 4 + (int)sizeof hud_band && !memcmp(buf, "HUD1", 4));
}
static int hud_set(const uint8_t *buf, int len) {
    if (!hud_validate(buf, len)) return 0;
    hud_valid = len != 0 && enabled;
    if (hud_valid) memcpy(hud_band, buf + 4, sizeof hud_band);
    return 1;
}
static void hooks(bool on) {
    const char *ids[] = { "player", "seam", "draw", "queue", "wait", "pause", "inventory", "death", "script" };
    char id[64];
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        snprintf(id, sizeof id, "faxanadu.world.%s", ids[i]);
        nes_mod_set_function_hook_enabled(id, on);
    }
}
static void deactivate(void) {
    enabled = atlas = false;
    cyc_mod_set_ram_read_hook(NULL);
    hooks(false);
}
static void activate(void) {
    enabled = true;
    hooks(true);
    nes_mod_set_local_only("Seamless world", 1);
}
NES_MOD_CONSTRUCTOR(register_world) {
    if (!nes_mod_register_function_entry_plugin("faxanadu.world.player", 0xe0ca, player_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.seam", 0xe8e9, seam_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.draw", 0xb982, draw_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.pause", 0xe02b, freeze_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.inventory", 0xe016, freeze_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.death", 0xd8ec, death_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.script", 0x8242, script_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.queue", 0xcfdc, queue_hook) ||
        !nes_mod_register_function_entry_plugin("faxanadu.world.wait", 0xcff4, wait_hook) ||
        !nes_mod_register_reset_callback(deactivate) ||
        !nes_mod_register_activation_plugin("faxanadu.seamless-world", activate) ||
        !nes_mod_register_savestate_hook("faxanadu.world", state_get, state_set) ||
        !nes_mod_register_savestate_validator("faxanadu.world", state_validate) ||
        !nes_mod_register_savestate_hook("faxanadu.world.hud", hud_get, hud_set) ||
        !nes_mod_register_savestate_validator("faxanadu.world.hud", hud_validate))
        abort();
}
