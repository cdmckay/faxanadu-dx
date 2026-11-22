/* The map's layout. USA 57dd23d1, chipx86 byte-exact disassembly.
 *
 * Bank 3 starts with AREAS_TABLE_PTR, the offset of AREA_TABLES from $8000.
 * That holds, per area, the offset of <AREA>_AREA_DATA, five more offsets:
 * block attributes, block properties, <AREA>_SCROLL_DATA (4 bytes a screen:
 * left, right, up, down, $FF none), <AREA>_DOOR_LOCATIONS (4 bytes a door to
 * a $FF: screen, block YX, index, start YX) and <AREA>_DOOR_DESTINATIONS (4
 * bytes an index: screen, palette, key, unused). Player_CheckIfDoorBlock reads
 * a door's destination at index * 4 (index - $20 in AREA_TOWNS); $FE and $FF
 * are a region's doors, which it never looks up there.
 *
 * Bank 15 has the rest: AREA_TOWN_TRANSITIONS_DATA ($EA9C), per area a
 * pointer to 5-byte entries to a $FF (screen, area, screen, start YX, palette)
 * that Player_CheckSwitchScreen_SwitchAreaHoriz takes on a town-edge block,
 * and AREA_SCREEN_COMPARATORS ($EA37), per area a pointer to 4-byte entries
 * (screen, screen, start YX, palette) it takes on a climb block: only the
 * Trunk's $0C and $16 have one. See fax_map_layout.h for how a page is laid
 * out, and docs/GAME_INTERNALS.md, "The map".
 *
 * Bank 11's AREA_SPRITE_ADDRESSES ($8210) holds a pointer per area to its
 * screens' pointers, a word a screen (a building's by its number), each to
 * the screen's sprite list as Screen_LoadSpriteInfo reads it: an entity and a
 * block YX a sprite, to a $FF (then each sprite's value, to a $FF, and maybe
 * $80 and the screen's event). Screen_LoadAllScreenInfo loads nothing for an
 * area whose first screen's pointer has $FF for its high byte. */
#include "fax_map_layout.h"
#include "fax_zone.h"
#include <stdlib.h>
#include <string.h>

enum { BANK = 0x4000, PRG_BANKS = 16 };
/* Bank 3 */
#define AREAS_TABLE_PTR 0x8000u
/* Bank 15 */
#define AREA_TOWN_TRANSITIONS_DATA 0xEA9Cu
#define AREA_SCREEN_COMPARATORS    0xEA37u
/* Bank 11 */
#define AREA_SPRITE_ADDRESSES      0x8210u
enum { LIST_MAX = 16 };   /* sprites a list may hold here: the original's longest has 5 (8 slots fill) */

/* Screens per area (the scroll data's length / 4), as the doc's table has them. */
static const uint8_t screens[FAX_MAP_AREAS] = { 9, 64, 83, 14, 10, 40, 32, 20 };

typedef struct { const uint8_t *prg; } Rom;
static unsigned rd(const Rom *r, unsigned bank, unsigned addr) { return r->prg[bank * BANK + (addr & (BANK - 1))]; }
static unsigned word(const Rom *r, unsigned bank, unsigned addr) {
    return rd(r, bank, addr) | rd(r, bank, addr + 1) << 8;
}

