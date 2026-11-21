/* Unit tests for zone names (fax_zone.c, fax_text.c, fax_banner.c), without
 * the emulator or a ROM. The zone table is checked against area and screen
 * pairs the disassembly places (door, town-edge and temple tables, and where
 * the items are). The banner runs against a model of the machine: CPU RAM as
 * the game leaves it when it walks between screens, goes through doors and
 * respawns (with the column of tiles each screen leaves over the text box's
 * variables), the nametable the last frame showed, with the boxes TextBox_Open
 * frames there, a cart image holding glyphs, and pattern tiles and a palette
 * for drawing. */
#include <stdio.h>
static char logged[128];
#define FAX_BANNER_LOG(...) snprintf(logged, sizeof logged, __VA_ARGS__)
#include "fax_zone.c"
#include "fax_text.c"
#include "fax_banner.c"

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
static uint8_t ram[0x800];
static uint8_t cart[16 * 16384];
static uint8_t chr[0x2000];
static uint8_t ciram[0x400];        /* the nametable the last frame showed, unscrolled */
static uint16_t bg_table = 0x1000;
static uint32_t native[256 * 240];
static int presents;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
size_t cyc_mod_copy_cart_payload(uint8_t *out, size_t capacity) {
    if (out && capacity >= sizeof cart) memcpy(out, cart, sizeof cart);
    return sizeof cart;
}
uint16_t cyc_render_line_bg_table(int y) { (void)y; return bg_table; }
uint8_t cyc_render_chr(uint16_t a) { return chr[a & 0x1FFF]; }
int cyc_render_line_scroll_x(int y) { (void)y; return 0; }
int cyc_render_line_scroll_y(int y) { return y; }
uint8_t cyc_render_nametable(uint16_t a) { return ciram[a & 0x3FF]; }
/* Palette entry e as a color whose blue byte is e, so a pixel tells its entry. */
uint32_t cyc_render_color(int e) { return 0xFF000000u | 0x00400000u | (uint32_t)(e & 31); }
const uint32_t *cyc_render_present(int *w, int *h) { ++presents; *w = 256; *h = 240; return native; }
int nes_mod_register_reset_callback(NESModActivationCallback cb) { return cb == deactivate; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    return !strcmp(id, "faxanadu.zone-names") && cb == activate;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    return !strcmp(id, "faxanadu.zone-names") && get == state_get && set == state_set;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate v) {
    return !strcmp(id, "faxanadu.zone-names") && v == state_validate;
}

enum { EOLIS = 0, TRUNK = 1, MIST = 2, TOWNS = 3, BUILDINGS = 4, BRANCH = 5, DARTMOOR = 6, ZENIS = 7 };
static const unsigned screens[8] = { 9, 64, 83, 14, 0, 40, 32, 20 };   /* <AREA>_SCROLL_DATA / 4 */

static const char *name_at(unsigned area, unsigned screen) {
    const FaxZone *z = fax_zone(fax_zone_at(area, screen));
    return z ? z->name : "";
}
#define NAMED(area, screen, name) CHECK(!strcmp(name_at(area, screen), name))

