/* Unit tests for the map (fax_map_layout.c, fax_map.c), without the emulator.
 *
 * The layout is decoded from a cart of our own: bank 3's area tables, bank
 * 15's town edges and climb and bank 11's sprite lists, laid out as the
 * original has them, holding a small world that has each case the layout and
 * the items handle. The feature runs against
 * Block's model of the machine (../block/model.c): RAM as the game leaves it
 * at the end of a frame, GameLoop_CheckPauseGame's two branches, the frames'
 * ends, Save files' fields and the presented picture.
 *
 * With the real ROM's path as the argument, it also checks the real map: the
 * tables decode, every page fits the map's grid at 10 x 8 pixels a screen or
 * more, no two screens share a cell, every screen anything leads to is on a
 * page, and the places the doc names are where it says. */
#define FAX_MAP_LOG(...) ((void)0)
#include "fax_map.c"
#include "fax_map_layout.c"
#include "model.h"

/* ---- what fax_map.c uses besides the machine ---- */

uint32_t hw_palette_argb[512];
static uint32_t native[256 * 240];
const uint32_t *cyc_render_present(int *width, int *height) { *width = 256; *height = 240; return native; }
bool fax_text_load(void) { return true; }
bool fax_text_loaded(void) { return true; }
int fax_text_width(const char *s) { return (int)strlen(s) * FAX_TEXT_CELL; }
static unsigned boxes_drawn, lines_drawn;
static char last_line[64];
void fax_text_box(uint32_t *px, int w, int h, int x, int y, int cols, int rows, unsigned alpha) {
    (void)px; (void)w; (void)h; (void)x; (void)y; (void)cols; (void)rows; (void)alpha; ++boxes_drawn;
}
void fax_text_draw(uint32_t *px, int w, int h, int x, int y, const char *s, unsigned alpha) {
    (void)px; (void)w; (void)h; (void)x; (void)y; (void)alpha; ++lines_drawn;
    snprintf(last_line, sizeof last_line, "%s", s);
}
static uint8_t gurus = 1;   /* Guru travel's visited Gurus: Eolis's from the start */
uint8_t fax_travel_visited(void) { return gurus; }
bool fax_quests_showing(void) { return false; }   /* the quest log, closed */

/* Save files: the file in play's fields, a small key-value list. */
static FaxSaveListener save_listener;
static struct { char key[32], value[96]; } fields[16];
static unsigned field_count;
static bool file_in_play = true;
bool fax_save_listen(FaxSaveListener listener) { save_listener = listener; return true; }
static int field_at(const char *key) {
    for (unsigned i = 0; i < field_count; ++i) if (!strcmp(fields[i].key, key)) return (int)i;
    return -1;
}
const char *fax_save_get(const char *key) { int i = field_at(key); return file_in_play && i >= 0 ? fields[i].value : NULL; }
bool fax_save_set(const char *key, const char *value) {
    if (!file_in_play) return false;
    int i = field_at(key);
    if (!value) { if (i >= 0) fields[i] = fields[--field_count]; return true; }
    CHECK(strlen(value) < 96 && fax_save_value_ok(value));
    if (i < 0) { CHECK(field_count < 16); i = (int)field_count++; snprintf(fields[i].key, 32, "%s", key); }
    snprintf(fields[i].value, 96, "%s", value);
    return true;
}
bool fax_save_value_ok(const char *value) {
    for (const char *c = value; *c; ++c) if (*c < 0x20 || *c > 0x7E) return false;
    return strlen(value) <= 95;
}

/* ---- the cart ---- */

static uint8_t payload[16 * 0x4000];
static size_t payload_size = sizeof payload;
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= payload_size) memcpy(out, payload, payload_size);
    return payload_size;
}

enum { L = FAX_MAP_LEFT, R = FAX_MAP_RIGHT, U = FAX_MAP_UP, D = FAX_MAP_DOWN };
static const unsigned counts[8] = { 9, 64, 83, 14, 10, 40, 32, 20 };
static uint8_t links[8][83][4];
static struct { uint8_t screen, block, index, start, to, palette, key; } doors[8][48];
static unsigned door_counts[8];
static struct { uint8_t from, area, to, start, palette; } edges[8][8];
static unsigned edge_counts[8];
typedef struct { uint8_t from, to, start; } Climb;
static Climb climbs[4];
static unsigned climb_count;
typedef struct { uint8_t area, screen, entity, block; } Sprite;
static Sprite sprites[32];
static unsigned sprite_count;
static void sprite(unsigned a, unsigned screen, unsigned entity, unsigned block) {
    sprites[sprite_count++] = (Sprite){ (uint8_t)a, (uint8_t)screen, (uint8_t)entity, (uint8_t)block };
}

static void link1(unsigned a, unsigned s, unsigned d, unsigned t) { links[a][s][d] = (uint8_t)t; }
static void link2(unsigned a, unsigned s, unsigned d, unsigned t) {
    static const unsigned back[4] = { R, L, D, U };
    links[a][s][d] = (uint8_t)t; links[a][t][back[d]] = (uint8_t)s;
}
static void door(unsigned a, unsigned screen, unsigned block, unsigned index, unsigned start, unsigned to,
                 unsigned palette) {
    unsigned i = door_counts[a]++;
    doors[a][i].screen = (uint8_t)screen; doors[a][i].block = (uint8_t)block; doors[a][i].index = (uint8_t)index;
    doors[a][i].start = (uint8_t)start; doors[a][i].to = (uint8_t)to; doors[a][i].palette = (uint8_t)palette;
}
static void edge(unsigned a, unsigned from, unsigned area, unsigned to, unsigned start) {
    unsigned i = edge_counts[a]++;
    edges[a][i].from = (uint8_t)from; edges[a][i].area = (uint8_t)area; edges[a][i].to = (uint8_t)to;
    edges[a][i].start = (uint8_t)start; edges[a][i].palette = 0x1B;
}
static uint8_t *b3(unsigned addr) { return payload + 3 * 0x4000 + (addr - 0x8000); }
static uint8_t *b15(unsigned addr) { return payload + 15 * 0x4000 + (addr - 0xC000); }
static uint8_t *b11(unsigned addr) { return payload + 11 * 0x4000 + (addr - 0x8000); }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* Bank 3 as the original lays it out: AREAS_TABLE_PTR, AREA_TABLES, then per
 * area its five offsets, its scroll data and right after it its door
 * locations, then its destinations. Bank 15's two tables at their addresses. */
