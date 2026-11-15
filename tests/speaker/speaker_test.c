/* Unit tests for speaker names (fax_speaker.c), without the emulator or a ROM.
 * The name lookup is checked for every script with a portrait, in the
 * building the disassembly puts it in (bank 11's sprite lists, by building)
 * with the zone at that building's door (bank 3's door tables). The tab runs
 * against a model of the machine: CPU RAM as an interaction script leaves it,
 * a nametable the text box frames are written into, each line's scroll, a
 * cart image holding glyphs, and pattern tiles and a palette for drawing. */
#include <stdio.h>
static char logged[128];
#define FAX_SPEAKER_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_zone.c"
#include "fax_text.c"
#include "fax_speaker.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static uint8_t chr[0x2000];
static uint8_t ciram[0x800];        /* two nametables, side by side (vertical mirroring) */
static int scroll_x, scroll_y;      /* the playfield's: lines show nametable Y = scroll_y + line */
static uint16_t bg_table = 0x1000;
static uint32_t native[256 * 240];
static int presents;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof cart) memcpy(out, cart, sizeof cart);
    return sizeof cart;
}
int cyc_render_line_scroll_x(int y) { (void)y; return scroll_x; }
int cyc_render_line_scroll_y(int y) { return (scroll_y + y) % 480; }
uint8_t cyc_render_nametable(uint16_t a) { return ciram[(a & 0x3FF) | (a & 0x400)]; }
uint16_t cyc_render_line_bg_table(int y) { (void)y; return bg_table; }
uint8_t cyc_render_chr(uint16_t a) { return chr[a & 0x1FFF]; }
/* Palette entry e as a color whose blue byte is e, so a pixel tells its entry. */
uint32_t cyc_render_color(int e) { return 0xFF000000u | 0x00400000u | (uint32_t)(e & 31); }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.speaker-names") && cb == activate;
}

enum { EOLIS = 0, TRUNK = 1, MIST = 2, TOWNS = 3, BUILDINGS = 4, DARTMOOR = 6 };

/* ---- the names ---- */
static const char *named(unsigned portrait, unsigned building, unsigned door_area, unsigned door_screen) {
    static char out[FAX_SPEAKER_NAME_MAX + 1];
    memset(out, '#', sizeof out);
    if (!fax_speaker_name(portrait, building, fax_zone_at(door_area, door_screen), out)) return NULL;
    return out;
}
#define NAMED(portrait, building, area, screen, name) do { \
    const char *got_ = named(portrait, building, area, screen); \
    CHECK(got_ && !strcmp(got_, name)); } while (0)