/* ---- the zone table ---- */
static void towns(void) {
    /* AREA_TOWN_TRANSITIONS_DATA: each town's screen off a region's edge. */
    static const struct { uint8_t from_area, from, screen; const char *name; } edges[] = {
        { TRUNK, 0x07, 0x00, "Apolune" }, { TRUNK, 0x08, 0x01, "Apolune" },
        { TRUNK, 0x1A, 0x02, "Forepaw" }, { TRUNK, 0x1D, 0x03, "Forepaw" },
        { MIST, 0x09, 0x04, "Mascon" }, { MIST, 0x0C, 0x05, "Mascon" },
        { MIST, 0x22, 0x06, "Victim" }, { MIST, 0x25, 0x07, "Victim" },
        { BRANCH, 0x0D, 0x08, "Conflate" },
        { BRANCH, 0x23, 0x0A, "Daybreak" }, { BRANCH, 0x24, 0x0B, "Daybreak" },
        { DARTMOOR, 0x03, 0x0C, "Dartmoor" },
    };
    for (unsigned i = 0; i < sizeof edges / sizeof edges[0]; ++i) {
        NAMED(TOWNS, edges[i].screen, edges[i].name);
        CHECK(fax_zone(fax_zone_at(TOWNS, edges[i].screen))->kind == FAX_ZONE_TOWN);
        CHECK(fax_zone(fax_zone_at(edges[i].from_area, edges[i].from))->kind == FAX_ZONE_REGION);
    }
    NAMED(TOWNS, 0x09, "Conflate");   /* the town's right half, with no way out */
    NAMED(TOWNS, 0x0D, "Dartmoor");
    /* Where a temple's door lets out (START_SCREEN_FOR_TEMPLE_SPAWN and the
     * area after it): the Guru outside Mascon is in the Mist itself. */
    static const struct { uint8_t area, screen; const char *name; } temples[8] = {
        { EOLIS, 0x02, "Eolis" }, { TOWNS, 0x01, "Apolune" }, { TOWNS, 0x03, "Forepaw" }, { MIST, 0x06, "Mist" },
        { TOWNS, 0x06, "Victim" }, { TOWNS, 0x08, "Conflate" }, { TOWNS, 0x0B, "Daybreak" }, { TOWNS, 0x0C, "Dartmoor" },
    };
    for (unsigned i = 0; i < 8; ++i) NAMED(temples[i].area, temples[i].screen, temples[i].name);
    NAMED(EOLIS, 0x01, "Eolis");
    NAMED(EOLIS, 0x07, "Eolis");      /* the King's door */
    NAMED(EOLIS, 0x08, "Eolis");      /* the door out to the Trunk */
    CHECK(fax_zone_at(EOLIS, 0x00) == FAX_ZONE_NONE);   /* outside the gate: the intro */
}

static void regions(void) {
    /* MAYBE_REGION_TRANSITION_MAP: where the doors between regions arrive. */
    NAMED(TRUNK, 0x00, "Trunk");
    NAMED(MIST, 0x11, "Mist");
    NAMED(BRANCH, 0x00, "Branch");
    NAMED(DARTMOOR, 0x00, "Dartmoor");
    CHECK(fax_zone(fax_zone_at(DARTMOOR, 0x00))->kind == FAX_ZONE_REGION);
    NAMED(ZENIS, 0x08, "Fortress of Zenis");
    /* The far ends of those doors, and AREA_SCREEN_COMPARATORS' climb. */
    NAMED(TRUNK, 0x28, "Trunk"); NAMED(MIST, 0x1F, "Mist"); NAMED(BRANCH, 0x27, "Branch"); NAMED(DARTMOOR, 0x0E, "Dartmoor");
    NAMED(TRUNK, 0x0C, "Trunk"); NAMED(TRUNK, 0x16, "Trunk");
    NAMED(TRUNK, 0x25, "Trunk");      /* the spring in the sky */
    NAMED(BRANCH, 0x07, "Branch");    /* the darker side of the tree: the Battle Helmet */
    NAMED(BRANCH, 0x1E, "Branch");    /* the Magical Rod */
}

static void towers(void) {
    /* <AREA>_DOOR_LOCATIONS: a door into a tower, and where it arrives. */
    static const struct { uint8_t area, door, inside; const char *name; } doors[] = {
        { TRUNK, 0x0B, 0x0D, "Tower of Trunk" }, { TRUNK, 0x28, 0x29, "Tower of Fortress" },
        { MIST, 0x00, 0x3E, "Tower of Suffer" }, { MIST, 0x17, 0x45, "Tower of Mist" },
        { DARTMOOR, 0x0C, 0x10, "Castle of Fraternal" },
    };
    for (unsigned i = 0; i < sizeof doors / sizeof doors[0]; ++i) {
        NAMED(doors[i].area, doors[i].inside, doors[i].name);
        CHECK(fax_zone_at(doors[i].area, doors[i].door) != fax_zone_at(doors[i].area, doors[i].inside));
        CHECK(fax_zone(fax_zone_at(doors[i].area, doors[i].door))->kind == FAX_ZONE_REGION);
    }
    /* What the dialogue says is inside (bank 11, SPRITE_INFO_*). */
    NAMED(TRUNK, 0x15, "Tower of Trunk");      /* the Mattock: "should be in the Tower" */
    NAMED(TRUNK, 0x3D, "Tower of Fortress");   /* "a spring in the tower of Fortress" */
    NAMED(TRUNK, 0x3B, "Tower of Fortress");   /* the Dwarf with the Wing Boots */
    NAMED(MIST, 0x32, "Tower of Suffer");      /* "Look for [Wing Boots] in the Tower of Suffer" */
    NAMED(MIST, 0x4C, "Tower of Mist");        /* the Black Onyx, "the meteorite" */
    NAMED(DARTMOOR, 0x15, "Castle of Fraternal");   /* King Grieve's room, inside the castle */
    NAMED(ZENIS, 0x00, "Fortress of Zenis");
    /* Places the game never names stay part of their region. */
    NAMED(TRUNK, 0x3E, "Trunk"); NAMED(TRUNK, 0x3F, "Trunk");   /* behind the Joker door */
    NAMED(MIST, 0x4F, "Mist"); NAMED(MIST, 0x50, "Mist");
}

