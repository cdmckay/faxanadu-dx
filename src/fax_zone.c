/* Faxanadu's places. USA 57dd23d1, chipx86 byte-exact disassembly.
 * Each area ($24 Area_CurrentArea) is a set of screens ($63 Area_CurrentScreen)
 * linked by the scroll data in bank 3 (<AREA>_SCROLL_DATA, left/right/up/down).
 * A region's towers and castles are screens of the region's own area, reached
 * through doors that change the palette (<AREA>_DOOR_LOCATIONS, door index
 * < $20). The towns past Eolis share AREA_TOWNS, two screens each, joined to
 * the regions at their edges (AREA_TOWN_TRANSITIONS_DATA). Shops, houses,
 * temples and the King's room are AREA_BUILDINGS, where Area_CurrentScreen is
 * the building's own number. The names are the dialogue's (bank 13); places it
 * never names stay part of the region around them. Each span is listed with
 * what places it. See docs/GAME_INTERNALS.md, "Areas, screens and zones". */
#include "fax_zone.h"
#include <stddef.h>

static const FaxZone zones[FAX_ZONE_COUNT] = {
    [FAX_ZONE_EOLIS] = { "Eolis", FAX_ZONE_TOWN },                          /* $2A "the Elf town of Eolis" */
    [FAX_ZONE_TRUNK] = { "Trunk", FAX_ZONE_REGION },                        /* $59 "the spring of Trunk" */
    [FAX_ZONE_TOWER_OF_TRUNK] = { "Tower of Trunk", FAX_ZONE_TOWER },       /* $3D */
    [FAX_ZONE_TOWER_OF_FORTRESS] = { "Tower of Fortress", FAX_ZONE_TOWER }, /* $48, $50 */
    [FAX_ZONE_APOLUNE] = { "Apolune", FAX_ZONE_TOWN },                      /* $3B "the town of Apolune" */
    [FAX_ZONE_FOREPAW] = { "Forepaw", FAX_ZONE_TOWN },                      /* $4A "the town of Forepaw" */
    [FAX_ZONE_MIST] = { "Mist", FAX_ZONE_REGION },                          /* $6D, $7D "the Tower of Mist" */
    [FAX_ZONE_TOWER_OF_SUFFER] = { "Tower of Suffer", FAX_ZONE_TOWER },     /* $66, $67 */
    [FAX_ZONE_TOWER_OF_MIST] = { "Tower of Mist", FAX_ZONE_TOWER },         /* $6D, $7D */
    [FAX_ZONE_MASCON] = { "Mascon", FAX_ZONE_TOWN },                        /* $60 "the misty town of Mascon" */
    [FAX_ZONE_VICTIM] = { "Victim", FAX_ZONE_TOWN },                        /* $70 "the Town of Victim" */
    [FAX_ZONE_BRANCH] = { "Branch", FAX_ZONE_REGION },                      /* $81 "the World of Branch" */
    [FAX_ZONE_CONFLATE] = { "Conflate", FAX_ZONE_TOWN },                    /* $80 "a trading town called Conflate" */
    [FAX_ZONE_DAYBREAK] = { "Daybreak", FAX_ZONE_TOWN },                    /* $8D spells it "Daydreak" */
    [FAX_ZONE_DARTMOOR] = { "Dartmoor", FAX_ZONE_REGION },                  /* $8C "the door to Dartmoor" */
    [FAX_ZONE_DARTMOOR_TOWN] = { "Dartmoor", FAX_ZONE_TOWN },               /* $99 "the town of Dartmoor" */
    [FAX_ZONE_CASTLE_OF_FRATERNAL] = { "Castle of Fraternal", FAX_ZONE_CASTLE }, /* $99, $9D */
    [FAX_ZONE_FORTRESS_OF_ZENIS] = { "Fortress of Zenis", FAX_ZONE_CASTLE },     /* $9A "the fortress of Zenis" */
};

typedef struct {
    uint8_t area, first, last;
    int8_t zone;
} Span;