static void kings_and_gurus(void) {
    /* The King's room, behind Eolis's door on screen 7 (Ring of Elf). */
    NAMED(FAX_PORTRAIT_KING, 0x07, EOLIS, 0x07, "King of Eolis");
    /* The temple Gurus, by DEST_SCREEN_FOR_TEMPLE_SPAWN, at their doors. */
    static const struct { uint8_t building, area, screen; const char *name; } gurus[] = {
        { 0x02, EOLIS, 0x02, "Guru of Eolis" }, { 0x0B, TOWNS, 0x01, "Guru of Apolune" },
        { 0x10, TOWNS, 0x03, "Guru of Forepaw" },
        { 0x1E, MIST, 0x06, "Guru of Mascon" },        /* his door opens on the Mist */
        { 0x23, TOWNS, 0x06, "Guru of Victim" }, { 0x2B, TOWNS, 0x08, "Guru of Conflate" },
        { 0x33, TOWNS, 0x0B, "Guru of Daybreak" }, { 0x3C, TOWNS, 0x0C, "Guru of Dartmoor" },
        { 0x42, DARTMOOR, 0x1E, "Guru of Fraternal" },  /* $9D "the Guru in the Castle of Fraternal" */
    };
    for (unsigned i = 0; i < sizeof gurus / sizeof gurus[0]; ++i)
        NAMED(FAX_PORTRAIT_GURU, gurus[i].building, gurus[i].area, gurus[i].screen, gurus[i].name);
    /* A temple's Guru is its town's, wherever its door is thought to be: a
     * respawn sets the door to the temple's town, and an edited state anything. */
    NAMED(FAX_PORTRAIT_GURU, 0x1E, TOWNS, 0x04, "Guru of Mascon");
    NAMED(FAX_PORTRAIT_GURU, 0x02, TRUNK, 0x05, "Guru of Eolis");
    /* Elsewhere a Guru is the place's: a tower's or a castle's own name, a town's or region's. */
    NAMED(FAX_PORTRAIT_GURU, 0x15, TRUNK, 0x2D, "Guru of Fortress");
    NAMED(FAX_PORTRAIT_GURU, 0x50, MIST, 0x10, "Guru of Mist");
    NAMED(FAX_PORTRAIT_GURU, 0x50, 7, 0x03, "Guru of Zenis");
    /* With no place, the title alone. */
    NAMED(FAX_PORTRAIT_KING, 0x07, EOLIS, 0x00, "King");             /* outside the gate: no zone */
    NAMED(FAX_PORTRAIT_KING, 0x07, BUILDINGS, 0x07, "King");
    NAMED(FAX_PORTRAIT_GURU, 0x50, 9, 0x00, "Guru");
}

static void everyone_else(void) {
    /* Every other script with a portrait, by building (bank 11), at its door
     * (bank 3). None of them takes a place. */
    static const struct { uint8_t portrait, building, area, screen; const char *name; } people[] = {
        { FAX_PORTRAIT_MARTIAL_ARTIST, 0x06, EOLIS, 0x06, "Martial Artist" },
        { FAX_PORTRAIT_MAGICIAN, 0x05, EOLIS, 0x06, "Magician" },
        { FAX_PORTRAIT_MAGICIAN, 0x1C, MIST, 0x29, "Magician" },          /* "a magic hall outside town" */
        { FAX_PORTRAIT_DOCTOR, 0x0C, TOWNS, 0x01, "Doctor" }, { FAX_PORTRAIT_DOCTOR, 0x11, TOWNS, 0x03, "Doctor" },
        { FAX_PORTRAIT_DOCTOR, 0x1B, TOWNS, 0x05, "Doctor" }, { FAX_PORTRAIT_DOCTOR, 0x24, TOWNS, 0x06, "Doctor" },
        { FAX_PORTRAIT_DOCTOR, 0x2C, TOWNS, 0x08, "Doctor" }, { FAX_PORTRAIT_DOCTOR, 0x3F, TOWNS, 0x0D, "Doctor" },
        { FAX_PORTRAIT_NURSE, 0x0C, TOWNS, 0x01, "Nurse" }, { FAX_PORTRAIT_NURSE, 0x11, TOWNS, 0x03, "Nurse" },
        { FAX_PORTRAIT_NURSE, 0x1B, TOWNS, 0x05, "Nurse" },
        { FAX_PORTRAIT_NURSE, 0x31, TOWNS, 0x0A, "Nurse" },              /* Daybreak's meat shop */
        { FAX_PORTRAIT_MEAT_SALESMAN, 0x00, EOLIS, 0x01, "Meat Seller" },
        { FAX_PORTRAIT_MEAT_SALESMAN, 0x0E, TOWNS, 0x02, "Meat Seller" },
        { FAX_PORTRAIT_MEAT_SALESMAN, 0x3E, TOWNS, 0x0C, "Meat Seller" },
        { FAX_PORTRAIT_TOOLS_SALESMAN, 0x04, EOLIS, 0x03, "Tool Seller" },
        { FAX_PORTRAIT_TOOLS_SALESMAN, 0x14, TRUNK, 0x06, "Tool Seller" },  /* "stores outside town" */
        { FAX_PORTRAIT_TOOLS_SALESMAN, 0x20, MIST, 0x0D, "Tool Seller" },
        { FAX_PORTRAIT_TOOLS_SALESMAN, 0x45, TOWNS, 0x07, "Tool Seller" },
        { FAX_PORTRAIT_KEY_SALESMAN, 0x03, EOLIS, 0x03, "Key Seller" },
        { FAX_PORTRAIT_KEY_SALESMAN, 0x40, TOWNS, 0x0D, "Key Seller" },
    };
    for (unsigned i = 0; i < sizeof people / sizeof people[0]; ++i)
        NAMED(people[i].portrait, people[i].building, people[i].area, people[i].screen, people[i].name);
    /* No name: the two portraits no script shows, no portrait, and anything past them. */
    CHECK(!named(FAX_PORTRAIT_PINK_SHIRT_GUY, 0x01, EOLIS, 0x02));
    CHECK(!named(FAX_PORTRAIT_SMOKING_GUY, 0x01, EOLIS, 0x02));
    CHECK(!named(0xFF, 0x00, EOLIS, 0x01));                           /* IScripts_ClearPortraitImage */
    CHECK(!named(FAX_PORTRAITS, 0x00, EOLIS, 0x01) && !named(0x80, 0x07, EOLIS, 0x07));
    /* Every name fits a tab no wider than the dialogue box, inside its frame. */
    for (unsigned p = 0; p < FAX_PORTRAITS; ++p)
        for (unsigned b = 0; b < 0x50; ++b)
            for (int z = FAX_ZONE_NONE; z < FAX_ZONE_COUNT; ++z) {
                char out[FAX_SPEAKER_NAME_MAX + 1];
                if (!fax_speaker_name(p, b, z, out)) { CHECK(p == FAX_PORTRAIT_PINK_SHIRT_GUY || p == FAX_PORTRAIT_SMOKING_GUY); continue; }
                Tab tab = { .pad = (int)strlen(out) + 4 <= DIALOGUE_W ? 1 : 0 };
                strcpy(tab.name, out);
                CHECK(strlen(out) <= FAX_SPEAKER_NAME_MAX && tab_cols(&tab) + 2 <= DIALOGUE_W);
            }
}