static void build(void) {
    memset(payload, 0, sizeof payload);
    unsigned at = 0x8010;
    put16(b3(0x8000), at - 0x8000);
    unsigned tables = at;
    at += 16;
    for (unsigned a = 0; a < 8; ++a) {
        unsigned data = at;
        put16(b3(tables + 2 * a), data - 0x8000);
        at += 10;
        unsigned scroll = at, locations = scroll + 4 * counts[a];
        unsigned dests = locations + 4 * door_counts[a] + 1;
        put16(b3(data + 4), scroll - 0x8000); put16(b3(data + 6), locations - 0x8000); put16(b3(data + 8), dests - 0x8000);
        for (unsigned s = 0; s < counts[a]; ++s) memcpy(b3(scroll + 4 * s), links[a][s], 4);
        for (unsigned i = 0; i < door_counts[a]; ++i) {
            uint8_t *p = b3(locations + 4 * i);
            p[0] = doors[a][i].screen; p[1] = doors[a][i].block; p[2] = doors[a][i].index; p[3] = doors[a][i].start;
            if (doors[a][i].index < 0xFE) {
                unsigned k = a == FAX_MAP_TOWNS ? doors[a][i].index - 0x20u : doors[a][i].index;
                uint8_t *dst = b3(dests + 4 * k);
                dst[0] = doors[a][i].to; dst[1] = doors[a][i].palette; dst[2] = 0;
            }
        }
        *b3(locations + 4 * door_counts[a]) = 0xFF;
        at = dests + 4 * 0x40;
    }
    unsigned list = 0xF000;
    for (unsigned a = 0; a < 8; ++a) {
        put16(b15(0xEA9C + 2 * a), list);
        for (unsigned i = 0; i < edge_counts[a]; ++i, list += 5) {
            uint8_t *p = b15(list);
            p[0] = edges[a][i].from; p[1] = edges[a][i].area; p[2] = edges[a][i].to; p[3] = edges[a][i].start;
            p[4] = edges[a][i].palette;
        }
        *b15(list++) = 0xFF;
    }
    for (unsigned a = 0; a < 8; ++a) put16(b15(0xEA37 + 2 * a), 0xF400);
    put16(b15(0xEA37 + 2 * FAX_MAP_TRUNK), 0xF410);
    *b15(0xF400) = 0xFF;
    for (unsigned i = 0; i < climb_count; ++i) {
        uint8_t *p = b15(0xF410 + 4 * i);
        p[0] = climbs[i].from; p[1] = climbs[i].to; p[2] = climbs[i].start; p[3] = 0x06;
    }
    *b15(0xF410 + 4 * climb_count) = 0xFF;
    /* Bank 11: AREA_SPRITE_ADDRESSES, a word a screen (a building's by its number), and each screen's list:
     * entity and block a sprite, a $FF, then the values' $FF. The Fortress of Zenis loads nothing: its first
     * screen's pointer has $FF for its high byte. */
    unsigned at11 = 0x8220;
    for (unsigned a = 0; a < 8; ++a) {
        unsigned n = a == FAX_MAP_BUILDINGS ? 0x40 : counts[a], table = at11;
        put16(b11(0x8210 + 2 * a), table);
        at11 += 2 * n;
        if (a == FAX_MAP_ZENIS) { put16(b11(table), 0xFF00); continue; }
        for (unsigned screen = 0; screen < n; ++screen) {
            put16(b11(table + 2 * screen), at11);
            for (unsigned i = 0; i < sprite_count; ++i)
                if (sprites[i].area == a && sprites[i].screen == screen) {
                    *b11(at11++) = sprites[i].entity; *b11(at11++) = sprites[i].block;
                }
            *b11(at11++) = 0xFF; *b11(at11++) = 0xFF;
        }
    }
}

/* The world. Each case has its own screens, with the zones fax_zone.c gives them. */
enum { TOOL_SHOP_05 = 0x05, APOLUNE_TEMPLE = 0x0B };
static void world(void) {
    memset(links, 0xFF, sizeof links);
    memset(door_counts, 0, sizeof door_counts); memset(edge_counts, 0, sizeof edge_counts); climb_count = 0;
    /* Eolis: 03 02 01 left to right; the gate (screen 0) has a door to 01 and nothing leads to it; 04-08
     * lead nowhere and nothing leads to them. */
    link2(FAX_MAP_EOLIS, 1, L, 2); link2(FAX_MAP_EOLIS, 2, L, 3);
    door(FAX_MAP_EOLIS, 0, 0x9C, 0x00, 0x99, 1, 0);
    door(FAX_MAP_EOLIS, 2, 0x95, 0x22, 0x00, 0x02, FAX_MAP_TEMPLE);   /* Eolis's temple */
    /* The Trunk: 00 01, Apolune (towns 00 01) off 01's right edge and back to 08's left, 08 09; 09 climbs
     * to 16, beside 17. A door on 00 to the tower (0D 0E) and back, and a tool shop; a door on 08 to the
     * spring (3E 3F) in the same zone. 01 links up to 20, one way only. 21 links to 01, one way, and
     * nothing leads to it. 09 has a door to another region. */
    link2(FAX_MAP_TRUNK, 0x00, R, 0x01); link2(FAX_MAP_TRUNK, 0x08, R, 0x09); link2(FAX_MAP_TRUNK, 0x16, R, 0x17);
    link2(FAX_MAP_TRUNK, 0x0D, R, 0x0E); link2(FAX_MAP_TRUNK, 0x3E, R, 0x3F);
    link1(FAX_MAP_TRUNK, 0x01, U, 0x20); link1(FAX_MAP_TRUNK, 0x21, L, 0x01);
    door(FAX_MAP_TRUNK, 0x00, 0xA9, 0x00, 0x91, 0x0D, 0x07);
    door(FAX_MAP_TRUNK, 0x00, 0x95, 0x20, 0x9E, TOOL_SHOP_05, FAX_MAP_TOOL_SHOP);
    door(FAX_MAP_TRUNK, 0x0D, 0x90, 0x01, 0xA9, 0x00, 0x06);
    door(FAX_MAP_TRUNK, 0x08, 0x9D, 0x02, 0x92, 0x3E, 0x07);
    door(FAX_MAP_TRUNK, 0x09, 0x9C, 0xFF, 0x9E, 0, 0);
    climbs[climb_count++] = (Climb){ 0x09, 0x16, 0xB3 };   /* starts low on 16: it is above */
    climbs[climb_count++] = (Climb){ 0x16, 0x09, 0x2D };
    link2(FAX_MAP_TOWNS, 0x00, R, 0x01);
    edge(FAX_MAP_TRUNK, 0x00, FAX_MAP_TOWNS, 0x03, 0x92);   /* the original's dead first entry: no way back */
    edge(FAX_MAP_TRUNK, 0x01, FAX_MAP_TOWNS, 0x00, 0x92);   /* starts at the town's left: it is to the right */
    edge(FAX_MAP_TRUNK, 0x08, FAX_MAP_TOWNS, 0x01, 0x9E);
    edge(FAX_MAP_TOWNS, 0x00, FAX_MAP_TRUNK, 0x01, 0x7E);
    edge(FAX_MAP_TOWNS, 0x01, FAX_MAP_TRUNK, 0x08, 0x71);
    door(FAX_MAP_TOWNS, 0x01, 0x95, 0x23, 0x00, APOLUNE_TEMPLE, FAX_MAP_TEMPLE);
    /* The Mist: 00 01 02 in a row, 01 up to 03, 03 06 07 09 along the top and 09 down to 0A, which takes
     * the second cell Mascon (towns 04 05) would need off 02's right edge: the town goes elsewhere whole.
     * Its other side leads to 0B. 00 down to the loop 0C 0D 0E 0F, whose 0F up to 10 lands on 0C's cell. */
    link2(FAX_MAP_MIST, 0x00, R, 0x01); link2(FAX_MAP_MIST, 0x01, R, 0x02); link2(FAX_MAP_MIST, 0x01, U, 0x03);
    link2(FAX_MAP_MIST, 0x03, R, 0x06); link2(FAX_MAP_MIST, 0x06, R, 0x07); link2(FAX_MAP_MIST, 0x07, R, 0x09);
    link2(FAX_MAP_MIST, 0x09, D, 0x0A);
    link2(FAX_MAP_MIST, 0x00, D, 0x0C); link2(FAX_MAP_MIST, 0x0C, R, 0x0D); link2(FAX_MAP_MIST, 0x0D, D, 0x0E);
    link2(FAX_MAP_MIST, 0x0E, L, 0x0F); link2(FAX_MAP_MIST, 0x0F, U, 0x10);
    door(FAX_MAP_MIST, 0x00, 0x9A, 0xFE, 0x11, 0, 0);
    link2(FAX_MAP_TOWNS, 0x04, R, 0x05);
    edge(FAX_MAP_MIST, 0x02, FAX_MAP_TOWNS, 0x04, 0x91);
    edge(FAX_MAP_MIST, 0x0B, FAX_MAP_TOWNS, 0x05, 0x9E);
    edge(FAX_MAP_TOWNS, 0x04, FAX_MAP_MIST, 0x02, 0x7E);
    edge(FAX_MAP_TOWNS, 0x05, FAX_MAP_MIST, 0x0B, 0x71);
    /* The items: one of each kind, a building's, and sprites that are none (a Zozura, Poison). */
    sprite_count = 0;
    sprite(FAX_MAP_TRUNK, 0x00, 0x47, 0xA9);           /* a Zozura */
    sprite(FAX_MAP_TRUNK, 0x00, 0x4D, 0x3B);           /* item 0: an Elixir, at every visit */
    sprite(FAX_MAP_TRUNK, 0x01, 0x55, 0x41);           /* item 1: Wing Boots, one visit in four */
    sprite(FAX_MAP_TRUNK, 0x08, 0x4C, 0x31);           /* Poison */
    sprite(FAX_MAP_TRUNK, 0x08, 0x4A, 0x9E);           /* item 2: the Pendant, until SpecialItems has it */
    sprite(FAX_MAP_TRUNK, 0x09, 0x48, 0x52);           /* item 3: a Glove, unless one is running */
    sprite(FAX_MAP_MIST, 0x00, 0x58, 0xBD);            /* item 4: the Battle Suit, until he holds it */
    sprite(FAX_MAP_BUILDINGS, TOOL_SHOP_05, 0x5D, 0x98);   /* item 5: a Red Potion in the shop on Trunk 00 */
    sprite(FAX_MAP_BUILDINGS, 0x30, 0x4D, 0x98);       /* in a building no door leads to: none */
    sprite(FAX_MAP_ZENIS, 0x00, 0x4D, 0x98);           /* in an area that loads nothing: none */
    build();
}

