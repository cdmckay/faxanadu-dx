/* Unit tests for Sugata's flash (fax_sugata.c), with Block (fax_block.c)
 * beside it, without the emulator or a ROM. Both run against a model of the
 * machine (../block/model.c), and Sugata runs the game's own code around the
 * sites, as the disassembly has it: each frame the sprite loop runs its
 * behavior, SpriteBehavior_FlashScreenHitPlayer (the warning's site at its
 * entry, the greyscale one after STA PPU_Mask, the damage one shared by both
 * features in game.toml's order), then draws it (the blink's site), and the
 * game's own HP and death code takes a reflection. The band is checked on a
 * native picture and on a Seamless canvas. In Seamless a neighbouring room's
 * Sugata runs in that room's tick (isolated, Seamless ticking), against a
 * model of the rooms and of what Seams answers: its flash in reach of the
 * hero's true place waits at its damage until the live game's visitors' pass
 * enters the damage for it, its place then in 16 bits from its side. */
#include "fax_sugata.c"
#include "model.h"

void test_block_hold(bool on);
unsigned test_block_flashes(void);

/* RAM the model's game code uses */
#define HP_U           0x0431u
#define INVINCIBILITY  0x00ADu
#define STATUS_FLAG    0x00A5u
#define PLAYER_FLAGS   0x00A4u
#define SPRITE_HP      0x0344u
#define BEHAVIOR_DATA2 0x02ECu
enum { SLOT = 6, DEATH = 0x14, SUGATA_HP = 22 };
enum { SMALL, LARGE, MAGIC, HELMET, NONE = 0xFF };

/* ---- the picture ---- */
uint32_t hw_palette_argb[512];
static uint32_t native[256 * 240], world[800 * 400];
static int origin_x, origin_y;
static bool world_on;
bool fax_world_origin(int *x, int *y) {
    if (!world_on) return false;
    *x = origin_x;
    *y = origin_y;
    return true;
}

/* ---- Seamless's rooms, and Seams (fax_seams.c) ----
 * Without Seamless (most tests) no room runs: the warning's row is NO_ROOM's.
 * With it, room 4 is the live one and room 5, left of it, a neighbour whose
 * tick runs on the machine (ram) and is kept in its record after. */
enum { LIVE = 4, NEIGHBOUR = 5 };
static int seams_room = -1, active = -1, shift[8], hero_true;
static bool ticking, joined = true, known = true;
static uint8_t records[ROOMS][0x800];
static unsigned asked;
bool fax_world_ticking(void) { return ticking; }
int fax_world_seams_room(void) { return seams_room; }
int fax_world_active_room(void) { return active; }
uint8_t *fax_world_room_record(int r) { return r >= 0 && r < ROOMS ? records[r] : NULL; }
int fax_world_area(void) { return 2; }
bool fax_world_room_origin(int area, int room, int *x, int *y) {
    if (!world_on || area != 2 || (room != LIVE && room != NEIGHBOUR)) return false;
    *x = origin_x + (room == NEIGHBOUR ? -256 : 0);
    *y = origin_y;
    return true;
}
int fax_seams_x16(unsigned s) { return ram[CURRENT_SPRITES_XPOS + s] + shift[s]; }
int fax_seams_room_x16(int room, unsigned s) {
    return room == active || room < 0 ? ram[CURRENT_SPRITES_XPOS + s] : records[room][CURRENT_SPRITES_XPOS + s];
}
bool fax_seams_hero_x(int *x) {
    if (!isolated_scope) {
        *x = ram[PLAYER_POS_X];
        return true;
    }
    *x = hero_true;
    return known;
}
bool fax_seams_flash(void) {
    ++asked;
    return ticking && joined;
}
const uint32_t *cyc_render_present(int *width, int *height) {
    *width = 256;
    *height = 240;
    return native;
}
int cyc_render_native_origin(int width) { return (width - 256) / 2; }
static void palette(void) { /* a 2C02-like palette: the $xD-$xF columns black, $20 and $30 white */
    for (int e = 0; e < 64; ++e) {
        unsigned col = e & 0x0F, row = e >> 4;
        uint32_t c = 0xFF000000u | (uint32_t)(40 + row * 50 + col * 3) << 16 | (uint32_t)(20 + col * 12) << 8 |
                     (uint32_t)(row * 60);
        if (col >= 0x0D) c = 0xFF000000u;
        hw_palette_argb[e] = c;
    }
    hw_palette_argb[0x00] = 0xFF666666u;
    hw_palette_argb[0x10] = 0xFFADADADu;
    hw_palette_argb[0x20] = hw_palette_argb[0x30] = 0xFFFFFFFFu;
    palette_ready = false;
}

