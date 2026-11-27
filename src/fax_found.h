#pragma once
/* Found items (fax_found.c): with the remaster the Wing Boots, the Hour Glass
 * and the Elixir are upgrades found in the world, not bought (#138). The
 * shops stop selling them (Quick shops drops their lines, fax_shops.c), the
 * Wing Boots come only from Zorugeriru, and the Hour Glass is one fixed find,
 * Mist $30's, there until taken. It registers itself and keeps whether the
 * Hour Glass is taken in its save-state record and in the file in play (Save
 * files' named field found.hourglass); the host adds its TCP command. */
#include <stdbool.h>
#include <stdint.h>

void fax_found_tcp_setup(void);
/* Found items is on: Quick shops drops the items below from the tool shops'
 * buy lists, and the map shows the finds as this module leaves them. */
bool fax_found_on(void);

/* The items no tool shop sells while it is on, as full item IDs (the shops'
 * lists'): the Wing Boots ($8F), the Hour Glass ($8D) and the Elixir ($92). */
enum { FAX_FOUND_UNSOLD = 3 };
extern const uint8_t fax_found_unsold[FAX_FOUND_UNSOLD];

/* What becomes of a find that bank 11 places: a sprite `entity` on `area`'s
 * `screen` (Area_CurrentArea, Area_CurrentScreen). AS_IS: the original's (every
 * entity but these two). GONE: never there (the Wing Boots' $55 and every
 * Hour Glass $56 but one). FIXED: Mist $30's Hour Glass, there at every visit
 * until taken. Pure: it holds whether the feature is on or not. */
typedef enum { FAX_FOUND_AS_IS, FAX_FOUND_GONE, FAX_FOUND_FIXED } FaxFoundSpawn;
enum { FAX_FOUND_BOOTS = 0x55, FAX_FOUND_GLASS = 0x56 };          /* SpriteBehavior_RandomlyShowItem_55, _51 */
enum { FAX_FOUND_GLASS_AREA = 2, FAX_FOUND_GLASS_SCREEN = 0x30 }; /* the Mist: the Tower of Suffer's first room */
static inline FaxFoundSpawn fax_found_spawn(unsigned area, unsigned screen, unsigned entity) {
    if (entity == FAX_FOUND_BOOTS) return FAX_FOUND_GONE;
    if (entity != FAX_FOUND_GLASS) return FAX_FOUND_AS_IS;
    return area == FAX_FOUND_GLASS_AREA && screen == FAX_FOUND_GLASS_SCREEN ? FAX_FOUND_FIXED : FAX_FOUND_GONE;
}
/* The fixed Hour Glass lies at its spot: not taken in the file in play, and
 * the hero holds no Hour Glass (one is all he needs). */
bool fax_found_glass_there(void);