static FaxMapAtlas got;
static const FaxMapCell *cell(unsigned area, unsigned screen) { return fax_map_cell(&got, area, screen); }
static bool at(unsigned area, unsigned screen, int x, int y) {
    const FaxMapCell *c = cell(area, screen);
    return c && c->x == x && c->y == y;
}
static bool beside(unsigned a1, unsigned s1, int dx, int dy, unsigned a2, unsigned s2) {
    const FaxMapCell *c1 = cell(a1, s1), *c2 = cell(a2, s2);
    return c1 && c2 && c2->x - c1->x == dx && c2->y - c1->y == dy;
}
/* A component set down apart: no cell of `group` within one cell of any other placed cell. */
static bool apart(unsigned page, const uint8_t (*group)[2], unsigned n) {
    const FaxMapPage *p = &got.page[page];
    for (unsigned i = 0; i < n; ++i) {
        const FaxMapCell *c = cell(group[i][0], group[i][1]);
        if (!c) return false;
        for (unsigned j = 0; j < p->count; ++j) {
            bool member = false;
            for (unsigned k = 0; k < n; ++k)
                member |= p->cell[j].area == group[k][0] && p->cell[j].screen == group[k][1];
            if (!member && abs(p->cell[j].x - c->x) <= 1 && abs(p->cell[j].y - c->y) <= 1) return false;
        }
    }
    return true;
}
static bool distinct(const FaxMapPage *p) {
    for (unsigned i = 0; i < p->count; ++i)
        for (unsigned j = i + 1; j < p->count; ++j)
            if (p->cell[i].x == p->cell[j].x && p->cell[i].y == p->cell[j].y) return false;
    for (unsigned i = 0; i < p->count; ++i)
        if (p->cell[i].x < 0 || p->cell[i].y < 0 || p->cell[i].x >= p->w || p->cell[i].y >= p->h) return false;
    return true;
}