/* ---- the game's code ---- */
static void damage_hero(uint8_t hp) {
    ram[INVINCIBILITY] = 0x3C;
    ram[STATUS_FLAG] |= 0x02;
    ram[HP_U] = (uint8_t)(ram[HP_U] - hp);
}
static void sound(uint8_t id) {
    CycModRegs r = cpu;
    r.a = id;
    cyc_mod_call_commit(0xD0E4, &r);
}

static unsigned finished;
static void finish(void) { /* $AB60: LDX CurrentSpriteIndex, JMP Sprite_FinishBehavior */
    cpu.x = ram[CURRENT_SPRITE_INDEX];
    ram[CURRENT_SPRITES_FLAGS + cpu.x] &= (uint8_t)~BEHAVIOR_READY;
    ram[CURRENT_SPRITES_BEHAVIORS + cpu.x] |= 0x80;
    ++finished;
}
/* Player_HitEnemyWithMagic's @_reduceEnemyHP ($81FD), X the sprite. */
static unsigned kills;
static void reduce_enemy_hp(void) {
    unsigned x = cpu.x;
    int hp = ram[SPRITE_HP + x] - ram[TEMP_00];
    ram[SPRITE_HP + x] = (uint8_t)hp;
    if (hp >= 0) return;                                                    /* BCS RETURN_822D */
    if (ram[CURRENT_SPRITES_ENTITIES + x] == SUGATA) ram[PPU_MASK] &= 0xFE; /* its greyscale, cleared */
    ++kills;                                                                /* Player_AddExperienceFromSprite */
    sound(0x03);
    ram[CURRENT_SPRITES_ENTITIES + x] = DEATH; /* Sprite_ShowNormalEnemyDeathByMagic */
    ram[CURRENT_SPRITES_BEHAVIORS + x] = 0xFF;
    ram[CURRENT_SPRITES_HIT_BY_MAGIC + x] = 0xFF;
    ram[CURRENT_SPRITES_FLAGS + x] &= (uint8_t)~BEHAVIOR_READY;
}
static bool drawn;
static void invisible(void) { drawn = false; } /* SpriteUpdateHandler_Invisible */

/* Damage numbers (fax_damage.c): a reflection's number, reported before the
 * committed call, while the sprite is still Sugata. */
static unsigned numbered, numbered_amount, numbered_slot, numbered_entity;
void fax_damage_enemy_hit(unsigned slot, unsigned amount) {
    ++numbered;
    numbered_amount = amount;
    numbered_slot = slot;
    numbered_entity = ram[CURRENT_SPRITES_ENTITIES + slot];
}

/* MP from kills (fax_mana.c): a kill the committed call made, which runs no
 * hooks, reported after it. */
static unsigned killed_reported, killed_entity;
void fax_mana_killed(uint8_t entity) {
    ++killed_reported;
    killed_entity = entity;
}

/* SpriteBehavior_FlashScreenHitPlayer ($AB26), X the sprite. */
static void flash_behavior(void) {
    unsigned x = SLOT;
    cpu.x = (uint8_t)x;
    if (!SITES(FLASH_BEHAVIOR, "faxanadu.sugata.behavior")) return;
    if (!(ram[CURRENT_SPRITES_FLAGS + x] & BEHAVIOR_READY)) { /* JSR Sprite_IsBehaviorNotReady, BNE */
        ram[BEHAVIOR_DATA2 + x] = 2;
        ram[PPU_MASK] |= 0x01; /* greyscale */
        CHECK(SITES(FLASH_GREYSCALE, "faxanadu.sugata.greyscale"));
        ram[CURRENT_SPRITES_FLAGS + x] |= BEHAVIOR_READY; /* JSR Sprite_SetBehaviorReady */
    }
    if (--ram[BEHAVIOR_DATA2 + x]) return; /* DEC, BNE @_return */
    ram[PPU_MASK] &= 0xFE;
    if (!SITES(FLASH_HIT, "faxanadu.sugata.hit", "faxanadu.block.flash")) return;
    sound(0x04);
    damage_hero(10); /* iframes, knockback, Player_ReduceHP */
    finish();
}
/* One frame of the sprite loop for Sugata's slot, then the frame's end. */
static void frame(void) {
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    if (ram[CURRENT_SPRITES_ENTITIES + SLOT] == SUGATA) {
        if (ram[CURRENT_SPRITES_HIT_BY_MAGIC + SLOT] < 0x80) { /* knocked back: no behavior */
            /* Sprites_Maybe_UpdateHitByMagic, a Deluge's: 3 pixels the way it faces */
            ram[CURRENT_SPRITES_XPOS + SLOT] += ram[CURRENT_SPRITES_FLAGS + SLOT] & FACING_RIGHT ? 3 : -3;
            if (!--ram[CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES + SLOT]) ram[CURRENT_SPRITES_HIT_BY_MAGIC + SLOT] = 0xFF;
        } else if (ram[CURRENT_SPRITES_BEHAVIORS + SLOT] == BEHAVIOR_FLASH) {
            flash_behavior();
        }
        drawn = true; /* CurrentSprite_UpdateState */
        SITES(SUGATA_DRAW, "faxanadu.sugata.blink");
    }
    fax_block_frame_end();
    fax_sugata_frame_end();
}
/* Sugata's script reaches the flash again. */
static void next_flash(void) {
    ram[CURRENT_SPRITES_BEHAVIORS + SLOT] = BEHAVIOR_FLASH;
    ram[CURRENT_SPRITES_FLAGS + SLOT] &= (uint8_t)~BEHAVIOR_READY;
    finished = 0;
}