/* ---- the game, as a script runs ---- */
#define AT(a) ram[a]
static void put_tile(int col, int row, uint8_t tile) {
    int x = (scroll_x + col * 8) & 511, y = (scroll_y + row * 8) % 480;
    ciram[((x >= 256) ? 0x400 : 0) + (y % 240) / 8 * 32 + (x & 255) / 8] = tile;
}
/* TextBox_Open: frame tiles top row first; `rows` of it so far, or all. */
static void frame(int x, int y, int w, int h, int rows) {
    for (int r = 0; r < h && r < rows; ++r)
        for (int c = 0; c < w; ++c) {
            bool top = r == 0, bottom = r == h - 1, left = c == 0, right = c == w - 1;
            put_tile(x + c, y + r, top ? (left ? 1 : right ? 3 : 2) : bottom ? (left ? 5 : right ? 6 : 2) : left || right ? 4 : 0);
        }
    AT(TEXTBOX_X) = (uint8_t)x; AT(TEXTBOX_Y) = (uint8_t)y; AT(TEXTBOX_X + 2) = (uint8_t)w; AT(TEXTBOX_X + 3) = (uint8_t)h;
}
/* TextBox_Close: the screen's own tiles back, top row first. */
static void unframe(int x, int y, int w, int h, int rows) {
    for (int r = 0; r < h && r < rows; ++r)
        for (int c = 0; c < w; ++c) put_tile(x + c, y + r, 0x40);
    AT(TEXTBOX_X) = (uint8_t)x; AT(TEXTBOX_Y) = (uint8_t)y; AT(TEXTBOX_X + 2) = (uint8_t)w; AT(TEXTBOX_X + 3) = (uint8_t)h;
}
/* In a building whose door is at (area, screen). */
static void inside(unsigned building, unsigned area, unsigned screen) {
    memset(ram, 0, sizeof ram);
    memset(ciram, 0x40, sizeof ciram);    /* the room's own tiles */
    AT(AREA_CURRENT_AREA) = BUILDINGS; AT(AREA_CURRENT_SCREEN) = (uint8_t)building;
    AT(AREA_SAVED_AREA) = (uint8_t)area; AT(AREA_SAVED_SCREEN) = (uint8_t)screen;
    AT(ISCRIPT_PORTRAIT_ID) = 0xFF;       /* Screen_ClearSprites */
    deactivate(); activate();
}
/* IScripts_Begin: the portrait, then its frame, then the dialogue box. */
static void begin_script(unsigned portrait) {
    AT(ISCRIPT_PORTRAIT_ID) = (uint8_t)portrait;
    frame(PORTRAIT_X, PORTRAIT_Y, PORTRAIT_W, PORTRAIT_H, 99);
    frame(DIALOGUE_X, DIALOGUE_Y, DIALOGUE_W, 8, 99);
}
/* IScriptAction_EndScript with a portrait: the ID first, then both boxes. */
static void end_script(void) {
    AT(ISCRIPT_PORTRAIT_ID) = 0xFF;
    unframe(2, 8, 30, 20, 99);
}
static bool tab(Tab *t) { memset(t, 0, sizeof *t); return enabled && !failed && tab_now(t); }