static void test_layout(void) {
    world();
    CHECK(fax_map_decode(&got, payload, sizeof payload));
    for (unsigned p = 0; p < FAX_MAP_PAGES; ++p) CHECK(distinct(&got.page[p]));
    CHECK(!strcmp(got.page[FAX_MAP_PAGE_TRUNK].name, "Trunk") && !strcmp(got.page[FAX_MAP_PAGE_MIST].name, "Mist"));
    CHECK(!strcmp(got.page[FAX_MAP_PAGE_TOWER_OF_TRUNK].name, "Tower of Trunk"));

    /* Eolis: the row in the links' order. The gate, which nothing leads to but where a game starts, is
     * the first screen, so the row is set down near it: below it, a cell clear, the screen its door leads
     * to under it. Screens nothing leads to are left off. */
    const FaxMapPage *eolis = &got.page[FAX_MAP_PAGE_EOLIS];
    CHECK(eolis->count == 4 && eolis->w == 3 && eolis->h == 3);
    CHECK(at(0, 3, 0, 2) && at(0, 2, 1, 2) && at(0, 1, 2, 2) && at(0, 0, 2, 0));
    for (unsigned s = 4; s < 9; ++s) CHECK(!cell(0, s) && fax_map_page_of(&got, 0, s) == FAX_MAP_PAGE_EOLIS);

    /* The Trunk: Apolune whole between 01 and 08, the climb up from 09, the spring below the door to it,
     * the one-way link's screen set down apart, not above 01; 21 left off. */
    CHECK(got.edge_count == 4);   /* the dead entry is not an edge */
    CHECK(beside(1, 0x00, 1, 0, 1, 0x01) && beside(1, 0x01, 1, 0, 3, 0x00) && beside(3, 0x00, 1, 0, 3, 0x01));
    CHECK(beside(3, 0x01, 1, 0, 1, 0x08) && beside(1, 0x08, 1, 0, 1, 0x09));
    CHECK(beside(1, 0x09, 0, -1, 1, 0x16) && beside(1, 0x16, 1, 0, 1, 0x17));
    CHECK(beside(1, 0x08, 0, 2, 1, 0x3E) && beside(1, 0x3E, 1, 0, 1, 0x3F));
    CHECK(apart(FAX_MAP_PAGE_TRUNK, (const uint8_t[][2]){ { 1, 0x3E }, { 1, 0x3F } }, 2));
    CHECK(cell(1, 0x20) && !beside(1, 0x01, 0, -1, 1, 0x20) && apart(FAX_MAP_PAGE_TRUNK, (const uint8_t[][2]){ { 1, 0x20 } }, 1));
    CHECK(beside(1, 0x01, 0, 2, 1, 0x20));
    CHECK(!cell(1, 0x21));
    CHECK(fax_map_page_of(&got, FAX_MAP_TOWNS, 0x00) == FAX_MAP_PAGE_TRUNK);
    CHECK(fax_map_page_of(&got, FAX_MAP_TOWNS, 0x01) == FAX_MAP_PAGE_TRUNK);
    CHECK(got.page[FAX_MAP_PAGE_TRUNK].count == 11);
    unsigned ta, ts;
    CHECK(fax_map_joined(&got, 1, 0x09, U, &ta, &ts) && ta == 1 && ts == 0x16);
    CHECK(fax_map_joined(&got, 1, 0x16, D, &ta, &ts) && ta == 1 && ts == 0x09);
    CHECK(fax_map_joined(&got, 3, 0x00, L, &ta, &ts) && ta == 1 && ts == 0x01);
    CHECK(!fax_map_joined(&got, 1, 0x01, U, &ta, &ts));   /* one way */

    /* The tower is its own page, behind the door. */
    const FaxMapPage *tower = &got.page[FAX_MAP_PAGE_TOWER_OF_TRUNK];
    CHECK(tower->count == 2 && at(1, 0x0D, 0, 0) && at(1, 0x0E, 1, 0));
    CHECK(fax_map_page_of(&got, 1, 0x0D) == FAX_MAP_PAGE_TOWER_OF_TRUNK);

    /* The Mist: Mascon does not fit off 02 (0A has its second cell), so it goes in whole with 0B,
     * a cell clear of the rest; 10 would land on 0C, so it is set down apart. */
    CHECK(at(2, 0x00, 0, 1) && beside(2, 0x00, 1, 0, 2, 0x01) && beside(2, 0x01, 1, 0, 2, 0x02));
    CHECK(beside(2, 0x02, 2, 0, 2, 0x0A));
    CHECK(beside(3, 0x04, 1, 0, 3, 0x05) && beside(3, 0x05, 1, 0, 2, 0x0B));
    CHECK(apart(FAX_MAP_PAGE_MIST, (const uint8_t[][2]){ { 3, 4 }, { 3, 5 }, { 2, 0x0B } }, 3));
    CHECK(beside(2, 0x00, 0, 1, 2, 0x0C) && beside(2, 0x0C, 1, 0, 2, 0x0D) && !beside(2, 0x0F, 0, -1, 2, 0x10));
    CHECK(beside(2, 0x0D, 0, 1, 2, 0x0E) && beside(2, 0x0E, -1, 0, 2, 0x0F));
    CHECK(cell(2, 0x10) && apart(FAX_MAP_PAGE_MIST, (const uint8_t[][2]){ { 2, 0x10 } }, 1));
    CHECK(fax_map_page_of(&got, FAX_MAP_TOWNS, 0x05) == FAX_MAP_PAGE_MIST);

    /* Empty pages, buildings, nonsense. */
    CHECK(got.page[FAX_MAP_PAGE_BRANCH].count == 0 && got.page[FAX_MAP_PAGE_BRANCH].w == 0);
    CHECK(fax_map_page_of(&got, FAX_MAP_BUILDINGS, 2) == -1 && fax_map_page_of(&got, 9, 0) == -1);
    CHECK(fax_map_page_of(&got, FAX_MAP_TRUNK, 64) == -1);

    /* The doors as decoded: kinds, destinations, the towns' index - $20. */
    const FaxMapArea *trunk = &got.area[FAX_MAP_TRUNK];
    CHECK(trunk->door_count == 5 && trunk->door[0].kind == FAX_MAP_DOOR_SCREEN && trunk->door[0].to == 0x0D);
    CHECK(trunk->door[1].kind == FAX_MAP_DOOR_BUILDING && trunk->door[1].palette == FAX_MAP_TOOL_SHOP);
    CHECK(trunk->door[4].kind == FAX_MAP_DOOR_REGION);
    CHECK(got.area[FAX_MAP_TOWNS].door[0].to == APOLUNE_TEMPLE && got.area[FAX_MAP_TOWNS].door[0].palette == FAX_MAP_TEMPLE);

    /* The items: in the lists' order, a building's marked at its door. */
    CHECK(got.item_count == 6);
    static const uint8_t want[6][8] = {
        { 1, 0x00, 0x4D, 0x3B, FAX_MAP_ITEM_ALWAYS, 1, 0x00, 0x3B },
        { 1, 0x01, 0x55, 0x41, FAX_MAP_ITEM_SOMETIMES, 1, 0x01, 0x41 },
        { 1, 0x08, 0x4A, 0x9E, FAX_MAP_ITEM_ONCE, 1, 0x08, 0x9E },
        { 1, 0x09, 0x48, 0x52, FAX_MAP_ITEM_ALWAYS, 1, 0x09, 0x52 },
        { 2, 0x00, 0x58, 0xBD, FAX_MAP_ITEM_ONCE, 2, 0x00, 0xBD },
        { 4, TOOL_SHOP_05, 0x5D, 0x98, FAX_MAP_ITEM_SOMETIMES, 1, 0x00, 0x95 },
    };
    for (unsigned i = 0; i < 6; ++i) {
        const FaxMapItem *it = &got.item[i];
        CHECK(it->area == want[i][0] && it->screen == want[i][1] && it->entity == want[i][2] && it->block == want[i][3]);
        CHECK(it->kind == want[i][4] && it->map_area == want[i][5] && it->map_screen == want[i][6] &&
              it->map_block == want[i][7]);
    }
    CHECK(fax_map_item_kind(0x4C) < 0 && fax_map_item_kind(0x5E) < 0 && fax_map_item_kind(0x47) < 0);

    /* Tables that are not the original's are refused: a sprite list off the bank, one too long. */
    put16(b11(0x8210 + 2 * FAX_MAP_EOLIS), 0xC100);
    CHECK(!fax_map_decode(&got, payload, sizeof payload));
    world();
    memset(b11(0xA000), 0x47, 40);
    put16(b11(0x8220), 0xA000);   /* Eolis's first screen: 20 Zozuras and on */
    CHECK(!fax_map_decode(&got, payload, sizeof payload));
    world();
    /* Door locations not right after the scroll data. */
    put16(b3(0x8010 + 16 + 6), 0x1234);
    CHECK(!fax_map_decode(&got, payload, sizeof payload));
    CHECK(!fax_map_decode(&got, payload, 15 * 0x4000));
    world();
    CHECK(fax_map_decode(&got, payload, sizeof payload));
}

/* ---- the feature ---- */

#define AREA 0x24
#define SCREEN 0x63
static void settle_at(unsigned area, unsigned screen) {
    ram[SCREEN_READY_STATE] = 0; ram[INTERRUPT_HANDLERS_ON] = 1; ram[SCREEN_SCROLL_DIRECTION] = 0xFF;
    ram[AREA] = (uint8_t)area; ram[SCREEN] = (uint8_t)screen;
    fax_map_frame_end();
}
static void inside(unsigned building, unsigned from_area, unsigned from_screen) {
    ram[AREA_SAVED_AREA] = (uint8_t)from_area; ram[AREA_SAVED_SCREEN] = (uint8_t)from_screen;
    settle_at(FAX_MAP_BUILDINGS, building);
}
static bool seen(unsigned area, unsigned screen) { return bit(v.seen[area], screen); }
static bool used(unsigned area, unsigned door) { return bit(v.doors[area], door); }
static const char *field_value(const char *key) { int i = field_at(key); return i >= 0 ? fields[i].value : NULL; }

/* GameLoop_CheckPauseGame, as the game runs it: LDA Joy1_ChangedButtonMask, AND #$10, then the BEQ. */
static void and_start(void) {
    cpu.a = (uint8_t)(ram[JOY1_CHANGED] & BUTTON_START);
    cpu.p = (uint8_t)(cpu.a ? cpu.p & ~P_Z : cpu.p | P_Z);
}
/* The main loop's check: true if the game pauses (falls past the BEQ). */
static bool check(void) {
    and_start();
    CHECK(SITES(PAUSE_CHECK, "faxanadu.map.check"));
    if (cpu.p & P_Z) return false;
    ram[GAME_PAUSED_STATE] = 1;
    return true;
}
/* A frame of the pause loop: true if the game unpauses. */
static bool pause_frame(uint8_t changed) {
    ram[JOY1_CHANGED] = changed;
    and_start();
    CHECK(SITES(PAUSE_LOOP, "faxanadu.map.loop"));
    fax_map_frame_end();
    if (cpu.p & P_Z) return false;
    ram[GAME_PAUSED_STATE] = 0;
    return true;
}