static void scene(uint8_t hero_x, uint8_t shield, bool hold) {
    seams_room = active = -1;
    ticking = false;
    joined = known = true;
    asked = 0;
    memset(shift, 0, sizeof shift);
    model_reset();
    memset(ram, 0, sizeof ram);
    cpu.s = 0xF0;
    model_activate("faxanadu.block");
    model_activate("faxanadu.sugata-flash");
    model_routine(FLASH_FINISH, finish);
    model_routine(REDUCE_ENEMY_HP, reduce_enemy_hp);
    model_routine(INVISIBLE_DRAW, invisible);
    ram[PLAYER_POS_X] = hero_x;
    ram[PLAYER_FLAGS] = 0x40;
    ram[SELECTED_SHIELD] = shield;
    ram[HP_U] = 80;
    ram[DURATION_OINTMENT] = 0xFF; /* no Ointment */
    for (unsigned s = 0; s < SLOTS; ++s) ram[CURRENT_SPRITES_ENTITIES + s] = 0xFF;
    ram[CURRENT_SPRITES_ENTITIES + SLOT] = SUGATA;
    ram[CURRENT_SPRITES_XPOS + SLOT] = 120; /* its middle at 128 */
    ram[SPRITE_HP + SLOT] = SUGATA_HP;
    ram[CURRENT_SPRITES_HIT_BY_MAGIC + SLOT] = 0xFF;
    next_flash();
    test_block_hold(hold);
    sound_count = 0;
    finished = 0;
    kills = 0;
    numbered = 0;
}
/* Through the warning to the flash: returns the frames until the damage site. */
static unsigned to_the_flash(void) {
    unsigned n = 0;
    while (!finished && ram[CURRENT_SPRITES_ENTITIES + SLOT] == SUGATA && n < 100) {
        frame();
        ++n;
    }
    return n;
}

/* The band of the slot's Sugata (no room's, or `room`'s), or none. */
static Band band_at(unsigned room) {
    Band *b = band_of(room, SLOT, false);
    return b ? *b : (Band){ 0 };
}
#define band band_at(NO_ROOM)

/* ---- the rules ---- */
static void the_reach(void) {
    /* The band is [88, 168) around a middle at 128; any of the hero's 16
     * pixels in it is a hit. */
    CHECK(!fax_sugata_reaches(72, 128) && fax_sugata_reaches(73, 128));
    CHECK(fax_sugata_reaches(120, 128) && fax_sugata_reaches(167, 128) && !fax_sugata_reaches(168, 128));
    CHECK(fax_sugata_reaches(0, 20) && !fax_sugata_reaches(200, 20));
    /* In 16 bits: a Sugata across a seam, its middle at -12 (room 5's 244). */
    CHECK(fax_sugata_reaches(4, -12) && fax_sugata_reaches(27, -12) && !fax_sugata_reaches(28, -12));
    CHECK(SUGATA_REACH == 40 && SUGATA_WARNING == 30 && SUGATA_REFLECTED == 12);
}

static void it_warns_then_flashes(void) {
    scene(20, NONE, false);
    unsigned hidden = 0;
    for (unsigned f = 1; f <= SUGATA_WARNING; ++f) {
        frame();
        CHECK(!(ram[CURRENT_SPRITES_FLAGS + SLOT] & BEHAVIOR_READY) && !(ram[PPU_MASK] & 1)); /* nothing yet */
        CHECK(warn[NO_ROOM][SLOT] == f && band.kind == WARNING_BAND && band.center == 128);
        CHECK(drawn == !((SUGATA_WARNING - f) & 4)); /* blinking: hidden 4 frames in 8, shown at the end */
        hidden += !drawn;
    }
    CHECK(hidden == 14 && drawn && warnings == 1 && flashes == 0 && warned == 0);
    /* The flash: the game's first call, greyscale cleared at once, the band. */
    frame();
    CHECK(ram[CURRENT_SPRITES_FLAGS + SLOT] & BEHAVIOR_READY && ram[BEHAVIOR_DATA2 + SLOT] == 1);
    CHECK(!(ram[PPU_MASK] & 1) && flashes == 1 && warned == 1 && band.kind == FLASH_BAND && warn[NO_ROOM][SLOT] == 0 &&
          drawn);
    frame();
    CHECK(finished == 1 && out_of_reach == 1 && in_reach == 0);
    CHECK(band.kind == FLASH_BAND); /* it lingers a few frames */
    for (unsigned f = 0; f < FLASH_SHOWN; ++f) frame();
    CHECK(band.kind == NO_BAND);
    /* Every flash is warned. */
    next_flash();
    CHECK(to_the_flash() == SUGATA_WARNING + 2 && warnings == 2 && flashes == 2);
}