static void coverage(void) {
    /* Every screen of every area but the buildings is in a zone, Eolis's gate aside. */
    for (unsigned area = 0; area < 8; ++area)
        for (unsigned s = 0; s < 0x60; ++s) {
            int z = fax_zone_at(area, s);
            bool real = s < screens[area];
            CHECK(real == (z != FAX_ZONE_NONE) || (area == EOLIS && s == 0));
        }
    for (unsigned s = 0; s < 0x100; ++s) CHECK(fax_zone_at(BUILDINGS, s) == FAX_ZONE_NONE);
    CHECK(fax_zone_at(8, 0) == FAX_ZONE_NONE);
    CHECK(fax_zone(FAX_ZONE_NONE) == NULL && fax_zone(FAX_ZONE_COUNT) == NULL);
    /* Every zone is somewhere, and its name fits a box on the 256-pixel screen. */
    bool used[FAX_ZONE_COUNT] = { 0 };
    for (unsigned area = 0; area < 8; ++area)
        for (unsigned s = 0; s < 0x60; ++s) if (fax_zone_at(area, s) >= 0) used[fax_zone_at(area, s)] = true;
    for (int z = 0; z < FAX_ZONE_COUNT; ++z) {
        CHECK(used[z] && fax_zone(z)->name);
        CHECK((strlen(fax_zone(z)->name) + 2 * PAD + 2) * FAX_TEXT_CELL <= 256);
    }
}

/* ---- the game, as it moves the hero ---- */
#define AT(a) ram[a]
#define TEXTBOX_X      0x0208u   /* TextBox_X, _Y, _Width, _Height */
#define TEXTBOX_CLOSED 0x020Cu   /* TextBox_Closed */
enum { ROOM_TILE = 0x80, MENU_X = 2, MENU_Y = 6, MENU_W = 10, MENU_H = 14 };   /* PlayerMenu_Show's box */
static void settle(void) { AT(SCREEN_READY_STATE) = 0; AT(INTERRUPT_HANDLERS_ON) = 1; AT(SCREEN_SCROLL_DIRECTION) = 0xFF; }
static void frames(int n) { while (n-- > 0) fax_banner_step(); }
/* Screen_LoadBlocksHoriz leaves the last column of tiles it loaded at
 * $0200-$021D, over TextBox_X to TextBox_Timer: the area's tiles, from $80,
 * and tile 0, which Forepaw's screen 2 has in the row TextBox_Closed takes. */
static void load_column(unsigned area, unsigned screen) {
    for (unsigned i = 0; i < 30; ++i) AT(0x0200 + i) = (uint8_t)(ROOM_TILE | (area * 16 + screen + i));
    if (area == TOWNS && screen == 2) AT(TEXTBOX_CLOSED) = 0;
}
/* Walking across a screen edge, or through a door to another screen or area. */
static void go(unsigned area, unsigned screen) {
    AT(SCREEN_READY_STATE) = 1; frames(3);   /* loading: whatever RAM says now is not settled */
    AT(AREA_CURRENT_AREA) = (uint8_t)area; AT(AREA_CURRENT_SCREEN) = (uint8_t)screen;
    load_column(area, screen);
    settle(); frames(1);
}
/* TextBox_Open: where the box goes, TextBox_Closed 0, and its frame queued;
 * the NMI writes it, top row first, so it shows from the next frame (show). */