static bool decode_area(const Rom *r, unsigned a, FaxMapArea *out) {
    unsigned tables = 0x8000u + word(r, 3, AREAS_TABLE_PTR);
    unsigned data = 0x8000u + word(r, 3, tables + 2 * a);
    unsigned scroll = 0x8000u + word(r, 3, data + 4), doors = 0x8000u + word(r, 3, data + 6),
             dests = 0x8000u + word(r, 3, data + 8);
    if (scroll >= 0xC000u || doors >= 0xC000u || dests >= 0xC000u) return false;
    /* The door locations follow the scroll data, so its length is the screens. */
    if (doors != scroll + 4u * screens[a]) return false;
    out->count = screens[a];
    for (unsigned s = 0; s < out->count; ++s)
        for (unsigned d = 0; d < 4; ++d) out->link[s][d] = (uint8_t)rd(r, 3, scroll + 4 * s + d);
    out->door_count = 0;
    for (unsigned at = doors; rd(r, 3, at) != 0xFF; at += 4) {
        if (out->door_count == FAX_MAP_DOORS || at >= 0xC000u) return false;
        FaxMapDoor *d = &out->door[out->door_count++];
        d->screen = (uint8_t)rd(r, 3, at); d->block = (uint8_t)rd(r, 3, at + 1);
        d->index = (uint8_t)rd(r, 3, at + 2); d->start = (uint8_t)rd(r, 3, at + 3);
        if (d->screen >= out->count) return false;
        if (d->index >= 0xFE) { d->kind = FAX_MAP_DOOR_REGION; d->to = d->palette = d->key = 0; continue; }
        unsigned i = a == FAX_MAP_TOWNS ? (unsigned)d->index - 0x20u : d->index;
        if (i > 0x3F) return false;
        d->to = (uint8_t)rd(r, 3, dests + 4 * i); d->palette = (uint8_t)rd(r, 3, dests + 4 * i + 1);
        d->key = (uint8_t)rd(r, 3, dests + 4 * i + 2);
        d->kind = d->index < 0x20 ? FAX_MAP_DOOR_SCREEN : FAX_MAP_DOOR_BUILDING;
        if (d->kind == FAX_MAP_DOOR_SCREEN && d->to >= out->count) return false;
        if (d->kind == FAX_MAP_DOOR_BUILDING && d->palette >= FAX_MAP_BUILDING_KINDS) return false;
    }
    return true;
}

/* Town edges: a region's entry into a town that the town's entry leads back to. */
static void decode_edges(const Rom *r, FaxMapAtlas *atlas) {
    atlas->edge_count = 0;
    unsigned towns = word(r, 15, AREA_TOWN_TRANSITIONS_DATA + 2 * FAX_MAP_TOWNS);
    for (unsigned t = towns; rd(r, 15, t) != 0xFF; t += 5) {
        unsigned town = rd(r, 15, t), area = rd(r, 15, t + 1), screen = rd(r, 15, t + 2);
        if (area >= FAX_MAP_AREAS || area == FAX_MAP_TOWNS || area == FAX_MAP_BUILDINGS) continue;
        unsigned list = word(r, 15, AREA_TOWN_TRANSITIONS_DATA + 2 * area);
        for (unsigned e = list; rd(r, 15, e) != 0xFF; e += 5) {
            if (rd(r, 15, e) != screen || rd(r, 15, e + 1) != FAX_MAP_TOWNS || rd(r, 15, e + 2) != town) continue;
            /* The hero starts at the town's left (X < 8): the town is to the screen's right. */
            unsigned side = (rd(r, 15, e + 3) & 0x0F) < 8 ? FAX_MAP_RIGHT : FAX_MAP_LEFT;
            if (atlas->edge_count < FAX_MAP_TOWN_EDGES && screen < atlas->area[area].count &&
                town < atlas->area[FAX_MAP_TOWNS].count)
                atlas->edge[atlas->edge_count++] = (FaxMapTownEdge){ (uint8_t)area, (uint8_t)screen, (uint8_t)side,
                                                                     (uint8_t)town };
            break;
        }
    }
}

/* The Trunk's climb: AREA_SCREEN_COMPARATORS[Trunk]. Starting low on the
 * other screen (Y >= 8) means it is above. */
static void decode_climbs(const Rom *r, FaxMapAtlas *atlas) {
    memset(atlas->climb, FAX_MAP_NONE, sizeof atlas->climb);
    unsigned list = word(r, 15, AREA_SCREEN_COMPARATORS + 2 * FAX_MAP_TRUNK);
    for (unsigned e = list; rd(r, 15, e) != 0xFF; e += 4) {
        unsigned from = rd(r, 15, e), to = rd(r, 15, e + 1), start = rd(r, 15, e + 2);
        if (from >= screens[FAX_MAP_TRUNK] || to >= screens[FAX_MAP_TRUNK]) continue;
        atlas->climb[from][start >> 4 >= 8 ? 0 : 1] = (uint8_t)to;
    }
}

