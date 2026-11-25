#pragma once
/* The Select menu (fax_menu.c): with the remaster the menu lists QUESTS and
 * MAP above the original's six rows (#146), so every screen the remaster
 * adds is a row there as well as a button. A on QUESTS opens the quest log,
 * on MAP the map, as their buttons do; the original's rows do what they did.
 * It registers its own hooks and keeps no state; the host adds its TCP
 * command. */
#include <stdbool.h>

void fax_menu_tcp_setup(void);

/* The menu's top box, in tiles: 18 tall with its eight rows, the original's
 * 14 with the remaster off. Seamless keeps it over the world under a list. */
unsigned fax_menu_height(void);

/* The rows, top to bottom, as each is drawn: the string number in DataArray
 * (PLAYER_TOP_MENU_ITEM_STRINGS' row; 6 and 7, past the table's end, are
 * served by the feature) and its text. */
enum { FAX_MENU_ROWS = 8, FAX_MENU_STOCK_ROWS = 6, FAX_MENU_ADDED = FAX_MENU_ROWS - FAX_MENU_STOCK_ROWS };
enum { FAX_MENU_QUESTS, FAX_MENU_MAP };   /* the added rows, at the top */
extern const unsigned char fax_menu_strings[FAX_MENU_ROWS];
const char *fax_menu_text(unsigned string);   /* "QUESTS" for 6, "MAP" for 7, NULL for the table's own */