static void with_the_box(void) {
    Tab t;
    inside(0x0B, TOWNS, 0x01);                      /* Apolune's temple */
    CHECK(!tab(&t));                                 /* nobody talking */
    /* The portrait comes first, then its frame row by row: only a whole frame counts. */
    AT(ISCRIPT_PORTRAIT_ID) = FAX_PORTRAIT_GURU;
    CHECK(!tab(&t));
    for (int rows = 1; rows < PORTRAIT_H; ++rows) {
        frame(PORTRAIT_X, PORTRAIT_Y, PORTRAIT_W, PORTRAIT_H, rows);
        CHECK(!tab(&t));
    }
    frame(PORTRAIT_X, PORTRAIT_Y, PORTRAIT_W, PORTRAIT_H, 99);
    put_tile(PORTRAIT_X + PORTRAIT_W - 1, PORTRAIT_Y + PORTRAIT_H - 1, 0x40);   /* all but its last tile */
    CHECK(!tab(&t));
    frame(PORTRAIT_X, PORTRAIT_Y, PORTRAIT_W, PORTRAIT_H, 99);
    CHECK(tab(&t) && !strcmp(t.name, "Guru of Apolune"));
    /* On the dialogue box, at its left edge: a frame around one line, a blank cell each side. */
    frame(DIALOGUE_X, DIALOGUE_Y, DIALOGUE_W, 8, 99);
    CHECK(tab(&t) && t.col == 10 && t.row == 13 && t.pad == 1 && tab_cols(&t) == 17);
    /* The portrait's frame stays while the dialogue box is redrawn, and so does the tab. */
    unframe(DIALOGUE_X, DIALOGUE_Y, DIALOGUE_W, 8, 99);
    CHECK(tab(&t) && t.row == 13);
    /* A shop's list covers the dialogue box and rises above it; the tab sits on it. */
    frame(10, 8, 20, 20, 99);
    CHECK(tab(&t) && t.col == 10 && t.row == 5);
    unframe(10, 8, 20, 20, 3);                       /* Maybe_Shop_DrawTextBox closes it... */
    CHECK(tab(&t) && t.row == 5);
    frame(DIALOGUE_X, DIALOGUE_Y, DIALOGUE_W, 8, 99);   /* ...and opens the dialogue box again */
    CHECK(tab(&t) && t.row == 13);
    /* Never below the dialogue box, nor into the HUD, whatever box was last at its column. */
    AT(TEXTBOX_Y) = 18;
    CHECK(tab(&t) && t.row == 13);
    AT(TEXTBOX_Y) = 6;
    CHECK(tab(&t) && t.row == 13);
    AT(TEXTBOX_Y) = 7;
    CHECK(tab(&t) && t.row == 4);
    AT(TEXTBOX_X) = 2; AT(TEXTBOX_Y) = 8;            /* not at the dialogue's column: not under the tab */
    CHECK(tab(&t) && t.row == 13);
    frame(DIALOGUE_X, DIALOGUE_Y, DIALOGUE_W, 8, 99);
    /* The end: the ID goes first, and with it the tab. */
    end_script();
    CHECK(!tab(&t));
    /* If the boxes were cleared with the ID still set, the frame's first row going would do. */
    begin_script(FAX_PORTRAIT_GURU);
    CHECK(tab(&t));
    unframe(2, 8, 30, 20, 7);                        /* rows 8-14: the portrait's top row */
    CHECK(!tab(&t));
}