static void test_visits(void) {
    world();
    model_reset();
    model_activate("faxanadu.map");
    CHECK(hook_enabled("faxanadu.map.check") && hook_enabled("faxanadu.map.loop"));
    gurus = 1;
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(decoded);
    ram[GAME_PAUSED_STATE] = 0; ram[PLAYER_IS_DEAD] = 0;

    /* A new game: the gate, then Eolis through its door. Eolis's Guru counts from the start in Guru
     * travel, but not on the map until the hero goes in. */
    settle_at(0, 0);
    CHECK(seen(0, 0) && !seen(0, 1) && !seen(0, 2));
    settle_at(0, 1);
    CHECK(seen(0, 1) && used(0, 0));   /* 00 to 01 is no link: the gate's door */
    CHECK(!used(0, 1));
    /* Not settled, or paused: nothing. */
    ram[SCREEN_SCROLL_DIRECTION] = 0; ram[SCREEN] = 2; fax_map_frame_end();
    CHECK(!seen(0, 2));
    ram[SCREEN_SCROLL_DIRECTION] = 0xFF; ram[GAME_PAUSED_STATE] = 1; fax_map_frame_end();
    CHECK(!seen(0, 2));
    ram[GAME_PAUSED_STATE] = 0;
    settle_at(0, 2);
    CHECK(seen(0, 2) && !used(0, 1));   /* walked: no door */
    inside(0x02, 0, 2);
    CHECK(used(0, 1));                  /* Eolis's temple */

    /* The Trunk: a shop, then out and on; across Apolune; the climb up and down; the tower's door both
     * ways; another region's door. */
    settle_at(1, 0);   /* (from a building: no move) */
    CHECK(seen(1, 0) && !used(1, 0) && !used(1, 4));
    inside(TOOL_SHOP_05, 1, 0);
    CHECK(used(1, 1));
    settle_at(1, 0); settle_at(1, 1); settle_at(3, 0); settle_at(3, 1); settle_at(1, 8); settle_at(1, 9);
    CHECK(seen(3, 0) && seen(3, 1) && seen(1, 8) && seen(1, 9) && !used(1, 4));
    settle_at(1, 0x16);
    CHECK(bit(v.up[1], 0x09) && !bit(v.down[1], 0x16));
    settle_at(1, 0x09);
    CHECK(bit(v.down[1], 0x16));
    settle_at(2, 0x00);                 /* through 09's door into the Mist, arriving at 00's */
    CHECK(used(1, 4) && used(2, 0));
    settle_at(2, 0x0C);
    CHECK(bit(v.down[2], 0x00) && !bit(v.up[2], 0x0C));
    settle_at(1, 0x00); settle_at(1, 0x0D);
    CHECK(used(1, 0) && used(1, 2));    /* the door in, and the one he arrives at, back */
    CHECK(!used(1, 3));

    /* A death, and zone respawn brings him back to 0C's neighbour above: no ladder for that. */
    settle_at(2, 0x0C);
    ram[PLAYER_IS_DEAD] = 1; fax_map_frame_end();
    ram[PLAYER_IS_DEAD] = 0;
    memset(v.down[2], 0, sizeof v.down[2]);
    settle_at(2, 0x00);
    CHECK(!bit(v.up[2], 0x0C) && seen(2, 0x00));
    settle_at(2, 0x0C);
    CHECK(bit(v.down[2], 0x00));
    /* A respawn at Apolune's temple: its door counts, and so does its screen. */
    CHECK(!used(3, 0));
    inside(APOLUNE_TEMPLE, 3, 1);
    CHECK(used(3, 0));
    /* Guru travel's Gurus count as their temples; Eolis's never does by itself. */
    memset(v.doors[3], 0, sizeof v.doors[3]); v.seen[3][0] = 0;
    gurus = 1 | 2; fax_map_frame_end();
    CHECK(used(3, 0) && seen(3, 1) && !seen(3, 0));
    gurus = 1;

    /* The file: one field an area, written when the file is; read back when it continues. */
    field_count = 0;
    save_listener(FAX_SAVE_WRITING);
    CHECK(!field_value("map.buildings") && !field_value("map.branch"));   /* nothing there */
    /* Eolis: screens 0-2 (9 screens: 3 digits, the first for 0-3), no ladders, both doors. */
    CHECK(field_value("map.eolis") && !strcmp(field_value("map.eolis"), "700 000 000 3"));
    /* The Trunk: screens 0, 1, 8, 9, $0D, $16; up from 9, down from $16; doors 0, 1, 2 and 4. */
    CHECK(!strcmp(field_value("map.trunk"), "3032040000000000 0020000000000000 0000040000000000 71"));
    CHECK(!strcmp(field_value("map.mist"), "100100000000000000000 000000000000000000000 "
                                           "100000000000000000000 1"));
    CHECK(!strcmp(field_value("map.towns"), "2000 0000 0000 1"));
    Visits before = v;
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!seen(1, 0) && !used(0, 0));
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!memcmp(&v, &before, sizeof v));
    /* A damaged field forgets that area alone. */
    snprintf(fields[field_at("map.trunk")].value, 96, "30x0 0 0 0");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!seen(1, 0) && seen(0, 1) && seen(2, 0x0C));
    /* Bits past the area's screens and doors are dropped. */
    snprintf(fields[field_at("map.eolis")].value, 96, "ffff 0 0 ffff");
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(v.seen[0][0] == 0xFF && v.seen[0][1] == 0x01 && v.seen[0][2] == 0 && v.doors[0][0] == 0x03);
    /* No file in play: nothing is written, and the map keeps playing. */
    file_in_play = false;
    save_listener(FAX_SAVE_WRITING);
    file_in_play = true;

    /* Save states keep it all. */
    uint8_t rec[512];
    int n = model_state_get("faxanadu.map", rec, sizeof rec);
    CHECK(n == (int)RECORD);
    Visits kept = v;
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(model_state_set("faxanadu.map", rec, n) && !memcmp(&v, &kept, sizeof v));
    rec[0] = 9;
    CHECK(!model_state_set("faxanadu.map", rec, n));
    CHECK(!model_state_set("faxanadu.map", rec, n - 1));
    CHECK(model_state_set("faxanadu.map", rec, 0) && !seen(0, 1));   /* saved without the map */
}

/* The items: where each shows, the pickups, the file. */
static void no_sprites(void) { memset(&ram[SPRITE_ENTITIES], 0xFF, SLOTS); }
/* Player_HandleTouchItem as the game runs it: the item in slot `slot`, then the slot emptied. */
static void touch(unsigned slot, unsigned entity) {
    ram[SPRITE_ENTITIES + slot] = (uint8_t)entity; cpu.x = (uint8_t)slot;
    CHECK(SITES(PLAYER_HANDLE_TOUCH_ITEM, "faxanadu.map.touch"));   /* it only observes */
    ram[SPRITE_ENTITIES + slot] = 0xFF;
}
enum { ELIXIR, BOOTS, PENDANT, GLOVE, SUIT, SHOP_POTION };