/* ---- items ---- */

/* By entity, its behavior's verdict (SPRITE_BSCRIPTS, bank 14). */
int fax_map_item_kind(unsigned entity) {
    switch (entity) {
    case 0x4B: case 0x4D:     /* Red Potion, Elixir: BSCRIPTS_OBJ_POTIONLIKE, SpriteBehavior_Fall */
    case 0x48: case 0x4E:     /* Glove, Ointment: SpriteBehavior_Glove/_Ointment, gone while one runs */
        return FAX_MAP_ITEM_ALWAYS;
    case 0x50: case 0x55: case 0x56: case 0x5D: case 0x5F: case 0x60:
        /* Mattock, Wing Boots, Hour Glass, Red Potion, Glove, Ointment: SpriteBehavior_RandomlyShowItem_* */
        return FAX_MAP_ITEM_SOMETIMES;
    case 0x49: case 0x4A:     /* Black Onyx, Pendant: SpecialItems $01, $02 */
    case 0x57:                /* Magical Rod: SpecialItems $04 */
    case 0x58: case 0x59: case 0x5A:   /* Battle Suit, Battle Helmet, Dragon Slayer: held (item 3 of its list) */
    case 0x5B: case 0x5C:     /* Ripasheiku's Mattock, Zorugeriru's Wing Boots: Quests $10, $08 */
        return FAX_MAP_ITEM_ONCE;
    default:                  /* not an item, or Poison ($4C, $5E) */
        return -1;
    }
}

/* The door to building `b`, or NULL: the first in the areas' order. */
static const FaxMapDoor *door_to(const FaxMapAtlas *atlas, unsigned b, unsigned *area) {
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a)
        for (unsigned i = 0; i < atlas->area[a].door_count; ++i) {
            const FaxMapDoor *d = &atlas->area[a].door[i];
            if (d->kind == FAX_MAP_DOOR_BUILDING && d->to == b) { *area = a; return d; }
        }
    return NULL;
}

/* One screen's list: its items. False if it runs off the bank or past the slots. */
static bool decode_list(const Rom *r, FaxMapAtlas *atlas, unsigned area, unsigned screen, unsigned list) {
    if (list < 0x8000u || list >= 0xC000u) return false;
    const FaxMapDoor *door = NULL;
    unsigned door_area = 0;
    for (unsigned n = 0; rd(r, 11, list + 2 * n) != 0xFF; ++n) {
        if (n == LIST_MAX || list + 2 * n + 1 >= 0xC000u) return false;
        unsigned entity = rd(r, 11, list + 2 * n), block = rd(r, 11, list + 2 * n + 1);
        int kind = fax_map_item_kind(entity);
        if (kind < 0) continue;
        FaxMapItem it = { (uint8_t)area, (uint8_t)screen, (uint8_t)entity, (uint8_t)block, (uint8_t)kind,
                          (uint8_t)area, (uint8_t)screen, (uint8_t)block };
        if (area == FAX_MAP_BUILDINGS) {   /* marked at the door that leads in */
            if (!door && !(door = door_to(atlas, screen, &door_area))) continue;
            it.map_area = (uint8_t)door_area; it.map_screen = door->screen; it.map_block = door->block;
        }
        if (atlas->item_count == FAX_MAP_ITEMS) return false;
        atlas->item[atlas->item_count++] = it;
    }
    return true;
}

static bool decode_items(const Rom *r, FaxMapAtlas *atlas) {
    atlas->item_count = 0;
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a) {
        unsigned base = word(r, 11, AREA_SPRITE_ADDRESSES + 2 * a);
        if (base < 0x8000u || base >= 0xC000u) return false;
        if (rd(r, 11, base + 1) == 0xFF) continue;   /* Screen_LoadAllScreenInfo: nothing to load */
        if (a != FAX_MAP_BUILDINGS) {
            for (unsigned s = 0; s < atlas->area[a].count; ++s)
                if (!decode_list(r, atlas, a, s, word(r, 11, base + 2 * s))) return false;
            continue;
        }
        /* The buildings any door leads to: their numbers run past the area's scroll data. */
        for (unsigned b = 0; b < 0x100; ++b) {
            unsigned area;
            if (door_to(atlas, b, &area) && !decode_list(r, atlas, a, b, word(r, 11, base + 2 * b))) return false;
        }
    }
    return true;
}