static void open_box(int x, int y, int w, int h) {
    AT(TEXTBOX_X) = (uint8_t)x; AT(TEXTBOX_X + 1) = (uint8_t)y; AT(TEXTBOX_X + 2) = (uint8_t)w; AT(TEXTBOX_X + 3) = (uint8_t)h;
    AT(TEXTBOX_CLOSED) = 0;
}
static void put_tile(int col, int row, uint8_t tile) { ciram[row * 32 + col] = tile; }
static void show(int x, int y, int w, int h) {
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            bool top = r == 0, bottom = r == h - 1, left = c == 0, right = c == w - 1;
            put_tile(x + c, y + r, top ? (left ? 1 : right ? 3 : 2) : bottom ? (left ? 5 : right ? 6 : 2) : left || right ? 4 : 0);
        }
}
/* TextBox_Close: TextBox_Closed 1 first, then the screen's own tiles back. */
static void close_box(void) {
    AT(TEXTBOX_CLOSED) = 1;
    memset(ciram, ROOM_TILE, sizeof ciram);
}
/* Game_EnterBuilding: remember where the hero stood, then the building. */
static void enter(unsigned building) {
    AT(AREA_SAVED_AREA) = AT(AREA_CURRENT_AREA); AT(AREA_SAVED_SCREEN) = AT(AREA_CURRENT_SCREEN);
    go(BUILDINGS, building);
}
/* Game_ExitBuilding: back where the hero stood. */
static void leave(void) { go(AT(AREA_SAVED_AREA), AT(AREA_SAVED_SCREEN)); }
/* Game_SpawnInTemple, then Game_EnterBuilding: a respawn or a Guru travel journey. */
static void spawn(unsigned area, unsigned outside, unsigned temple) {
    AT(AREA_CURRENT_AREA) = (uint8_t)area; AT(AREA_CURRENT_SCREEN) = (uint8_t)outside;
    enter(temple);
}
static void begin(void) {
    memset(ram, 0, sizeof ram);
    memset(ciram, ROOM_TILE, sizeof ciram);
    load_column(EOLIS, 0);       /* the gate's column of tiles (TextBox_Closed reads $9F there) */
    deactivate(); activate();
    fax_banner_reset();
    AT(AREA_CURRENT_AREA) = EOLIS; AT(AREA_CURRENT_SCREEN) = 0;   /* the title, then the gate */
    frames(10);
    settle(); frames(1);
}
static const char *showing(void) { return t.showing == FAX_ZONE_NONE ? "" : fax_zone(t.showing)->name; }

/* ---- the banner ---- */
static void names_on_arrival(void) {
    begin();
    CHECK(banners == 0 && !*showing());                         /* the gate is no zone */
    go(EOLIS, 1);
    CHECK(!strcmp(showing(), "Eolis") && banners == 1);
    go(EOLIS, 2); go(EOLIS, 3);
    CHECK(banners == 1);                                         /* moving within a zone repeats nothing */
    enter(0x03);                                                 /* Eolis's key shop */
    CHECK(!*showing());                                          /* put away inside */
    leave();
    CHECK(banners == 1 && !*showing());                          /* stepping out of a shop says nothing */
    go(EOLIS, 8);
    go(TRUNK, 0x00);                                             /* the door out */
    CHECK(!strcmp(showing(), "Trunk") && banners == 2 && !strcmp(last_name, "Trunk"));
    go(TRUNK, 0x0B); go(TRUNK, 0x0D);
    CHECK(!strcmp(showing(), "Tower of Trunk") && banners == 3);
    go(TRUNK, 0x0E);
    CHECK(banners == 3);
    go(TRUNK, 0x0B);
    CHECK(!strcmp(showing(), "Trunk") && banners == 4);          /* out again: a different zone */
    go(TRUNK, 0x07); go(TOWNS, 0x00);
    CHECK(!strcmp(showing(), "Apolune") && banners == 5);
    go(TOWNS, 0x01);
    CHECK(banners == 5);
}

static void only_when_settled(void) {
    begin();
    go(EOLIS, 1);
    unsigned before = banners;
    /* Mid-scroll toward another area's screen, nothing is read. */
    AT(SCREEN_SCROLL_DIRECTION) = 1;
    AT(AREA_CURRENT_AREA) = TRUNK; AT(AREA_CURRENT_SCREEN) = 0;
    frames(5);
    CHECK(banners == before);
    AT(SCREEN_SCROLL_DIRECTION) = 0xFF; AT(SCREEN_READY_STATE) = 2;
    frames(5);
    CHECK(banners == before);
    AT(SCREEN_READY_STATE) = 0; AT(INTERRUPT_HANDLERS_ON) = 0;
    frames(5);
    CHECK(banners == before);
    AT(INTERRUPT_HANDLERS_ON) = 1;
    frames(1);
    CHECK(banners == before + 1 && !strcmp(showing(), "Trunk"));
}