static void out_of_reach_it_misses(void) {
    scene(20, NONE, false);
    CHECK(to_the_flash() == SUGATA_WARNING + 2);
    CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && !(ram[STATUS_FLAG] & 2) && sound_count == 0);
    CHECK(ram[CURRENT_SPRITES_BEHAVIORS + SLOT] & 0x80 && finished == 1); /* finished as the game's */
    CHECK(last_distance == 28 - 128 && !last_reached);
    scene(220, NONE, false);
    to_the_flash();
    CHECK(ram[HP_U] == 80 && out_of_reach == 1);
}

static void in_reach_it_hurts(void) {
    scene(100, NONE, false);
    to_the_flash();
    CHECK(ram[HP_U] == 70 && ram[INVINCIBILITY] == 0x3C && ram[STATUS_FLAG] & 2 && last_sound() == 0x04);
    CHECK(in_reach == 1 && finished == 1 && last_reached);
    /* Holding Block without a shield: the same. */
    scene(100, NONE, true);
    to_the_flash();
    CHECK(ram[HP_U] == 70);
    /* The original's flash ignores invincibility: it does here too. */
    scene(100, NONE, false);
    ram[INVINCIBILITY] = 20;
    to_the_flash();
    CHECK(ram[HP_U] == 70);
}

static void block_stops_it(void) {
    static const uint8_t stoppers[] = { SMALL, LARGE, HELMET };
    for (unsigned i = 0; i < sizeof stoppers; ++i)
        for (int facing = 0; facing < 2; ++facing) { /* from any side */
            scene(100, stoppers[i], true);
            ram[PLAYER_FLAGS] = facing ? 0x40 : 0;
            to_the_flash();
            CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && sound_count == 1 && last_sound() == 0x0C);
            CHECK(finished == 1 && in_reach == 1 && reflected == 0 && test_block_flashes() == 1);
            CHECK(ram[SPRITE_HP + SLOT] == SUGATA_HP);
        }
}

static void the_magic_shield_reflects_it(void) {
    scene(100, MAGIC, true);
    to_the_flash();
    CHECK(ram[HP_U] == 80 && ram[SPRITE_HP + SLOT] == SUGATA_HP - SUGATA_REFLECTED && reflected == 1);
    CHECK(sound_count == 1 && last_sound() == 0x02 && finished == 1 && test_block_flashes() == 0);
    CHECK(killed_reported == 0); /* a hit, no kill: no kill's MP */
    CHECK(numbered == 1 && numbered_amount == SUGATA_REFLECTED && numbered_slot == SLOT && numbered_entity == SUGATA);
    CHECK(ram[CURRENT_SPRITES_HIT_BY_MAGIC + SLOT] == 0 && ram[CURRENT_SPRITES_HIT_BY_MAGIC_IFRAMES + SLOT] == 4);
    for (int f = 0; f < 4; ++f) frame(); /* knocked back, as a Deluge would, away from him */
    CHECK(ram[CURRENT_SPRITES_HIT_BY_MAGIC + SLOT] == 0xFF && ram[CURRENT_SPRITES_XPOS + SLOT] == 120 + 12);
    /* The hero on its right: knocked to the left. */
    scene(140, MAGIC, true);
    ram[CURRENT_SPRITES_FLAGS + SLOT] |= FACING_RIGHT; /* it faced him */
    to_the_flash();
    CHECK(reflected == 1 && !(ram[CURRENT_SPRITES_FLAGS + SLOT] & FACING_RIGHT));
    for (int f = 0; f < 4; ++f) frame();
    CHECK(ram[CURRENT_SPRITES_XPOS + SLOT] == 120 - 12);
    scene(100, MAGIC, true);
    to_the_flash();
    for (int f = 0; f < 4; ++f) frame();
    /* A second one kills it, through the game's death code. */
    next_flash();
    to_the_flash();
    CHECK(ram[CURRENT_SPRITES_ENTITIES + SLOT] == DEATH && kills == 1 && reflected == 2 && ram[HP_U] == 80);
    CHECK(numbered == 2 && numbered_amount == SUGATA_REFLECTED && numbered_entity == SUGATA); /* the kill's too */
    CHECK(killed_reported == 1 && killed_entity == SUGATA); /* and its MP, which no hook gave */
    CHECK(!(ram[PPU_MASK] & 1) && band.kind == NO_BAND);    /* its death ends the band, as the game's ends greyscale */
    CHECK(sounds[sound_count - 2] == 0x02 && last_sound() == 0x03);
    /* Out of reach, the Magic Shield has nothing to send back. */
    scene(20, MAGIC, true);
    to_the_flash();
    CHECK(ram[SPRITE_HP + SLOT] == SUGATA_HP && reflected == 0 && numbered == 0);
    /* Without Block held, it is a shield like the others: it hurts. */
    scene(100, MAGIC, false);
    to_the_flash();
    CHECK(ram[HP_U] == 70 && ram[SPRITE_HP + SLOT] == SUGATA_HP);
}