/* ---- pages ---- */

static const int8_t page_zone[FAX_MAP_PAGES] = {
    FAX_ZONE_EOLIS, FAX_ZONE_TRUNK, FAX_ZONE_TOWER_OF_TRUNK, FAX_ZONE_TOWER_OF_FORTRESS, FAX_ZONE_MIST,
    FAX_ZONE_TOWER_OF_SUFFER, FAX_ZONE_TOWER_OF_MIST, FAX_ZONE_BRANCH, FAX_ZONE_DARTMOOR,
    FAX_ZONE_CASTLE_OF_FRATERNAL, FAX_ZONE_FORTRESS_OF_ZENIS,
};
static const uint8_t page_area[FAX_MAP_PAGES] = {
    FAX_MAP_EOLIS, FAX_MAP_TRUNK, FAX_MAP_TRUNK, FAX_MAP_TRUNK, FAX_MAP_MIST, FAX_MAP_MIST, FAX_MAP_MIST,
    FAX_MAP_BRANCH, FAX_MAP_DARTMOOR, FAX_MAP_DARTMOOR, FAX_MAP_ZENIS,
};

static unsigned town_pair(const FaxMapAtlas *atlas, unsigned town) {   /* the town's other screen */
    const uint8_t *l = atlas->area[FAX_MAP_TOWNS].link[town];
    return l[FAX_MAP_LEFT] != FAX_MAP_NONE ? l[FAX_MAP_LEFT] : l[FAX_MAP_RIGHT];
}

int fax_map_page_of(const FaxMapAtlas *atlas, unsigned area, unsigned screen) {
    if (area >= FAX_MAP_AREAS || screen >= atlas->area[area].count) return -1;
    if (area == FAX_MAP_EOLIS) return FAX_MAP_PAGE_EOLIS;   /* screen 0, outside the gate, too */
    if (area == FAX_MAP_TOWNS) {
        unsigned other = town_pair(atlas, screen);
        for (unsigned e = 0; e < atlas->edge_count; ++e)
            if (atlas->edge[e].town == screen || atlas->edge[e].town == other)
                return fax_map_page_of(atlas, atlas->edge[e].area, atlas->edge[e].screen);
        return -1;
    }
    int zone = fax_zone_at(area, screen);
    for (int p = 0; p < FAX_MAP_PAGES; ++p) if (page_zone[p] == zone && page_area[p] == area) return p;
    return -1;
}

/* Something leads to it: a link from another screen, a door, a town edge, the
 * climb; or a new game starts there (Eolis screen 0, outside the gate). */
static void leads(const FaxMapAtlas *atlas, bool led[FAX_MAP_AREAS][FAX_MAP_SCREENS]) {
    memset(led, 0, sizeof(bool) * FAX_MAP_AREAS * FAX_MAP_SCREENS);
    led[FAX_MAP_EOLIS][0] = true;
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a) {
        const FaxMapArea *area = &atlas->area[a];
        for (unsigned s = 0; s < area->count; ++s)
            for (unsigned d = 0; d < 4; ++d)
                if (area->link[s][d] < area->count && area->link[s][d] != s) led[a][area->link[s][d]] = true;
        for (unsigned i = 0; i < area->door_count; ++i)
            if (area->door[i].kind == FAX_MAP_DOOR_SCREEN) led[a][area->door[i].to] = true;
    }
    for (unsigned e = 0; e < atlas->edge_count; ++e) {
        led[atlas->edge[e].area][atlas->edge[e].screen] = true;
        led[FAX_MAP_TOWNS][atlas->edge[e].town] = true;
    }
    for (unsigned s = 0; s < FAX_MAP_SCREENS; ++s)
        for (unsigned d = 0; d < 2; ++d) if (atlas->climb[s][d] != FAX_MAP_NONE) led[FAX_MAP_TRUNK][atlas->climb[s][d]] = true;
}