static void respawn_and_travel(void) {
    begin();
    go(EOLIS, 1);
    go(EOLIS, 8); go(TRUNK, 0x00); go(TRUNK, 0x05);
    CHECK(banners == 2);
    /* Death in the Trunk: Eolis's temple, whose door opens on Eolis screen 2. */
    spawn(EOLIS, 0x02, 0x02);
    CHECK(!*showing());
    leave();
    CHECK(!strcmp(showing(), "Eolis") && banners == 3);
    /* A journey from Conflate's temple to Apolune's. */
    go(TOWNS, 0x08);
    CHECK(!strcmp(showing(), "Conflate") && banners == 4);
    enter(0x2B);
    spawn(TOWNS, 0x01, 0x0B);
    leave();
    CHECK(!strcmp(showing(), "Apolune") && banners == 5);
    /* Through a temple's door and back, nothing. */
    enter(0x0B); leave();
    CHECK(banners == 5);
    /* A journey back to the town just left still names it: the hero was moved. */
    enter(0x0B);
    spawn(TOWNS, 0x08, 0x2B);
    spawn(TOWNS, 0x01, 0x0B);
    leave();
    CHECK(banners == 6 && !strcmp(last_name, "Apolune"));
}

static void one_name_once(void) {
    begin();
    go(BRANCH, 0x27);
    go(DARTMOOR, 0x00);
    CHECK(!strcmp(showing(), "Dartmoor") && banners == 2);
    frames(20);
    go(DARTMOOR, 0x03); go(TOWNS, 0x0C);
    CHECK(banners == 2 && t.here == FAX_ZONE_DARTMOOR_TOWN && t.showing == FAX_ZONE_DARTMOOR);   /* the same banner goes on */
    go(DARTMOOR, 0x03); go(DARTMOOR, 0x0C); go(DARTMOOR, 0x10);
    CHECK(!strcmp(showing(), "Castle of Fraternal") && banners == 3);
}

static void timing(void) {
    begin();
    go(EOLIS, 1);
    CHECK(t.age == 1 && alpha_at(t.age) > 0 && alpha_at(t.age) < 255);   /* the frame it arrived */
    frames(FADE_IN - 1);
    CHECK(alpha_at(t.age) == 255);                                   /* in, in 8 frames */
    frames(HOLD);
    CHECK(alpha_at(t.age) < 255 && alpha_at(t.age) > 200);           /* about 2 s later, fading */
    /* A text box or the menu: hidden, and the clock stops, once its frame
     * shows (in the frame TextBox_Open runs, the picture has no box yet). */
    int w = 256, h = 240;
    open_box(MENU_X, MENU_Y, MENU_W, MENU_H);
    frames(1);
    CHECK(fax_banner_present(native, &w, &h) != native);
    show(MENU_X, MENU_Y, MENU_W, MENU_H);
    uint8_t age = t.age;
    frames(50);
    CHECK(t.age == age && *showing());
    CHECK(fax_banner_present(native, &w, &h) == native);
    close_box();
    AT(GAME_PAUSED_STATE) = 1;                                       /* paused: still there, held */
    frames(50);
    CHECK(t.age == age && fax_banner_present(native, &w, &h) != native);
    AT(GAME_PAUSED_STATE) = 0;
    unsigned last = alpha_at(t.age);
    for (int i = t.age; i < SHOWN - 1; ++i) {
        frames(1);
        CHECK(alpha_at(t.age) <= last && alpha_at(t.age) > 0);
        last = alpha_at(t.age);
    }
    frames(1);
    CHECK(!*showing() && banners == 1);                              /* gone after 160 frames */
    CHECK(fax_banner_present(native, &w, &h) == native);
}

/* Forepaw's screen 2 leaves TextBox_Closed at 0 with no box up (its tile
 * there is 0): the banner shows, fades and goes as anywhere. A box there still
 * holds it: the menu, and its submenu, queued over it a frame before its own
 * frame shows. A frame's corner in the HUD, or one with no top edge after it,
 * is no box. */