static void test_items(void) {
    world();
    model_reset();
    model_activate("faxanadu.map");
    CHECK(hook_enabled("faxanadu.map.touch"));
    gurus = 1;
    save_listener(FAX_SAVE_NEW_GAME);
    ram[GAME_PAUSED_STATE] = 0; ram[PLAYER_IS_DEAD] = 0;
    ram[DURATION_GLOVE] = ram[DURATION_OINTMENT] = 0xFF;
    ram[SPECIAL_ITEMS] = ram[QUESTS] = 0;
    ram[SELECTED_ARMOR] = 0; ram[NUMBER_OF_ARMORS] = 0; memset(&ram[ARMOR_INVENTORY], 0, 4);
    no_sprites();
    for (unsigned i = 0; i < atlas.item_count; ++i) CHECK(!item_shown(i));   /* nothing seen */

    /* On its screen an item is there if its sprite is; elsewhere, by its kind. */
    settle_at(1, 0x00);
    CHECK(!item_shown(ELIXIR));
    ram[SPRITE_ENTITIES + 2] = 0x4D;
    CHECK(item_shown(ELIXIR) && !item_shown(SHOP_POTION));   /* the shop's: one visit in four, and he is not in */
    no_sprites();
    settle_at(1, 0x01);
    CHECK(item_shown(ELIXIR));                               /* at every visit */
    CHECK(!item_shown(BOOTS));
    ram[SPRITE_ENTITIES + 5] = 0x55;
    CHECK(item_shown(BOOTS));                                /* this visit's */
    settle_at(1, 0x00);
    CHECK(!item_shown(BOOTS) && seen(1, 0x01));              /* another's: not counted on */

    /* Touched, the Elixir is taken and stays gone; the Wing Boots come back when they do. */
    touch(3, 0x4D);
    CHECK(pickups == 1 && bit(picked, ELIXIR) && !item_shown(ELIXIR));
    settle_at(1, 0x01);
    CHECK(!item_shown(ELIXIR));
    touch(5, 0x55);
    CHECK(pickups == 2 && !bit(picked, BOOTS) && !item_shown(BOOTS));
    ram[SPRITE_ENTITIES + 5] = 0x55;
    CHECK(item_shown(BOOTS));
    no_sprites();
    touch(1, 0x02);                                          /* a coin: no item of the list */
    touch(1, 0x4D);                                          /* an Elixir, but not this screen's */
    CHECK(pickups == 2);
    isolated_scope = true; settle_at(1, 0x00); touch(3, 0x4D); isolated_scope = false;
    CHECK(pickups == 2);                                     /* in an isolated machine: nothing */

    /* The Pendant: until SpecialItems has it. On its screen, its sprite (hidden till the boss is gone). */
    settle_at(1, 0x08);
    ram[SPRITE_ENTITIES + 0] = 0x4A;
    CHECK(item_shown(PENDANT));
    no_sprites();
    settle_at(1, 0x09);
    CHECK(item_shown(PENDANT) && !item_shown(GLOVE));        /* the Glove: here, and no sprite */
    ram[SPECIAL_ITEMS] = 0x02;
    CHECK(!item_shown(PENDANT));
    ram[SPECIAL_ITEMS] = 0;
    settle_at(1, 0x08);
    CHECK(item_shown(GLOVE));
    ram[DURATION_GLOVE] = 10;                                /* a Glove running: it is not there */
    CHECK(!item_shown(GLOVE));
    ram[DURATION_GLOVE] = 0xFF;
    CHECK(item_shown(GLOVE));

    /* The Battle Suit: until he wears it, or it is in his list's first entries. */
    settle_at(2, 0x00); settle_at(2, 0x01);
    CHECK(item_shown(SUIT));
    ram[SELECTED_ARMOR] = 3;
    CHECK(!item_shown(SUIT));
    ram[SELECTED_ARMOR] = 0; ram[ARMOR_INVENTORY + 1] = 3; ram[NUMBER_OF_ARMORS] = 1;
    CHECK(item_shown(SUIT));                                 /* past the count: a stale byte */
    ram[NUMBER_OF_ARMORS] = 2;
    CHECK(!item_shown(SUIT));
    ram[NUMBER_OF_ARMORS] = 0;

    /* In the shop on Trunk 00: the Red Potion while its sprite is there, at the door. */
    inside(TOOL_SHOP_05, 1, 0x00);
    CHECK(!item_shown(SHOP_POTION));
    ram[SPRITE_ENTITIES + 1] = 0x5D;
    CHECK(item_shown(SHOP_POTION));
    no_sprites();
    settle_at(1, 0x00);
    CHECK(!item_shown(SHOP_POTION));

    /* The file: the items picked, by their place in the list, a field of their own. */
    field_count = 0;
    save_listener(FAX_SAVE_WRITING);
    CHECK(field_value("map.items") && !strcmp(field_value("map.items"), "10"));
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(!bit(picked, ELIXIR));
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(bit(picked, ELIXIR) && picked[0] == 1);
    snprintf(fields[field_at("map.items")].value, 96, "fff");   /* past the items: dropped */
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(picked[0] == 0x3F && picked[1] == 0);
    snprintf(fields[field_at("map.items")].value, 96, "1z");    /* damaged: every item shows again */
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!bit(picked, ELIXIR) && seen(1, 0x01));
    save_listener(FAX_SAVE_WRITING);
    CHECK(!field_value("map.items"));                        /* none taken: no field */
    set_bit(picked, ELIXIR);
    save_listener(FAX_SAVE_CONTINUED);
    CHECK(!bit(picked, ELIXIR));                              /* a file from before the items: none picked */

    /* Save states keep what is taken; a record from before the items loads with none. */
    set_bit(picked, ELIXIR);
    uint8_t rec[512];
    int n = model_state_get("faxanadu.map", rec, sizeof rec);
    CHECK(n == (int)RECORD && n == (int)RECORD_1 + ITEM_SET);
    save_listener(FAX_SAVE_NEW_GAME);
    CHECK(model_state_set("faxanadu.map", rec, n) && bit(picked, ELIXIR));
    rec[0] = 1;
    CHECK(!model_state_set("faxanadu.map", rec, n));
    CHECK(model_state_set("faxanadu.map", rec, (int)RECORD_1) && !bit(picked, ELIXIR) && seen(1, 0x01));
    rec[0] = 2;
    CHECK(!model_state_set("faxanadu.map", rec, (int)RECORD_1));
}

/* The map open, its buttons, and the picture. */
static uint32_t px_at(const uint32_t *p, int x, int y) { return p[y * 256 + x]; }
static const Drawn *box(unsigned area, unsigned screen) { return drawn_at(area, screen); }