/* A page's places as nodes: its own area's screens 0..82, its towns' 100.. */
enum { NODE_TOWN = 100, NODES = NODE_TOWN + 16, RANGE = 64 };
static unsigned node_area(const FaxMapPage *p, unsigned n) { return n >= NODE_TOWN ? FAX_MAP_TOWNS : p->area; }
static unsigned node_screen(unsigned n) { return n >= NODE_TOWN ? n - NODE_TOWN : n; }
static unsigned node(unsigned area, unsigned screen) { return area == FAX_MAP_TOWNS ? NODE_TOWN + screen : screen; }

typedef struct {
    const FaxMapAtlas *atlas;
    FaxMapPage *page;
    bool member[NODES];       /* on this page */
    bool placed[NODES];       /* by an earlier component, or this one */
    int x[NODES], y[NODES];
    unsigned order[NODES], count;   /* this component's nodes, as placed */
} Walk;

static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
static const unsigned reverse[4] = { FAX_MAP_RIGHT, FAX_MAP_LEFT, FAX_MAP_DOWN, FAX_MAP_UP };

bool fax_map_joined(const FaxMapAtlas *atlas, unsigned area, unsigned screen, unsigned dir, unsigned *to_area,
                    unsigned *to_screen) {
    if (area >= FAX_MAP_AREAS || screen >= atlas->area[area].count || dir > 3) return false;
    const FaxMapArea *a = &atlas->area[area];
    unsigned t = a->link[screen][dir];
    if (t < a->count && a->link[t][reverse[dir]] == screen) { *to_area = area; *to_screen = t; return true; }
    for (unsigned e = 0; e < atlas->edge_count; ++e) {
        const FaxMapTownEdge *edge = &atlas->edge[e];
        if (area == edge->area && screen == edge->screen && dir == edge->side) {
            *to_area = FAX_MAP_TOWNS; *to_screen = edge->town; return true;
        }
        if (area == FAX_MAP_TOWNS && screen == edge->town && dir == reverse[edge->side]) {
            *to_area = edge->area; *to_screen = edge->screen; return true;
        }
    }
    if (area == FAX_MAP_TRUNK && (dir == FAX_MAP_UP || dir == FAX_MAP_DOWN) &&
        atlas->climb[screen][dir - FAX_MAP_UP] != FAX_MAP_NONE) {
        *to_area = area; *to_screen = atlas->climb[screen][dir - FAX_MAP_UP]; return true;
    }
    return false;
}

static bool taken(const Walk *w, int x, int y) {
    for (unsigned i = 0; i < w->count; ++i) if (w->x[w->order[i]] == x && w->y[w->order[i]] == y) return true;
    return false;
}
static void put(Walk *w, unsigned n, int x, int y, unsigned *queue, unsigned *tail, unsigned *later, unsigned *ltail) {
    w->placed[n] = true; w->x[n] = x; w->y[n] = y;
    w->order[w->count++] = n;
    queue[(*tail)++] = n; later[(*ltail)++] = n;
}

/* One component from `root`, in its own coordinates: scroll links first, then
 * town edges and the climb once nothing else is left to walk. */