static const Span spans[] = {
    /* Screen 0 is outside the gate, where the intro plays. */
    { FAX_AREA_EOLIS, 0x01, 0x08, FAX_ZONE_EOLIS },
    /* Eolis's door leads to screen 0; Apolune is off screen 7 and 8's edges. */
    { FAX_AREA_TRUNK, 0x00, 0x0C, FAX_ZONE_TRUNK },
    /* Door on screen $0B (J key): Apolune's "turn right and go straight".
     * The Mattock and its Dwarf are on $15. */
    { FAX_AREA_TRUNK, 0x0D, 0x15, FAX_ZONE_TOWER_OF_TRUNK },
    /* $0C climbs to $16 (AREA_SCREEN_COMPARATORS); Forepaw is off $1A and
     * $1D's edges; the dried fountain is on $28, "right above this town". */
    { FAX_AREA_TRUNK, 0x16, 0x28, FAX_ZONE_TRUNK },
    /* Door on screen $28 (J key). The spring of Trunk is on $3D, the Dwarf
     * with the Wing Boots on $3B. */
    { FAX_AREA_TRUNK, 0x29, 0x3D, FAX_ZONE_TOWER_OF_FORTRESS },
    /* Door on screen $1E (Joker key): the third spring, which the game names
     * only "the first spring". */
    { FAX_AREA_TRUNK, 0x3E, 0x3F, FAX_ZONE_TRUNK },
    /* $0A, $0B, $23 and $24 are linked to nothing. The Guru outside Mascon
     * (temple 3) is on screen 6. */
    { FAX_AREA_MIST, 0x00, 0x2F, FAX_ZONE_MIST },
    /* Door on screen 0 (Q key), "above a Guru". The Wing Boots are on $32,
     * the pendant on $34. */
    { FAX_AREA_MIST, 0x30, 0x3E, FAX_ZONE_TOWER_OF_SUFFER },
    /* Door on screen $17 (K key). The meteorite, the Black Onyx that Victim's
     * house wants, is on $4C. */
    { FAX_AREA_MIST, 0x3F, 0x4C, FAX_ZONE_TOWER_OF_MIST },
    /* Two towers the game never names: doors on screens 1 and $20. */
    { FAX_AREA_MIST, 0x4D, 0x52, FAX_ZONE_MIST },
    /* AREA_TOWN_TRANSITIONS_DATA: each town's left and right screen. */
    { FAX_AREA_TOWNS, 0x00, 0x01, FAX_ZONE_APOLUNE },
    { FAX_AREA_TOWNS, 0x02, 0x03, FAX_ZONE_FOREPAW },
    { FAX_AREA_TOWNS, 0x04, 0x05, FAX_ZONE_MASCON },
    { FAX_AREA_TOWNS, 0x06, 0x07, FAX_ZONE_VICTIM },
    { FAX_AREA_TOWNS, 0x08, 0x09, FAX_ZONE_CONFLATE },
    { FAX_AREA_TOWNS, 0x0A, 0x0B, FAX_ZONE_DAYBREAK },
    { FAX_AREA_TOWNS, 0x0C, 0x0D, FAX_ZONE_DARTMOOR_TOWN },
    /* The doors inside Branch only change the palette, into the darker side
     * of the tree (PALETTE_LEFT_TRUNK); the Battle Helmet is on 7. */
    { FAX_AREA_BRANCH, 0x00, 0x27, FAX_ZONE_BRANCH },
    /* "The castle of Dartmoor is a complex maze": the town is off screen 3's
     * edge. Screen $0F is linked to nothing. */
    { FAX_AREA_DARTMOOR, 0x00, 0x0F, FAX_ZONE_DARTMOOR },
    /* Door on screen $0C. King Grieve's room, $15-$16, is inside it. */
    { FAX_AREA_DARTMOOR, 0x10, 0x1F, FAX_ZONE_CASTLE_OF_FRATERNAL },
    /* Through the door on Dartmoor's $0E (Demon's Ring). */
    { FAX_AREA_ZENIS, 0x00, 0x13, FAX_ZONE_FORTRESS_OF_ZENIS },
};

int fax_zone_at(unsigned area, unsigned screen) {
    for (size_t i = 0; i < sizeof spans / sizeof spans[0]; ++i)
        if (spans[i].area == area && screen >= spans[i].first && screen <= spans[i].last) return spans[i].zone;
    return FAX_ZONE_NONE; /* AREA_BUILDINGS, Eolis's gate, or no such screen */
}

const FaxZone *fax_zone(int zone) { return zone >= 0 && zone < FAX_ZONE_COUNT ? &zones[zone] : NULL; }