static void test_map(void) {
    world();
    model_reset();
    model_activate("faxanadu.map");
    for (unsigned i = 0; i < 64; ++i) hw_palette_argb[i] = 0x010000u * i + 0x0101u;
    gurus = 1;
    save_listener(FAX_SAVE_NEW_GAME);
    ram[GAME_PAUSED_STATE] = 0; ram[PLAYER_IS_DEAD] = 0;
    ram[DURATION_GLOVE] = ram[DURATION_OINTMENT] = 0xFF; ram[SPECIAL_ITEMS] = ram[QUESTS] = 0;
    no_sprites();
    settle_at(1, 0x00); settle_at(1, 0x01); settle_at(3, 0x00); settle_at(3, 0x01); settle_at(1, 0x08);
    settle_at(1, 0x09);
    settle_at(1, 0x16);   /* the climb */
    settle_at(1, 0x17);
    settle_at(0, 0x01);   /* (a door would take him here; it does not matter) */
    settle_at(1, 0x00);

    /* Not pressed: the check does what the game's does. */
    ram[JOY1_CHANGED] = 0;
    CHECK(!check() && !open);
    ram[JOY1_CHANGED] = BUTTON_START;
    CHECK(check() && !open);   /* Start pauses as ever */
    CHECK(pause_frame(BUTTON_START) && !open);

    /* Map: the next check pauses, on the page the hero is on. */
    press();
    CHECK(want && !open);
    ram[JOY1_CHANGED] = 0;
    CHECK(check() && open && shown == FAX_MAP_PAGE_TRUNK && cpu.a == BUTTON_START);
    CHECK(!pause_frame(0) && open);           /* it stays paused */
    CHECK(!pause_frame(BUTTON_LEFT | 0x80) && shown == FAX_MAP_PAGE_EOLIS && turns == 1);
    CHECK(!pause_frame(BUTTON_LEFT) && shown == FAX_MAP_PAGE_TRUNK);   /* round: only two pages seen */
    CHECK(!pause_frame(BUTTON_RIGHT) && shown == FAX_MAP_PAGE_EOLIS);
    CHECK(!pause_frame(BUTTON_RIGHT) && shown == FAX_MAP_PAGE_TRUNK);
    CHECK(pause_frame(BUTTON_B) && !open && closes == 1);   /* B closes it and unpauses */
    CHECK(!ram[GAME_PAUSED_STATE]);
    press(); CHECK(check() && open);
    CHECK(pause_frame(BUTTON_START) && !open);              /* so does Start */
    press(); CHECK(check() && open);
    press();                                                /* and Map */
    CHECK(closing && pause_frame(0) && !open);

    /* A press waits for the check a while (the menu, a conversation or a scroll may hold it), then goes. */
    press();
    for (unsigned i = 0; i < OPEN_WINDOW - 2; ++i) fax_map_frame_end();
    CHECK(want);
    for (unsigned i = 0; i < 4; ++i) fax_map_frame_end();
    CHECK(!want && !check());
    /* In the game's own pause, it opens there; closing it unpauses. */
    ram[JOY1_CHANGED] = BUTTON_START; CHECK(check() && !open);
    CHECK(!pause_frame(0));
    press();
    CHECK(!pause_frame(0) && open);
    CHECK(pause_frame(BUTTON_B) && !open);
    /* Inside an isolated machine nothing happens. */
    press(); isolated_scope = true; ram[JOY1_CHANGED] = 0;
    CHECK(!check() && !open);
    isolated_scope = false;
    CHECK(check() && open);
    /* In a building: the page of the screen he went in from. */
    CHECK(pause_frame(BUTTON_B));
    ram[AREA] = FAX_MAP_BUILDINGS; ram[SCREEN] = TOOL_SHOP_05; ram[AREA_SAVED_AREA] = 0; ram[AREA_SAVED_SCREEN] = 1;
    press(); CHECK(check() && open && shown == FAX_MAP_PAGE_EOLIS);
    CHECK(pause_frame(BUTTON_B));
    ram[AREA] = 1; ram[SCREEN] = 0;

    /* The picture, after a visit to the shop on 00. */
    inside(TOOL_SHOP_05, 1, 0); settle_at(1, 0);
    CHECK(used(1, 1));
    press(); CHECK(check() && open && shown == FAX_MAP_PAGE_TRUNK);
    age = 0;
    int w = 256, h = 240;
    const uint32_t *pic = fax_map_present(native, &w, &h);
    CHECK(pic != native && w == 256 && h == 240 && boxes_drawn > 0 && !strcmp(last_line, "Item"));
    CHECK(drawn_page == FAX_MAP_PAGE_TRUNK && drawn_cw == 16 && drawn_ch == 13);
    const Drawn *b0 = box(1, 0), *b1 = box(1, 1), *b8 = box(1, 8), *b9 = box(1, 9), *b16 = box(1, 0x16),
                *b3e = box(1, 0x3E), *t0 = box(3, 0);
    CHECK(b0 && b1 && b8 && b9 && b16 && b3e && t0 && b0->seen && !b3e->seen);
    CHECK(b0->w == 15 && b0->h == 12 && b1->x == b0->x + 16 && b1->y == b0->y);
    /* The hero's screen blinks; seen screens are walls around the inside; unseen ones are not drawn. */
    CHECK(px_at(pic, b0->x + 2, b0->y + 2) == nes(HERE) && px_at(pic, b0->x, b0->y) == nes(ROOM_WALL));
    CHECK(px_at(pic, b1->x + 5, b1->y + 5) == nes(ROOM) && px_at(pic, t0->x + 5, t0->y + 5) == nes(TOWN));
    CHECK(px_at(pic, t0->x + 5, t0->y) == nes(TOWN_WALL));
    CHECK(px_at(pic, b3e->x + 5, b3e->y + 5) == nes(BLACK) && px_at(pic, b3e->x, b3e->y) == nes(BLACK));
    /* Beside: open through both walls and the gap, but for the corners. */
    CHECK(px_at(pic, b1->x + b1->w - 1, b1->y + 5) == nes(ROOM) && px_at(pic, b1->x + b1->w, b1->y + 5) == nes(ROOM));
    CHECK(px_at(pic, t0->x, t0->y + 5) == nes(TOWN));
    CHECK(px_at(pic, b8->x + b8->w, b8->y + 5) == nes(ROOM) && px_at(pic, b9->x, b9->y + 5) == nes(ROOM));
    CHECK(px_at(pic, b8->x + b8->w, b8->y) == nes(BLACK) && px_at(pic, b8->x + b8->w - 1, b8->y) == nes(ROOM_WALL));
    /* 16 is above 09, and he climbed it: a gap in both walls. */
    CHECK(b16->x == b9->x && b16->y == b9->y - 13);
    int mid = b9->x + b9->w / 2;
    CHECK(px_at(pic, mid, b16->y + b16->h) == nes(ROOM) && px_at(pic, mid, b9->y) == nes(ROOM));
    CHECK(px_at(pic, b9->x + 1, b9->y) == nes(ROOM_WALL));
    /* The shop's door, at its block on 00 ($95: x 5 of 16, y 9 of 13): a door like any other. */
    int mx = b0->x + 1 + 5 * (b0->w - 5) / 15, my = b0->y + 1 + 9 * (b0->h - 5) / 12;
    CHECK(px_at(pic, mx, my) == nes(DOOR) && px_at(pic, mx + 2, my + 2) == nes(DOOR));
    /* The items: the Pendant's dot on 08 ($9E) and the Glove's on 09 ($52), 2 x 2; none for the Elixir on
     * 00, where he is and its sprite is not; none for the Glove while one runs. */
    int px8 = b8->x + 1 + 14 * (b8->w - 5) / 15, py8 = b8->y + 1 + 9 * (b8->h - 5) / 12;
    CHECK(px_at(pic, px8, py8) == nes(ITEM) && px_at(pic, px8 + 1, py8 + 1) == nes(ITEM));
    CHECK(px_at(pic, px8 + 2, py8) == nes(ROOM) && px_at(pic, px8, py8 + 2) == nes(ROOM));
    int px9 = b9->x + 1 + 2 * (b9->w - 5) / 15, py9 = b9->y + 1 + 5 * (b9->h - 5) / 12;
    CHECK(px_at(pic, px9, py9) == nes(ITEM));
    int ex = b0->x + 1 + 11 * (b0->w - 5) / 15, ey = b0->y + 1 + 3 * (b0->h - 5) / 12;
    CHECK(px_at(pic, ex, ey) != nes(ITEM) && dot_count == 2 && dots[0].item == PENDANT && dots[1].item == GLOVE);
    CHECK(dots[0].x == px8 && dots[0].y == py8);
    ram[SPRITE_ENTITIES + 4] = 0x4D;                         /* there after all */
    pic = fax_map_present(native, &w, &h);
    CHECK(px_at(pic, ex, ey) == nes(ITEM) && dot_count == 3);
    ram[SPRITE_ENTITIES + 4] = 0xFF; ram[DURATION_GLOVE] = 3;
    pic = fax_map_present(native, &w, &h);
    CHECK(px_at(pic, px9, py9) == nes(ROOM) && dot_count == 1);
    ram[DURATION_GLOVE] = 0xFF;
    /* It blinks: off every other BLINK frames. */
    age = BLINK;
    pic = fax_map_present(native, &w, &h);
    CHECK(px_at(pic, b0->x + 2, b0->y + 2) == nes(ROOM));
    /* A climb not taken stays shut: forget it, and the walls are whole. */
    v.up[1][1] = 0;
    pic = fax_map_present(native, &w, &h);
    CHECK(px_at(pic, mid, b9->y) == nes(ROOM_WALL) && px_at(pic, mid, b16->y + b16->h) == nes(BLACK));
    /* A wider picture (Seamless): the native map, centered, black around it. */
    static uint32_t wide[400 * 240];
    int x1 = b1->x, y1 = b1->y;
    w = 400; h = 240;
    const uint32_t *wpic = fax_map_present(wide, &w, &h);
    b1 = box(1, 1);
    CHECK(w == 400 && b1->x == x1 + 72 && b1->y == y1);
    CHECK(wpic[(b1->y + 5) * 400 + b1->x + 5] == nes(ROOM) && wpic[0] == nes(BLACK) && wpic[399] == nes(BLACK));
    CHECK(pause_frame(BUTTON_B));
    w = 256; h = 240;
    CHECK(fax_map_present(native, &w, &h) == native);   /* closed: the picture as it was */
    /* Off: nothing at all. */
    model_reset();
    CHECK(!enabled && !hook_enabled("faxanadu.map.check") && fax_map_present(native, &w, &h) == native);
}