static void ointment_keeps_the_hp(void) {
    /* In reach under Ointment: the hit sound, iframes and knockback, no HP. */
    scene(100, NONE, false);
    ram[DURATION_OINTMENT] = 0x20;
    to_the_flash();
    CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0x3C && ram[STATUS_FLAG] & 2 && last_sound() == 0x04);
    CHECK(sound_count == 1 && finished == 1 && anointed == 1 && in_reach == 1);
    /* Its last frame ($00) still counts; once it is over ($FF), the 10 HP. */
    scene(100, NONE, false);
    ram[DURATION_OINTMENT] = 0x00;
    to_the_flash();
    CHECK(ram[HP_U] == 80 && anointed == 1);
    scene(100, NONE, false);
    ram[DURATION_OINTMENT] = 0xFF;
    to_the_flash();
    CHECK(ram[HP_U] == 70 && anointed == 0);
    /* Out of reach, nothing at all; Block takes it first, without knockback;
     * the Magic Shield still reflects. */
    scene(20, NONE, false);
    ram[DURATION_OINTMENT] = 0x20;
    to_the_flash();
    CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && sound_count == 0 && anointed == 0);
    scene(100, SMALL, true);
    ram[DURATION_OINTMENT] = 0x20;
    to_the_flash();
    CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && !(ram[STATUS_FLAG] & 2) && last_sound() == 0x0C);
    CHECK(anointed == 0 && test_block_flashes() == 1);
    scene(100, MAGIC, true);
    ram[DURATION_OINTMENT] = 0x20;
    to_the_flash();
    CHECK(ram[HP_U] == 80 && reflected == 1 && anointed == 0 && ram[SPRITE_HP + SLOT] == SUGATA_HP - SUGATA_REFLECTED);
}

static void each_feature_alone(void) {
    /* Block alone: the flash is the game's, full screen, anywhere, and Block stops it anywhere. */
    scene(20, SMALL, true);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.behavior", 0);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.greyscale", 0);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.hit", 0);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.blink", 0);
    enabled = false;
    frame();
    CHECK(ram[PPU_MASK] & 1); /* greyscale, with no warning */
    frame();
    CHECK(!(ram[PPU_MASK] & 1) && ram[HP_U] == 80 && last_sound() == 0x0C && test_block_flashes() == 1);
    scene(20, SMALL, false);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.behavior", 0);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.hit", 0);
    enabled = false;
    frame();
    frame();
    CHECK(ram[HP_U] == 70); /* the original: hit anywhere */
    scene(100, NONE, false);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.behavior", 0);
    nes_mod_set_function_hook_enabled("faxanadu.sugata.hit", 0);
    enabled = false;
    ram[DURATION_OINTMENT] = 0x20;
    frame();
    frame();
    CHECK(ram[HP_U] == 70); /* and through Ointment */
    /* Sugata's flash alone: in reach it hurts, held key or not. */
    scene(100, SMALL, true);
    nes_mod_set_function_hook_enabled("faxanadu.block.flash", 0);
    model_reset();
    model_activate("faxanadu.sugata-flash");
    to_the_flash();
    CHECK(ram[HP_U] == 70 && test_block_flashes() == 0);
}

