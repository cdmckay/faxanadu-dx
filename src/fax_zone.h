#pragma once
/* Faxanadu's places (fax_zone.c): the towns, regions, towers and castles,
 * found from Area_CurrentArea ($24) and Area_CurrentScreen ($63). Zone names
 * shows a zone's name on arrival; zone respawn can use the same notion.
 * Buildings (shops, houses, temples, the King's room) belong to no zone.
 * See docs/GAME_INTERNALS.md, "Areas, screens and zones". */
#include <stdint.h>

/* Area_CurrentArea: AREA_* in chipx86's DEFS.asm. */
enum {
    FAX_AREA_EOLIS,
    FAX_AREA_TRUNK,
    FAX_AREA_MIST,
    FAX_AREA_TOWNS,
    FAX_AREA_BUILDINGS,
    FAX_AREA_BRANCH,
    FAX_AREA_DARTMOOR,
    FAX_AREA_ZENIS
};

typedef enum { FAX_ZONE_TOWN, FAX_ZONE_REGION, FAX_ZONE_TOWER, FAX_ZONE_CASTLE } FaxZoneKind;

typedef struct {
    const char *name; /* as the game's dialogue names it; at most 20 characters */
    FaxZoneKind kind;
} FaxZone;

enum {
    FAX_ZONE_NONE = -1,
    FAX_ZONE_EOLIS,
    FAX_ZONE_TRUNK,
    FAX_ZONE_TOWER_OF_TRUNK,
    FAX_ZONE_TOWER_OF_FORTRESS,
    FAX_ZONE_APOLUNE,
    FAX_ZONE_FOREPAW,
    FAX_ZONE_MIST,
    FAX_ZONE_TOWER_OF_SUFFER,
    FAX_ZONE_TOWER_OF_MIST,
    FAX_ZONE_MASCON,
    FAX_ZONE_VICTIM,
    FAX_ZONE_BRANCH,
    FAX_ZONE_CONFLATE,
    FAX_ZONE_DAYBREAK,
    FAX_ZONE_DARTMOOR,      /* the region, through the door the Ring of Dworf opens */
    FAX_ZONE_DARTMOOR_TOWN, /* the town, at its edge; the same name */
    FAX_ZONE_CASTLE_OF_FRATERNAL,
    FAX_ZONE_FORTRESS_OF_ZENIS,
    FAX_ZONE_COUNT
};

/* The zone at an area and screen, or FAX_ZONE_NONE. */
int fax_zone_at(unsigned area, unsigned screen);
/* A zone's name and kind; NULL for FAX_ZONE_NONE or any other id. */
const FaxZone *fax_zone(int zone);
