#pragma once
/* The map's layout (fax_map_layout.c): every place outside, from the cart's
 * own tables, laid out on a grid a page per region, tower or castle. It reads
 * a copy of the PRG and nothing else, so it is pure and runs once.
 *
 * Bank 3's area data gives each screen's neighbours (scroll data: left, right,
 * up, down) and each area's doors (door locations and destinations); bank 15
 * gives the walks off a region's edge into a town (AREA_TOWN_TRANSITIONS_DATA)
 * and the Trunk's climb ($0C to $16, AREA_SCREEN_COMPARATORS); bank 11 gives
 * the items lying in the world (AREA_SPRITE_ADDRESSES). A page is laid
 * out by a breadth-first walk from its first screen, scroll links first, then
 * town edges and climbs: each screen goes one cell over from the one it was
 * reached from, unless that cell is taken (the links are not always both ways
 * or Euclidean). Towns go in whole, both screens or neither. What the walk
 * cannot reach (a tower behind a door, a link it had to skip) is laid out the
 * same way on its own and set down near the screen whose door or edge leads
 * to it, a cell clear of everything else. Screens nothing leads to are left
 * off. See docs/GAME_INTERNALS.md, "The map". */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FAX_MAP_AREAS = 8,
    FAX_MAP_SCREENS = 83, /* the most an area has: the Mist */
    FAX_MAP_DOORS = 48,   /* the most door locations an area lists: the towns' 43 */
    FAX_MAP_CELLS = 96,   /* the most screens a page holds */
    FAX_MAP_TOWN_EDGES = 16,
    FAX_MAP_NONE = 0xFF, /* no link */
};

/* Area_CurrentArea */
enum {
    FAX_MAP_EOLIS,
    FAX_MAP_TRUNK,
    FAX_MAP_MIST,
    FAX_MAP_TOWNS,
    FAX_MAP_BUILDINGS,
    FAX_MAP_BRANCH,
    FAX_MAP_DARTMOOR,
    FAX_MAP_ZENIS
};
enum { FAX_MAP_LEFT, FAX_MAP_RIGHT, FAX_MAP_UP, FAX_MAP_DOWN };

/* The pages, in the game's order. */
enum {
    FAX_MAP_PAGE_EOLIS,
    FAX_MAP_PAGE_TRUNK,
    FAX_MAP_PAGE_TOWER_OF_TRUNK,
    FAX_MAP_PAGE_TOWER_OF_FORTRESS,
    FAX_MAP_PAGE_MIST,
    FAX_MAP_PAGE_TOWER_OF_SUFFER,
    FAX_MAP_PAGE_TOWER_OF_MIST,
    FAX_MAP_PAGE_BRANCH,
    FAX_MAP_PAGE_DARTMOOR,
    FAX_MAP_PAGE_CASTLE_OF_FRATERNAL,
    FAX_MAP_PAGE_FORTRESS_OF_ZENIS,
    FAX_MAP_PAGES
};

/* What a door leads to. */
typedef enum {
    FAX_MAP_DOOR_SCREEN,   /* index < $20: another screen of the same area (a tower's, a castle's) */
    FAX_MAP_DOOR_BUILDING, /* $20-$FD: a building; its palette is its kind (BUILDING_PALETTES) */
    FAX_MAP_DOOR_REGION,   /* $FE, $FF: another region */
} FaxMapDoorKind;

/* BUILDING_PALETTES: a building's kind, its door's destination palette. */
enum {
    FAX_MAP_KINGS_ROOM,
    FAX_MAP_TEMPLE,
    FAX_MAP_HOSPITAL,
    FAX_MAP_TAVERN,
    FAX_MAP_TOOL_SHOP,
    FAX_MAP_KEY_SHOP,
    FAX_MAP_HOUSE,
    FAX_MAP_MEAT_SHOP,
    FAX_MAP_MARTIAL_ARTS,
    FAX_MAP_MAGIC_SHOP,
    FAX_MAP_BUILDING_KINDS
};