static void walk(Walk *w, unsigned root) {
    unsigned queue[NODES], later[NODES], head = 0, tail = 0, lhead = 0, ltail = 0;
    w->count = 0;
    put(w, root, 0, 0, queue, &tail, later, &ltail);
    for (;;) {
        bool scroll = head < tail;
        if (!scroll && lhead == ltail) break;
        unsigned u = scroll ? queue[head++] : later[lhead++];
        unsigned ua = node_area(w->page, u), us = node_screen(u);
        for (unsigned d = 0; d < 4; ++d) {
            unsigned va, vs;
            if (!fax_map_joined(w->atlas, ua, us, d, &va, &vs)) continue;
            bool special = va != ua || w->atlas->area[ua].link[us][d] != vs;
            if (special == scroll) continue;   /* scroll links now, the rest later */
            unsigned v = node(va, vs);
            if (v >= NODES || !w->member[v] || w->placed[v]) continue;
            int x = w->x[u] + dx[d], y = w->y[u] + dy[d];
            if (taken(w, x, y)) continue;
            if (va == FAX_MAP_TOWNS && ua != FAX_MAP_TOWNS) {
                /* A town goes in whole: both its screens, side by side, or neither. */
                unsigned other = town_pair(w->atlas, vs), o = node(FAX_MAP_TOWNS, other);
                if (other >= w->atlas->area[FAX_MAP_TOWNS].count || !w->member[o] || w->placed[o]) continue;
                int ox = x + (w->atlas->area[FAX_MAP_TOWNS].link[vs][FAX_MAP_RIGHT] == other ? 1 : -1);
                if (taken(w, ox, y)) continue;
                put(w, v, x, y, queue, &tail, later, &ltail);
                put(w, o, ox, y, queue, &tail, later, &ltail);
                continue;
            }
            put(w, v, x, y, queue, &tail, later, &ltail);
        }
    }
}

/* Anything that leads between two nodes of the page: a link either way, a door, an edge, the climb. */
static bool connected(const Walk *w, unsigned u, unsigned v) {
    unsigned ua = node_area(w->page, u), us = node_screen(u), va = node_area(w->page, v), vs = node_screen(v);
    if (ua == va) {
        const FaxMapArea *a = &w->atlas->area[ua];
        for (unsigned d = 0; d < 4; ++d) if (a->link[us][d] == vs || a->link[vs][d] == us) return true;
        for (unsigned i = 0; i < a->door_count; ++i)
            if (a->door[i].kind == FAX_MAP_DOOR_SCREEN &&
                ((a->door[i].screen == us && a->door[i].to == vs) || (a->door[i].screen == vs && a->door[i].to == us)))
                return true;
    }
    for (unsigned d = 0; d < 4; ++d) {
        unsigned ta, ts;
        if (fax_map_joined(w->atlas, ua, us, d, &ta, &ts) && ta == va && ts == vs) return true;
    }
    return false;
}

/* Set a component down near what leads to it, a cell clear of every placed
 * screen: the nearest offset, then the shortest, then below, then right. */
static void settle(Walk *w, const unsigned *done, unsigned done_count) {
    int ax = 0, ay = 0, vx = 0, vy = 0;
    bool anchored = false;
    for (unsigned i = 0; i < w->count && !anchored; ++i)
        for (unsigned j = 0; j < done_count && !anchored; ++j)
            if (connected(w, done[j], w->order[i])) {
                anchored = true;
                ax = w->x[done[j]]; ay = w->y[done[j]]; vx = w->x[w->order[i]]; vy = w->y[w->order[i]];
            }
    if (!anchored) {   /* nothing on the page leads to it: to the right of it all */
        int right = 0;
        for (unsigned j = 0; j < done_count; ++j) if (w->x[done[j]] > right) right = w->x[done[j]];
        ax = right + 2; ay = 0;
    }
    int best_x = 0, best_y = 0;
    long best = -1;
    for (int r = anchored ? 1 : 0; r < RANGE && best < 0; ++r)
        for (int oy = -r; oy <= r; ++oy)
            for (int ox = -r; ox <= r; ++ox) {
                if ((abs(ox) > abs(oy) ? abs(ox) : abs(oy)) != r) continue;
                int sx = ax + ox - vx, sy = ay + oy - vy;   /* the shift that puts v at the anchor + offset */
                bool clear = true;
                for (unsigned i = 0; i < w->count && clear; ++i)
                    for (unsigned j = 0; j < done_count && clear; ++j)
                        if (abs(w->x[w->order[i]] + sx - w->x[done[j]]) <= 1 &&
                            abs(w->y[w->order[i]] + sy - w->y[done[j]]) <= 1) clear = false;
                if (!clear) continue;
                long key = (long)(abs(ox) + abs(oy)) * 64 + (oy < 0) * 32 + (oy == 0) * 16 + abs(ox) * 2 + (ox < 0);
                if (best < 0 || key < best) { best = key; best_x = sx; best_y = sy; }
            }
    for (unsigned i = 0; i < w->count; ++i) { w->x[w->order[i]] += best_x; w->y[w->order[i]] += best_y; }
}