static void a_tile_for_a_box(void) {
    begin();
    go(TRUNK, 0x1A); go(TOWNS, 0x02);
    CHECK(!strcmp(showing(), "Forepaw") && AT(TEXTBOX_CLOSED) == 0 && !fax_text_box_showing());
    int w = 256, h = 240;
    frames(FADE_IN);
    CHECK(fax_banner_present(native, &w, &h) != native && t.age == FADE_IN + 1);
    put_tile(5, 2, 1); put_tile(6, 2, 2);                            /* the HUD's rows */
    put_tile(5, 20, 1); put_tile(6, 20, 0x80);                       /* a corner alone */
    frames(10);
    CHECK(t.age == FADE_IN + 11 && fax_banner_present(native, &w, &h) != native);
    open_box(MENU_X, MENU_Y, MENU_W, MENU_H); show(MENU_X, MENU_Y, MENU_W, MENU_H);
    CHECK(fax_text_box_showing());
    frames(40);
    CHECK(t.age == FADE_IN + 11 && fax_banner_present(native, &w, &h) == native);
    open_box(4, 8, 20, 18);                                          /* PlayerMenu_ShowSubmenu */
    frames(1);
    CHECK(t.age == FADE_IN + 11 && fax_banner_present(native, &w, &h) == native);
    show(4, 8, 20, 18);
    frames(20);
    CHECK(t.age == FADE_IN + 11);
    close_box();                                                     /* PlayerMenu_Close */
    frames(SHOWN - (FADE_IN + 11) - 1);
    CHECK(*showing());
    frames(1);
    CHECK(!*showing() && banners == 2);                              /* gone on time */
    /* Two cells wide, a box's corners meet. */
    open_box(20, 10, 2, 3); show(20, 10, 2, 3);
    CHECK(fax_text_box_showing());
    close_box();
    CHECK(!fax_text_box_showing());
}

static void save_states(void) {
    begin();
    go(EOLIS, 1);
    frames(30);
    uint8_t rec[64];
    int n = state_get(rec, sizeof rec);
    CHECK(n == RECORD && state_validate(rec, n));
    Track saved = t;
    go(TRUNK, 0x00); frames(5);
    CHECK(state_set(rec, n) == 1);
    CHECK(!memcmp(&t, &saved, sizeof t) && !strcmp(showing(), "Eolis") && t.age == 31);
    frames(1);
    CHECK(!strcmp(showing(), "Trunk"));                              /* the machine is in the Trunk now */
    /* A state saved without zone names: name the zone again. */
    CHECK(state_set(NULL, 0) == 1 && t.here == FAX_ZONE_NONE && t.named == FAX_ZONE_NONE);
    frames(1);
    CHECK(!strcmp(showing(), "Trunk") && t.age == 1);
    rec[0] = 9; CHECK(!state_validate(rec, n));
    rec[0] = VERSION; rec[1] = FAX_ZONE_COUNT; CHECK(!state_validate(rec, n));
    rec[1] = 0; rec[4] = SHOWN + 1; CHECK(!state_validate(rec, n));
    CHECK(!state_validate(rec, n - 1) && state_set(rec, n) == 0);
    /* Once the banner has gone, the state still saves and loads. */
    frames(SHOWN);
    CHECK(!*showing() && t.age == SHOWN);
    n = state_get(rec, sizeof rec);
    CHECK(state_validate(rec, n) && state_set(rec, n) == 1 && !*showing());
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
}