static void the_band(void) {
    palette();
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 256; ++x) native[y * 256 + x] = hw_palette_argb[(x / 16 + y) % 13 + (y / 60) * 16];
    scene(20, NONE, false);
    int w = 0, h = 0;
    CHECK(fax_sugata_present(NULL, &w, &h) == NULL); /* no band: nothing drawn */
    for (unsigned f = 0; f < SUGATA_WARNING + 1; ++f) frame();
    CHECK(band.kind == FLASH_BAND);
    const uint32_t *p = fax_sugata_present(NULL, &w, &h);
    CHECK(p && w == 256 && h == 240);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 256; ++x) {
            uint32_t was = native[y * 256 + x], now = p[y * 256 + x];
            bool in = y >= 32 && x >= 88 && x < 168;
            if (!in) {
                CHECK(now == was);
                continue;
            }
            int e = (x / 16 + y) % 13 + (y / 60) * 16; /* the PPU's greyscale: entry & $30 */
            CHECK(now == hw_palette_argb[e & 0x30]);
        }
    /* Black is $0F, whose grey is $00. A color not in the palette takes the
     * grey nearest its brightness. */
    CHECK(grey(0xFF000000u) == hw_palette_argb[0x00] && grey(0xFFF0F0F0u) == 0xFFFFFFFFu);
    /* The warning dims the same band a third. */
    next_flash();
    scene(20, NONE, false);
    frame();
    p = fax_sugata_present(NULL, &w, &h);
    uint32_t was = native[100 * 256 + 100], now = p[100 * 256 + 100];
    for (int sh = 0; sh < 24; sh += 8) {
        unsigned c = was >> sh & 255;
        CHECK((now >> sh & 255) == c - c / 3);
    }
    CHECK(p[100 * 256 + 60] == native[100 * 256 + 60] && p[20 * 256 + 100] == native[20 * 256 + 100]);
    /* Seamless's canvas: the band follows the active room, its rows only. */
    for (int i = 0; i < 800 * 400; ++i) world[i] = 0xFF123456u;
    world_on = true;
    origin_x = 300;
    origin_y = 50;
    w = 800;
    h = 400;
    p = fax_sugata_present(world, &w, &h);
    CHECK(p != world && w == 800 && h == 400);
    CHECK(p[60 * 800 + 300 + 100] != 0xFF123456u && p[257 * 800 + 300 + 100] != 0xFF123456u);
    CHECK(p[258 * 800 + 300 + 100] == 0xFF123456u && p[40 * 800 + 300 + 100] == 0xFF123456u);
    CHECK(p[60 * 800 + 300 + 87] == 0xFF123456u && p[60 * 800 + 300 + 88] != 0xFF123456u);
    CHECK(p[60 * 800 + 300 + 168] == 0xFF123456u && p[60 * 800 + 300 + 167] != 0xFF123456u);
    /* A scene Seamless shows as the original's: no band on it. */
    world_on = false;
    w = 800;
    h = 400;
    CHECK(fax_sugata_present(world, &w, &h) == world);
    /* Clipped at the picture's edge. */
    ram[CURRENT_SPRITES_XPOS + SLOT] = 4;
    frame();
    p = fax_sugata_present(NULL, &w, &h);
    CHECK(w == 256 && p[100 * 256 + 0] != native[100 * 256 + 0] && p[100 * 256 + 52] == native[100 * 256 + 52]);
    /* Off: nothing. */
    model_reset();
    w = 256;
    h = 240;
    CHECK(fax_sugata_present(NULL, &w, &h) == NULL);
}