/* ---- the real map ---- */

static void test_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    CHECK(f);
    static uint8_t image[16 + 16 * 0x4000];
    CHECK(fread(image, 1, sizeof image, f) == sizeof image);
    fclose(f);
    CHECK(fax_map_decode(&got, image + 16, 16 * 0x4000));
    bool led[FAX_MAP_AREAS][FAX_MAP_SCREENS];
    leads(&got, led);
    for (unsigned p = 0; p < FAX_MAP_PAGES; ++p) {
        const FaxMapPage *page = &got.page[p];
        CHECK(page->count > 0 && distinct(page));
        CHECK(page->w * 10 <= GRID_W && page->h * 8 <= GRID_H);   /* 10 x 8 a screen or larger */
        printf("  %-20s %2u screens, %2u x %u\n", page->name, page->count, page->w, page->h);
    }
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a)
        for (unsigned s = 0; s < got.area[a].count; ++s)
            if (led[a][s] && a != FAX_MAP_BUILDINGS) CHECK(cell(a, s));
    /* What the doc says. */
    CHECK(got.edge_count == 12);
    CHECK(beside(1, 0x07, 1, 0, 3, 0x00) && beside(3, 0x01, 1, 0, 1, 0x08));   /* Apolune */
    CHECK(beside(1, 0x0C, 0, -1, 1, 0x16));                                     /* the climb */
    CHECK(beside(1, 0x1A, 1, 0, 3, 0x02) && beside(3, 0x03, 1, 0, 1, 0x1D));   /* Forepaw */
    CHECK(fax_map_page_of(&got, 3, 0x0C) == FAX_MAP_PAGE_DARTMOOR && fax_map_page_of(&got, 3, 0x09) == FAX_MAP_PAGE_BRANCH);
    CHECK(!cell(1, 0x1B) && !cell(2, 0x0A) && !cell(2, 0x23) && !cell(6, 0x0F));   /* linked to nothing */
    CHECK(cell(0, 0) && cell(7, 0));                                            /* the gate; the last boss */
    CHECK(got.page[FAX_MAP_PAGE_TOWER_OF_FORTRESS].w == 6 && got.page[FAX_MAP_PAGE_TOWER_OF_FORTRESS].h == 4);
    unsigned temples = 0;
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a)
        for (unsigned i = 0; i < got.area[a].door_count; ++i) {
            const FaxMapDoor *d = &got.area[a].door[i];
            if (d->kind != FAX_MAP_DOOR_BUILDING || d->palette != FAX_MAP_TEMPLE) continue;
            for (unsigned t = 0; t < 8; ++t) temples += d->to == temple_interior[t];
        }
    CHECK(temples == 8);
    /* The items, as the doc's table has them: 44 (no Poison), each on a page. */
    unsigned kinds[3] = { 0 };
    for (unsigned i = 0; i < got.item_count; ++i) {
        const FaxMapItem *it = &got.item[i];
        ++kinds[it->kind];
        CHECK(cell(it->map_area, it->map_screen));
    }
    CHECK(got.item_count == 44 && kinds[FAX_MAP_ITEM_ALWAYS] == 10 && kinds[FAX_MAP_ITEM_SOMETIMES] == 26 &&
          kinds[FAX_MAP_ITEM_ONCE] == 8);
    printf("  %u items: %u at every visit, %u at one in four, %u once\n", got.item_count, kinds[0], kinds[1], kinds[2]);
    const FaxMapItem *elixir = &got.item[11], *boots = &got.item[22], *slayer = &got.item[39];
    CHECK(elixir->area == 1 && elixir->screen == 0x3C && elixir->entity == 0x4D && elixir->block == 0x32);
    CHECK(boots->area == 4 && boots->screen == 0x1F && boots->entity == 0x55 && boots->map_area == 2 &&
          boots->map_screen == 0x06 && boots->map_block == 0x9B);   /* the Wing Boots in a house off Mist $06 */
    CHECK(slayer->area == 6 && slayer->screen == 0x15 && slayer->entity == 0x5A && slayer->kind == FAX_MAP_ITEM_ONCE);
    /* Every area's field fits a save file's value with everything seen and used. */
    memset(&v, 0xFF, sizeof v);
    for (unsigned a = 0; a < FAX_MAP_AREAS; ++a) {
        char text[FAX_SAVE_VALUE];
        memcpy(&atlas, &got, sizeof atlas);
        if (field[a]) CHECK(format_area(a, text) && strlen(text) <= 95);
    }
    memset(&v, 0, sizeof v);
    char text[FAX_SAVE_VALUE];
    memset(picked, 0xFF, sizeof picked);
    put_set(text, picked, got.item_count);
    CHECK(strlen(text) == 11);   /* map.items, every item taken */
    memset(picked, 0, sizeof picked);
}

int main(int argc, char **argv) {
    test_layout();
    test_visits();
    test_items();
    test_map();
    if (argc > 1) test_rom(argv[1]);
    printf("map: %u checks passed%s\n", checks, argc > 1 ? " (with the ROM's map)" : "");
    return 0;
}