typedef struct {
    uint8_t screen, block, index, start; /* <AREA>_DOOR_LOCATIONS: the door's screen and block (YX), its index,
                                              where the hero starts on the other side (YX) */
    uint8_t to, palette, key;            /* <AREA>_DOOR_DESTINATIONS[index] (index - $20 in the towns): the
                                              screen (a building's number), the palette (a building's kind), the
                                              key it takes; 0 for a region's door */
    uint8_t kind;                        /* FaxMapDoorKind */
} FaxMapDoor;

typedef struct {
    uint8_t count;                    /* screens */
    uint8_t link[FAX_MAP_SCREENS][4]; /* left, right, up, down; FAX_MAP_NONE */
    uint8_t door_count;
    FaxMapDoor door[FAX_MAP_DOORS];
} FaxMapArea;

/* A walk off a region's edge into a town and back: both ways listed, so it
 * is a real edge (the Trunk's first entry, for screen 0, has no way back). */
typedef struct {
    uint8_t area, screen, side, town;
} FaxMapTownEdge; /* the town is on `side` of the screen */

typedef struct {
    uint8_t area, screen;
    int8_t x, y;
} FaxMapCell;
typedef struct {
    const char *name; /* the zone's, as fax_zone.c names it */
    uint8_t area;     /* the area of its own screens; its towns are in AREA_TOWNS */
    uint8_t count;
    uint8_t w, h; /* cells span 0..w-1, 0..h-1 */
    FaxMapCell cell[FAX_MAP_CELLS];
} FaxMapPage;

/* The world's pickups: the item sprites in each screen's sprite list (bank
 * 11's AREA_SPRITE_ADDRESSES), and in each building's that a door leads to.
 * How the game decides one is there is its entity's behavior (bank 14's
 * BSCRIPTS_OBJ_*), so its kind is the entity's. Poison ($4C, $5E) is no item:
 * it hurts. See docs/GAME_INTERNALS.md, "The map". */
enum { FAX_MAP_ITEMS = 64 };
typedef enum {
    FAX_MAP_ITEM_ALWAYS,    /* lies there at every visit, and the game keeps no state (the Elixir, a Red
                                 Potion; a Glove or an Ointment while none is running) */
    FAX_MAP_ITEM_SOMETIMES, /* one counted visit in four (Screen_Is3Of4Visits) */
    FAX_MAP_ITEM_ONCE,      /* until the game's own flag or list says the hero has it */
} FaxMapItemKind;
typedef struct {
    uint8_t area, screen;         /* its sprite list's: in AREA_BUILDINGS, the building's number */
    uint8_t entity, block;        /* its sprite entity, and block YX */
    uint8_t kind;                 /* FaxMapItemKind */
    uint8_t map_area, map_screen; /* the place the map marks it on: its own, or a building's door's */
    uint8_t map_block;            /* and where on it: its block, or the door's */
} FaxMapItem;
/* An item sprite's kind, or -1 for an entity that is no item. */
int fax_map_item_kind(unsigned entity);

typedef struct {
    FaxMapArea area[FAX_MAP_AREAS];
    uint8_t edge_count;
    FaxMapTownEdge edge[FAX_MAP_TOWN_EDGES];
    uint8_t climb[FAX_MAP_SCREENS][2]; /* the Trunk's climb: up, down from a screen; FAX_MAP_NONE */
    FaxMapPage page[FAX_MAP_PAGES];
    uint8_t item_count;
    FaxMapItem item[FAX_MAP_ITEMS]; /* in the lists' order: by area, then screen (a building by its
                                               number), then place in the list. The file names them by it. */
} FaxMapAtlas;

/* Decode and lay out the map from 16 banks of PRG (no header). False if the
 * tables are not where and how the original Faxanadu (USA) has them. */
bool fax_map_decode(FaxMapAtlas *atlas, const uint8_t *prg, size_t size);

/* The page a place outside is on (a town is on its region's), or -1. */
int fax_map_page_of(const FaxMapAtlas *atlas, unsigned area, unsigned screen);
/* A place's cell on its page, or NULL if it is not on the map. */
const FaxMapCell *fax_map_cell(const FaxMapAtlas *atlas, unsigned area, unsigned screen);
/* Whether two places are joined by a link both ways, a town edge or the
 * climb, in direction `dir` from the first. */
bool fax_map_joined(const FaxMapAtlas *atlas, unsigned area, unsigned screen, unsigned dir, unsigned *to_area,
                    unsigned *to_screen);