static void only_portraits_in_buildings(void) {
    Tab t;
    /* The title: RAM is all zero, which reads as the King with a box open. */
    inside(0x07, EOLIS, 0x07);
    memset(ram, 0, sizeof ram);
    begin_script(FAX_PORTRAIT_KING);
    AT(AREA_CURRENT_AREA) = EOLIS;
    CHECK(!tab(&t));
    AT(AREA_CURRENT_AREA) = BUILDINGS; AT(AREA_CURRENT_SCREEN) = 0x07;
    AT(AREA_SAVED_AREA) = EOLIS; AT(AREA_SAVED_SCREEN) = 0x07;
    CHECK(tab(&t) && !strcmp(t.name, "King of Eolis"));
    /* A box without a portrait (a walking man, an item): the ID stays $FF. */
    inside(0x01, EOLIS, 0x02);
    frame(DIALOGUE_X, 6, DIALOGUE_W, 8, 99);
    CHECK(!tab(&t));
    /* The portraits no script shows have no name. */
    begin_script(FAX_PORTRAIT_SMOKING_GUY);
    CHECK(!tab(&t));
    /* Off: nothing. */
    begin_script(FAX_PORTRAIT_NURSE);
    CHECK(tab(&t));
    deactivate();
    CHECK(!tab(&t));
}

static void through_the_scroll(void) {
    Tab t;
    /* The boxes are written into the nametable the screen shows, wherever the
     * scroll puts it. The right-hand table: */
    inside(0x0E, TOWNS, 0x02);
    scroll_x = 256;
    begin_script(FAX_PORTRAIT_MEAT_SALESMAN);
    CHECK(tab(&t) && !strcmp(t.name, "Meat Seller"));
    scroll_x = 0;                                    /* the other table: no frame there */
    CHECK(!tab(&t));
    /* Two rows down, in the lower table, which mirrors the upper one. */
    inside(0x0E, TOWNS, 0x02);
    scroll_y = 240 + 16;
    begin_script(FAX_PORTRAIT_MEAT_SALESMAN);
    CHECK(tab(&t));
    scroll_y = 0;
    CHECK(!tab(&t));
    scroll_y = 16;
    CHECK(tab(&t));
    /* Across the right table's edge into the left one. */
    inside(0x0E, TOWNS, 0x02);
    scroll_x = 400; scroll_y = 8;
    begin_script(FAX_PORTRAIT_MEAT_SALESMAN);
    CHECK(tab(&t) && t.row == 13);
    scroll_x = scroll_y = 0;
    CHECK(!tab(&t));
}