static void it_keeps_its_place(void) {
    /* Sugata gone mid-warning (killed, or the screen changed): the next one starts afresh. */
    scene(20, NONE, false);
    for (int f = 0; f < 10; ++f) frame();
    CHECK(warn[NO_ROOM][SLOT] == 10);
    ram[CURRENT_SPRITES_ENTITIES + SLOT] = DEATH;
    frame();
    CHECK(warn[NO_ROOM][SLOT] == 0 && band.kind == NO_BAND);
    /* Save states carry the warning and the band. */
    scene(20, NONE, false);
    for (int f = 0; f < 12; ++f) frame();
    uint8_t rec[RECORD + 8];
    int n = model_state_get("faxanadu.sugata-flash", rec, sizeof rec);
    unsigned at = 1 + NO_ROOM * SLOTS + SLOT;
    CHECK(n == RECORD && rec[0] == VERSION && rec[at] == 12 && rec[1 + WARNS] == WARNING_BAND);
    CHECK(rec[1 + WARNS + 1] == NO_ROOM && rec[1 + WARNS + 2] == SLOT && rec[1 + WARNS + 4] == 128);
    CHECK(model_state_get("faxanadu.sugata-flash", rec, 4) == -1);
    for (int f = 0; f < 10; ++f) frame();
    CHECK(model_state_set("faxanadu.sugata-flash", rec, n) && warn[NO_ROOM][SLOT] == 12 && band.kind == WARNING_BAND);
    CHECK(to_the_flash() == SUGATA_WARNING - 12 + 2);
    CHECK(model_state_set("faxanadu.sugata-flash", NULL, 0) && warn[NO_ROOM][SLOT] == 0 && band.kind == NO_BAND);
    rec[at] = SUGATA_WARNING + 1;
    CHECK(!model_state_set("faxanadu.sugata-flash", rec, n));
    rec[at] = 3;
    rec[1 + WARNS + 1] = NO_ROOM + 1;
    CHECK(!model_state_set("faxanadu.sugata-flash", rec, n));
    rec[1 + WARNS + 1] = NO_ROOM;
    rec[0] = 9;
    CHECK(!model_state_set("faxanadu.sugata-flash", rec, n) && !model_state_set("faxanadu.sugata-flash", rec, n - 1));
    /* A version 1 record (before Seamless's rooms had warnings) loads as none. */
    uint8_t old[RECORD_1] = { 1 };
    old[1 + SLOT] = 12;
    old[1 + SLOTS] = WARNING_BAND;
    old[2 + SLOTS] = SLOT;
    warn[NO_ROOM][SLOT] = 5;
    CHECK(model_state_set("faxanadu.sugata-flash", old, RECORD_1) && warn[NO_ROOM][SLOT] == 0 && band.kind == NO_BAND);
    old[0] = 3;
    CHECK(!model_state_set("faxanadu.sugata-flash", old, RECORD_1));
    /* Inside Seamless's isolated rooms, the hooks do nothing. */
    scene(100, NONE, false);
    isolated_scope = true;
    CHECK(SITES(FLASH_BEHAVIOR, "faxanadu.sugata.behavior") && SITES(FLASH_HIT, "faxanadu.sugata.hit"));
    isolated_scope = false;
    CHECK(warn[NO_ROOM][SLOT] == 0 && warn[LIVE][SLOT] == 0 && in_reach == 0);
}

/* ---- Seamless: a neighbouring room's Sugata ---- */

/* One tick of room 5's Sugata on the machine (isolated, Seamless ticking),
 * kept in its record, then the frame's end in the live game. */
static void tick(void) {
    isolated_scope = ticking = true;
    seams_room = NEIGHBOUR;
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    if (ram[CURRENT_SPRITES_ENTITIES + SLOT] == SUGATA) {
        if (ram[CURRENT_SPRITES_BEHAVIORS + SLOT] == BEHAVIOR_FLASH) flash_behavior();
        drawn = true;
        SITES(SUGATA_DRAW, "faxanadu.sugata.blink");
    }
    isolated_scope = ticking = false;
    seams_room = active;
    memcpy(records[NEIGHBOUR], ram, sizeof ram);
    fax_block_frame_end();
    fax_sugata_frame_end();
}
/* Room 5's Sugata at its x 236 (its middle at 244, the live room's -12), the
 * hero on room 4 at `hero` (his true place: in room 5's pixels, + 256). */
static void beside(uint8_t hero, uint8_t shield, bool hold) {
    scene(hero, shield, hold);
    active = LIVE;
    seams_room = LIVE;
    hero_true = hero + 256;
    ram[CURRENT_SPRITES_XPOS + SLOT] = 236;
    memcpy(records[NEIGHBOUR], ram, sizeof ram);
}
static unsigned to_the_wait(void) {
    unsigned n = 0;
    while (!waits && !finished && n < 100) {
        tick();
        ++n;
    }
    return n;
}
/* Seams' visitors' pass: the Sugata in a live slot (SLOT), its X 16 bits from
 * its side (-256), and the damage entered for it, from its behaviour's code. */
static void visit(void) {
    shift[SLOT] = -256;
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    cpu.x = SLOT;
    if (SITES(FLASH_HIT, "faxanadu.sugata.hit", "faxanadu.block.flash")) {
        sound(0x04);
        damage_hero(10);
        finish();
    }
    shift[SLOT] = 0;
}