static bool lay_out(FaxMapAtlas *atlas, unsigned p, bool led[FAX_MAP_AREAS][FAX_MAP_SCREENS]) {
    FaxMapPage *page = &atlas->page[p];
    memset(page, 0, sizeof *page);
    page->name = fax_zone(page_zone[p])->name;
    page->area = page_area[p];
    Walk *w = calloc(1, sizeof *w);
    if (!w) return false;
    w->atlas = atlas; w->page = page;
    for (unsigned s = 0; s < atlas->area[page->area].count; ++s)
        w->member[s] = led[page->area][s] && fax_map_page_of(atlas, page->area, s) == (int)p;
    for (unsigned t = 0; t < atlas->area[FAX_MAP_TOWNS].count; ++t)
        w->member[NODE_TOWN + t] = led[FAX_MAP_TOWNS][t] && fax_map_page_of(atlas, FAX_MAP_TOWNS, t) == (int)p;
    unsigned done[NODES], done_count = 0;
    for (unsigned root = 0; root < NODES; ++root) {
        if (!w->member[root] || w->placed[root]) continue;
        walk(w, root);
        if (done_count) settle(w, done, done_count);
        for (unsigned i = 0; i < w->count; ++i) done[done_count++] = w->order[i];
    }
    bool ok = done_count <= FAX_MAP_CELLS;
    if (ok && done_count) {   /* a page nothing leads to stays empty, 0 x 0 */
        int x0 = w->x[done[0]], y0 = w->y[done[0]], x1 = x0, y1 = y0;
        for (unsigned i = 0; i < done_count; ++i) {
            unsigned n = done[i];
            if (w->x[n] < x0) x0 = w->x[n];
            if (w->x[n] > x1) x1 = w->x[n];
            if (w->y[n] < y0) y0 = w->y[n];
            if (w->y[n] > y1) y1 = w->y[n];
        }
        ok = x1 - x0 < 127 && y1 - y0 < 127;
        page->w = (uint8_t)(x1 - x0 + 1); page->h = (uint8_t)(y1 - y0 + 1);
        /* Cells in the order of the area's screens, then its towns'. */
        for (unsigned n = 0; ok && n < NODES; ++n) {
            if (!w->member[n]) continue;
            page->cell[page->count++] = (FaxMapCell){ (uint8_t)node_area(page, n), (uint8_t)node_screen(n),
                                                      (int8_t)(w->x[n] - x0), (int8_t)(w->y[n] - y0) };
        }
    }
    free(w);
    return ok;
}

bool fax_map_decode(FaxMapAtlas *atlas, const uint8_t *prg, size_t size) {
    if (!atlas || !prg || size < (size_t)PRG_BANKS * BANK) return false;
    memset(atlas, 0, sizeof *atlas);
    Rom r = { prg };
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a) if (!decode_area(&r, a, &atlas->area[a])) return false;
    decode_edges(&r, atlas);
    decode_climbs(&r, atlas);
    if (!decode_items(&r, atlas)) return false;
    bool (*led)[FAX_MAP_SCREENS] = calloc(FAX_MAP_AREAS, sizeof *led);
    if (!led) return false;
    leads(atlas, led);
    bool ok = true;
    for (unsigned p = 0; p < FAX_MAP_PAGES && ok; ++p) ok = lay_out(atlas, p, led);
    free(led);
    return ok;
}

const FaxMapCell *fax_map_cell(const FaxMapAtlas *atlas, unsigned area, unsigned screen) {
    int p = fax_map_page_of(atlas, area, screen);
    if (p < 0) return NULL;
    const FaxMapPage *page = &atlas->page[p];
    for (unsigned i = 0; i < page->count; ++i)
        if (page->cell[i].area == area && page->cell[i].screen == screen) return &page->cell[i];
    return NULL;
}