/* ---- drawing ---- */
static void glyphs_and_tiles(void) {
    memset(cart, 0xFF, sizeof cart);
    uint8_t *font = cart + 13 * 16384;
    for (int c = 0x20; c < 0x80; ++c)
        for (int r = 0; r < 8; ++r) font[(c - 0x20) * 8 + r] = c == ' ' ? 0 : (uint8_t)(c ^ (r * 0x11));
    /* Pattern tile t in the background table: every pixel is t & 3. */
    memset(chr, 0, sizeof chr);
    for (unsigned t_ = 0; t_ < 8; ++t_)
        for (int r = 0; r < 8; ++r) {
            chr[bg_table + t_ * 16 + r] = (t_ & 1) ? 0xFF : 0;
            chr[bg_table + t_ * 16 + r + 8] = (t_ & 2) ? 0xFF : 0;
        }
    for (unsigned i = 0; i < 256 * 240; ++i) native[i] = 0xFF123456u;
    fax_text_unload();
}

/* The tab's frame tile at cell (c, r) of a tab drawn at (x, y), sampled mid-cell. */
static uint32_t cell(const uint32_t *px, int w, int x, int y, int c, int r) {
    return px[(size_t)(y + r * 8 + 4) * (size_t)w + (size_t)(x + c * 8 + 4)];
}

/* `text` drawn with its top-left at (x, y): the UI palette's color 3 exactly
 * where each glyph's bits are, black (the box's inside) elsewhere. */
static bool shows(const uint32_t *px, int w, int x, int y, const char *text, unsigned palette) {
    for (int i = 0; text[i]; ++i)
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                bool on = glyphs[text[i] - 0x20][r] >> (7 - c) & 1;
                uint32_t got = px[(size_t)(y + r) * (size_t)w + (size_t)(x + i * 8 + c)];
                if (got != (on ? cyc_render_color((int)(palette * 4 + 3)) : cyc_render_color(0))) return false;
            }
    return true;
}