static void a_neighbours_sugata(void) {
    /* It warns and blinks in its own tick, its band in its room. */
    beside(4, NONE, false);
    unsigned hidden = 0;
    for (unsigned f = 1; f <= SUGATA_WARNING; ++f) {
        tick();
        CHECK(warn[NEIGHBOUR][SLOT] == f && warn[LIVE][SLOT] == 0);
        CHECK(band_at(NEIGHBOUR).kind == WARNING_BAND && band_at(NEIGHBOUR).center == 244);
        hidden += !drawn;
    }
    CHECK(hidden == 14 && near_warnings == 1 && warnings == 0);
    /* The flash: its band in its room, then in reach of his true place (x 4
     * of room 4, room 5's 260) the damage waits for the live game: nothing
     * happens to him, and the behaviour comes back to it each tick. */
    tick();
    CHECK(band_at(NEIGHBOUR).kind == FLASH_BAND && near_flashes == 1 && flashes == 0);
    tick();
    CHECK(waits == 1 && asked == 1 && finished == 0 && ram[HP_U] == 80 && sound_count == 0);
    CHECK(ram[BEHAVIOR_DATA2 + SLOT] == 1 && (ram[CURRENT_SPRITES_FLAGS + SLOT] & BEHAVIOR_READY));
    CHECK(last_reached && last_distance == 260 + 8 - 244);
    tick();
    CHECK(waits == 2 && finished == 0 && ram[HP_U] == 80 && in_reach == 0);
    /* The live game's pass: in reach of the visitor's place, 10 HP. */
    visit();
    CHECK(ram[HP_U] == 70 && in_reach == 1 && finished == 1 && last_sound() == 0x04);
    CHECK(last_distance == 4 + 8 - (236 - 256 + 8));
    /* Out of reach of his true place, it ends in its tick, as the game's. */
    beside(60, NONE, false);
    to_the_wait();
    CHECK(waits == 0 && finished == 1 && near_out_of_reach == 1 && out_of_reach == 0 && ram[HP_U] == 80);
    /* A room not joined beside his (Seams says no), or above or below his row
     * (no true place): it ends there too. */
    beside(4, NONE, false);
    joined = false;
    to_the_wait();
    CHECK(waits == 0 && finished == 1 && asked == 1 && near_out_of_reach == 1);
    beside(4, NONE, false);
    known = false;
    to_the_wait();
    CHECK(waits == 0 && finished == 1 && asked == 0 && near_out_of_reach == 1);
    /* The Magic Shield, Block up, in the pass: it goes back on Sugata, which
     * is knocked away from him, to the left. */
    beside(4, MAGIC, true);
    to_the_wait();
    CHECK(waits == 1);
    visit();
    CHECK(ram[HP_U] == 80 && reflected == 1 && ram[SPRITE_HP + SLOT] == SUGATA_HP - SUGATA_REFLECTED);
    CHECK(!(ram[CURRENT_SPRITES_FLAGS + SLOT] & FACING_RIGHT) && last_pushed == -1);
    /* Block with the Small Shield, in the pass: Block's site stops it. */
    beside(4, SMALL, true);
    to_the_wait();
    visit();
    CHECK(ram[HP_U] == 80 && test_block_flashes() == 1 && last_sound() == 0x0C);
    /* Its band shows on Seamless's canvas in its room: room 5's origin. */
    beside(4, NONE, false);
    for (unsigned f = 0; f < SUGATA_WARNING + 1; ++f) tick();
    CHECK(band_at(NEIGHBOUR).kind == FLASH_BAND);
    for (int i = 0; i < 800 * 400; ++i) world[i] = 0xFF123456u;
    world_on = true;
    origin_x = 300;
    origin_y = 50;
    int w = 800, h = 400;
    const uint32_t *p = fax_sugata_present(world, &w, &h);
    CHECK(p != world);
    int left = 300 - 256 + 244 - SUGATA_REACH, right = 300 - 256 + 244 + SUGATA_REACH; /* 248 to 328 */
    CHECK(p[100 * 800 + left] != 0xFF123456u && p[100 * 800 + left - 1] == 0xFF123456u);
    CHECK(p[100 * 800 + right - 1] != 0xFF123456u && p[100 * 800 + right] == 0xFF123456u);
    world_on = false;
    /* Gone from its room (killed there): its band and warning go too. */
    beside(4, NONE, false);
    for (int f = 0; f < 5; ++f) tick();
    CHECK(warn[NEIGHBOUR][SLOT] == 5);
    records[NEIGHBOUR][CURRENT_SPRITES_ENTITIES + SLOT] = DEATH;
    fax_sugata_frame_end();
    CHECK(warn[NEIGHBOUR][SLOT] == 0 && band_at(NEIGHBOUR).kind == NO_BAND);
    /* Two Sugatas, one in each room, each with its band. */
    beside(4, NONE, false);
    for (int f = 0; f < 3; ++f) tick();
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    ram[CURRENT_SPRITES_XPOS + SLOT] = 100;
    for (int f = 0; f < 3; ++f) frame();
    CHECK(band_at(NEIGHBOUR).kind == WARNING_BAND && band_at(LIVE).kind == WARNING_BAND);
    CHECK(band_at(LIVE).center == 108 && band_at(NEIGHBOUR).center == 244);
}

int main(void) {
    the_reach();
    it_warns_then_flashes();
    out_of_reach_it_misses();
    in_reach_it_hurts();
    block_stops_it();
    the_magic_shield_reflects_it();
    ointment_keeps_the_hp();
    each_feature_alone();
    the_band();
    it_keeps_its_place();
    a_neighbours_sugata();
    printf("sugata_test: %u checks passed\n", checks);
    return 0;
}