static void drawing(void) {
    glyphs_and_tiles();
    fax_text_unload();
    CHECK(fax_text_load() && fax_text_loaded());
    CHECK(!memcmp(glyphs['A' - 0x20], cart + 13 * 16384 + 0x108, 8));   /* TILE_GLYPH_A at $8108 */
    CHECK(fax_text_width("Mist") == 32);
    ram[UI_ATTRIBUTE_DATA_INDEX] = 2;
    /* Text: color 3 of the UI palette exactly where the glyph's bits are. */
    static uint32_t px[64 * 16];
    for (unsigned i = 0; i < 64 * 16; ++i) px[i] = 0xFF000000u;
    fax_text_draw(px, 64, 16, 3, 2, "Ab", 255);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) {
            uint8_t bits = glyphs[(c < 8 ? 'A' : 'b') - 0x20][r];
            bool on = bits >> (7 - c % 8) & 1;
            CHECK(px[(2 + r) * 64 + 3 + c] == (on ? cyc_render_color(2 * 4 + 3) : 0xFF000000u));
        }
    CHECK(px[0] == 0xFF000000u && px[10 * 64 + 3] == 0xFF000000u);
    /* Half alpha mixes evenly. */
    uint32_t mix = 0xFF000000u;
    put(&mix, 1, 1, 0, 0, 0xFFFFFFFFu, 128);
    CHECK(mix == 0xFF808080u);
    put(&mix, 1, 1, 5, 0, 0xFFFFFFFFu, 255);                          /* clipped */
    CHECK(mix == 0xFF808080u);
    /* The box: TextBox_Open's tiles around tile 0, in the UI palette. */
    static uint32_t box[48 * 24];
    fax_text_box(box, 48, 24, 0, 0, 4, 1, 255);
    static const unsigned layout[3][6] = { { 1, 2, 2, 2, 2, 3 }, { 4, 0, 0, 0, 0, 4 }, { 5, 2, 2, 2, 2, 6 } };
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 6; ++c) {
            unsigned v = layout[r][c] & 3;
            CHECK(box[(r * 8 + 4) * 48 + c * 8 + 4] == cyc_render_color(v ? (int)(2 * 4 + v) : 0));
        }
}

static void presenting(void) {
    glyphs_and_tiles();
    begin();
    go(TRUNK, 0x29);
    frames(FADE_IN);
    int w = 256, h = 240;
    /* No picture of Seamless's: the engine's, copied, with the banner over it. */
    presents = 0;
    int pw = 0, ph = 0;
    const uint32_t *out = fax_banner_present(NULL, &pw, &ph);
    CHECK(out && out != native && presents == 1 && pw == 256 && ph == 240);
    CHECK(native[45 * 256 + 128] == 0xFF123456u);                    /* the machine's picture is untouched */
    /* "Tower of Fortress": 17 characters, a blank cell each side, a frame: 168 wide, at x 44, y 40. */
    int x0 = (256 - 168) / 2;
    CHECK(out[40 * 256 + x0 - 1] == 0xFF123456u && out[40 * 256 + x0] != 0xFF123456u);
    CHECK(out[40 * 256 + x0 + 167] != 0xFF123456u && out[40 * 256 + x0 + 168] == 0xFF123456u);
    CHECK(out[39 * 256 + 128] == 0xFF123456u && out[(40 + 23) * 256 + 128] != 0xFF123456u);
    CHECK(out[(40 + 24) * 256 + 128] == 0xFF123456u);
    CHECK(out[31 * 256 + 128] == 0xFF123456u);                       /* the HUD is left alone */
    /* A wider picture of Seamless's: centered on it, below the HUD. */
    static uint32_t wide[426 * 240];
    for (unsigned i = 0; i < 426 * 240; ++i) wide[i] = 0xFF654321u;
    w = 426; h = 240; presents = 0;
    out = fax_banner_present(wide, &w, &h);
    CHECK(out != wide && w == 426 && h == 240 && presents == 0);
    CHECK(out[40 * 426 + (426 - 168) / 2] != 0xFF654321u && out[40 * 426 + (426 - 168) / 2 - 1] == 0xFF654321u);
    /* Off, or nothing showing: the picture as it came. */
    deactivate();
    CHECK(fax_banner_present(wide, &w, &h) == wide && fax_banner_present(NULL, &w, &h) == NULL);
    fax_banner_step();
    CHECK(!enabled && banners == 0);
}

static void without_the_font(void) {
    memset(cart, 0, sizeof cart);                                     /* no glyphs: no letters */
    fax_text_unload();
    begin();
    go(EOLIS, 1);
    CHECK(failed && banners == 0 && strstr(logged, "zone names are off"));
    deactivate();
    CHECK(!failed);
}

int main(void) {
    towns();
    regions();
    towers();
    coverage();
    glyphs_and_tiles();
    names_on_arrival();
    only_when_settled();
    respawn_and_travel();
    one_name_once();
    timing();
    a_tile_for_a_box();
    save_states();
    drawing();
    presenting();
    without_the_font();
    printf("zones_test: %u checks passed\n", checks);
    return 0;
}