static void presenting(void) {
    glyphs_and_tiles();
    inside(0x0B, TOWNS, 0x01);
    ram[UI_ATTRIBUTE_DATA_INDEX] = 1;
    begin_script(FAX_PORTRAIT_GURU);
    /* No picture of Seamless's: the engine's, copied, with the tab over it. */
    presents = 0;
    int w = 0, h = 0;
    const uint32_t *out = fax_speaker_present(NULL, &w, &h);
    CHECK(out && out != native && presents == 1 && w == 256 && h == 240 && fax_text_loaded());
    CHECK(native[104 * 256 + 84] == 0xFF123456u);                     /* the machine's picture is untouched */
    /* "Guru of Apolune": 15 characters, a blank cell each side, a frame: 19 cells at (80, 104). */
    CHECK(out[104 * 256 + 79] == 0xFF123456u && out[104 * 256 + 80] != 0xFF123456u);
    CHECK(out[104 * 256 + 80 + 19 * 8 - 1] != 0xFF123456u && out[104 * 256 + 80 + 19 * 8] == 0xFF123456u);
    CHECK(out[103 * 256 + 100] == 0xFF123456u && out[127 * 256 + 100] != 0xFF123456u);
    CHECK(out[128 * 256 + 100] == 0xFF123456u);                       /* the dialogue box below is the game's */
    /* TextBox_Open's tiles in the UI palette: corners, edges, and black inside. */
    CHECK(cell(out, 256, 80, 104, 0, 0) == cyc_render_color(1 * 4 + 1));    /* tile 1: top left */
    CHECK(cell(out, 256, 80, 104, 1, 0) == cyc_render_color(1 * 4 + 2));    /* tile 2: top */
    CHECK(cell(out, 256, 80, 104, 18, 0) == cyc_render_color(1 * 4 + 3));   /* tile 3: top right */
    CHECK(cell(out, 256, 80, 104, 0, 1) == cell(out, 256, 80, 104, 18, 1)); /* tile 4: the sides */
    CHECK(cell(out, 256, 80, 104, 0, 1) == cyc_render_color(0));
    CHECK(cell(out, 256, 80, 104, 0, 2) == cyc_render_color(1 * 4 + 1));    /* tile 5: bottom left */
    CHECK(cell(out, 256, 80, 104, 18, 2) == cyc_render_color(1 * 4 + 2));   /* tile 6: bottom right */
    CHECK(cell(out, 256, 80, 104, 1, 1) == cyc_render_color(0));            /* the blank cell */
    /* The name, after the blank cell. */
    CHECK(shows(out, 256, 96, 112, "Guru of Apolune", 1));
    /* A wider picture of Seamless's: at the native picture's place in it, centered. */
    static uint32_t wide[426 * 240];
    for (unsigned i = 0; i < 426 * 240; ++i) wide[i] = 0xFF654321u;
    w = 426; h = 240; presents = 0;
    out = fax_speaker_present(wide, &w, &h);
    CHECK(out != wide && w == 426 && h == 240 && presents == 0);
    CHECK(out[104 * 426 + 85 + 79] == 0xFF654321u && out[104 * 426 + 85 + 80] == cyc_render_color(1 * 4 + 1));
    /* And a taller one: the native picture at the top, as Seamless draws a building. */
    static uint32_t tall[300 * 320];
    for (unsigned i = 0; i < 300 * 320; ++i) tall[i] = 0xFF654321u;
    w = 300; h = 320;
    out = fax_speaker_present(tall, &w, &h);
    CHECK(out[104 * 300 + 22 + 80] == cyc_render_color(1 * 4 + 1) && out[103 * 300 + 22 + 80] == 0xFF654321u);
    /* Narrower than 256, the native picture is scaled down to fit; the tab goes where its corner went. */
    int x = 80, y = 104;
    to_picture(200, 240, &x, &y);
    CHECK(x == 62 && y == 81);
    /* "Guru of Fraternal" does not fit with blank cells: it touches its frame, 19 cells. */
    inside(0x42, DARTMOOR, 0x1E);
    ram[UI_ATTRIBUTE_DATA_INDEX] = 1;
    begin_script(FAX_PORTRAIT_GURU);
    w = h = 0;
    out = fax_speaker_present(NULL, &w, &h);
    CHECK(out != native && out[104 * 256 + 80 + 19 * 8 - 1] != 0xFF123456u && out[104 * 256 + 80 + 19 * 8] == 0xFF123456u);
    CHECK(shows(out, 256, 88, 112, "Guru of Fraternal", 1));
    /* On a shop's list, five rows down from the HUD. */
    inside(0x03, EOLIS, 0x03);
    begin_script(FAX_PORTRAIT_KEY_SALESMAN);
    frame(10, 8, 20, 20, 99);
    w = h = 0;
    out = fax_speaker_present(NULL, &w, &h);
    CHECK(out[40 * 256 + 80] == cyc_render_color(1) && out[39 * 256 + 80] == 0xFF123456u);
    /* Nobody talking, or off: the picture as it came. */
    end_script();
    w = 256; h = 240;
    CHECK(fax_speaker_present(native, &w, &h) == native && fax_speaker_present(NULL, &w, &h) == NULL);
    begin_script(FAX_PORTRAIT_KEY_SALESMAN);
    deactivate();
    CHECK(fax_speaker_present(native, &w, &h) == native);
}

static void without_the_font(void) {
    memset(cart, 0, sizeof cart);                                     /* no glyphs: no letters */
    fax_text_unload();
    inside(0x0B, TOWNS, 0x01);
    begin_script(FAX_PORTRAIT_GURU);
    int w = 256, h = 240;
    CHECK(fax_speaker_present(native, &w, &h) == native);
    CHECK(failed && strstr(logged, "speaker names are off"));
    CHECK(fax_speaker_present(native, &w, &h) == native);
    deactivate();
    CHECK(!failed);
}

int main(void) {
    kings_and_gurus();
    everyone_else();
    with_the_box();
    only_portraits_in_buildings();
    through_the_scroll();
    presenting();
    without_the_font();
    printf("speaker_test: %u checks passed\n", checks);
    return 0;
}
