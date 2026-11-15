/* Unit tests for Block (fax_block.c), without the emulator or a ROM. The
 * module runs against a model of the machine (model.c): RAM and the stack,
 * the hook sites as the engine runs them, and the game's code around them as
 * the disassembly has it: the sprite loop's shield check, enemy magic touching
 * the hero, the end of Sugata's flash, the controller bytes and the
 * interaction-script interpreter at a shop's END. So the tests check what the
 * game does, not the module's own arithmetic. */
#include "fax_block.c"
#include "model.h"
#include <unistd.h>

/* RAM the model's game code uses */
#define HP_U                 0x0431u
#define INVINCIBILITY        0x00ADu
#define STATUS_FLAG          0x00A5u   /* $02: knockback */
#define DURATION_OINTMENT    0x0427u
#define POS_Y                0x00A1u
#define SPRITE_X             0x00BAu
#define SPRITE_Y             0x00C2u
#define ENTITIES             0x02CCu
#define SPRITE_FLAGS         0x02DCu
#define SPRITE_BOX_TOP       0x03E3u
#define SPRITE_BOX_HEIGHT    0x03E5u
#define SHIELD_X             0x039Bu
#define SHIELD_Y             0x039Cu
#define BEHAVIOR_DATA2       0x02ECu
#define BEHAVIORS            0x02D4u
#define TEMP_0201            0x0201u
#define PORTRAIT             0x03C7u   /* IScript_PortraitID: $FF, none */
enum { MAGIC_SPRITE = 0x51, MAGIC_DAMAGE = 8, CATEGORY_MAGIC = 6, SLOT = 3 };
enum { SMALL, LARGE, MAGIC_SHIELD, HELMET, NONE = 0xFF };
enum { A = 0x80, B = 0x40, SELECT = 0x20, START = 0x10, UP = 0x08, DOWN = 0x04, LEFT = 0x02, RIGHT = 0x01 };

/* ---- the game's code around the sites ---- */

static void damage_hero(uint8_t hp) {           /* Player_SetDamagedBySprite and Player_ReduceHP */
    ram[INVINCIBILITY] = 0x3C;
    ram[STATUS_FLAG] |= 0x02;
    ram[HP_U] = ram[HP_U] > hp ? (uint8_t)(ram[HP_U] - hp) : 0;
}

/* Player_HandleHitByMagic ($87DC): the damage the shield lets through. */
static void handle_hit_by_magic(void) {
    if (!SITES(PLAYER_HANDLE_HIT_BY_MAGIC, "faxanadu.block.shield-hit")) return;
    uint8_t damage = MAGIC_DAMAGE;                         /* SPRITE_ENTITY_DAMAGES,Y */
    for (int y = ram[SELECTED_SHIELD]; y >= 0; --y) damage >>= 1;   /* LSR, DEY, BPL */
    if (damage) damage_hero(damage);
}
/* Player_HandleShieldHitByMagic after its JSR ($87CE): the shot is gone. */
static void shield_stops_magic(void) {
    ram[ENTITIES + ram[CURRENT_SPRITE_INDEX]] = 0xFF;
    cpu.a = SOUND_SHIELD;
    CycModRegs r = cpu;
    cyc_mod_call_commit(SOUND_PLAY_EFFECT, &r);
}
static void handle_shield_hit_by_magic(void) {        /* $87CB */
    handle_hit_by_magic();
    shield_stops_magic();
}
/* Player_HandleTouchEnemyMagic ($8999). */
static void handle_touch_enemy_magic(void) {
    if (!SITES(PLAYER_HANDLE_TOUCH_ENEMY_MAGIC, "faxanadu.block.touch-magic")) return;
    if (ram[INVINCIBILITY]) return;
    if (!(ram[DURATION_OINTMENT] & 0x80)) return;
    cpu.a = 0x04;                                          /* Player_HandleTouchEnemy: sound 4, full damage */
    CycModRegs r = cpu;
    cyc_mod_call_commit(SOUND_PLAY_EFFECT, &r);
    damage_hero(MAGIC_DAMAGE);
    ram[ENTITIES + ram[CURRENT_SPRITE_INDEX]] = 0xFF;
}

/* ---- the hero's sprites: Game_MainLoop's Screen_ResetSpritesForGamePlay,
 * Player_DrawShield, Player_DrawBody and Player_DrawWeapon ---- */

static uint8_t pose;                                       /* Maybe_Player_CalcAnimFrame: 0-3 upright, 4-6 a swing, 7 a ladder */
static const uint8_t SHIELD_SPRITE_OFFSETS_X[16] = { 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 8, 0, 0, 0 };
enum { HUD = 32, NOT_DRAWN = 0xF8, BODY_TILE = 0x00, SHIELD_TILE = 0x30, OTHER = 0x77 };

/* Sprite_Draw: cols x rows tiles at Arg_DrawSprite_PosX/Y, from the next slot
 * on (Sprites_IncNextSpriteSlot), as Y, tile, attributes, X. */
static void sprite_draw(unsigned cols, unsigned rows, uint8_t tile, uint8_t palette) {
    for (unsigned r = 0; r < rows; ++r)
        for (unsigned c = 0; c < cols; ++c) {
            uint8_t slot = ram[SPRITES_SPRITE_SLOT];
            uint16_t o = oam_of(slot, ram[SPRITES_START_SLOT_RANGE]);
            ram[o] = (uint8_t)(ram[ARG_DRAW_SPRITE_POS_Y] + HUD + 8 * r);
            ram[o + 1] = (uint8_t)(tile + 2 * c + r);
            ram[o + 2] = (uint8_t)(palette | (ram[PLAYER_FLAGS] & FACING_RIGHT));
            ram[o + 3] = (uint8_t)(ram[ARG_DRAW_SPRITE_POS_X] + 8 * c);
            ram[SPRITES_SPRITE_SLOT] = next_slot(slot);
        }
}
/* Player_DrawShield ($B982): nothing for the Battle Helmet or none; else the
 * box and the drawing place from the pose, and a column of 2 tiles in
 * palette 1. */
static void draw_shield(void) {
    if (!(ram[PORTRAIT] & 0x80) || ram[SELECTED_SHIELD] >= HELMET) return;
    ram[TEMP_00] = pose;
    unsigned i = pose * 2u + (ram[PLAYER_FLAGS] & FACING_RIGHT ? 1 : 0);
    ram[SHIELD_X] = (uint8_t)(ram[PLAYER_POS_X] + SHIELD_SPRITE_OFFSETS_X[i]);
    ram[SHIELD_Y] = (uint8_t)(ram[POS_Y] + 8);
    ram[ARG_DRAW_SPRITE_POS_X] = ram[SHIELD_X];
    ram[ARG_DRAW_SPRITE_POS_Y] = ram[SHIELD_Y];
    CHECK(SITES(DRAW_SHIELD_AT, "faxanadu.block.shield-draw"));
    sprite_draw(1, 2, SHIELD_TILE, 1);
}
/* Player_DrawBody ($EBEE): 2 columns of 4 tiles at the hero, in palette 0. */
static void draw_body(void) {
    CHECK(SITES(DRAW_BODY, "faxanadu.block.draw-body"));
    if (!(ram[PORTRAIT] & 0x80)) return;
    ram[ARG_DRAW_SPRITE_POS_X] = ram[PLAYER_POS_X];
    ram[ARG_DRAW_SPRITE_POS_Y] = ram[POS_Y];
    sprite_draw(2, 4, BODY_TILE, 0);
}
/* One pass's drawing: the slots start over (sprite 0 is the status bar's),
 * then the shield, the body and the weapon (none drawn: it is not swung). */
static void draw_hero(void) {
    for (unsigned i = 4; i < 256; i += 4) ram[SHADOW_OAM + i] = NOT_DRAWN;
    ram[SHADOW_OAM] = 23; ram[SHADOW_OAM + 1] = OTHER; ram[SHADOW_OAM + 2] = 3; ram[SHADOW_OAM + 3] = 8;
    ram[SPRITES_SPRITE_SLOT] = 1;
    draw_shield();
    draw_body();
    CHECK(SITES(DRAW_WEAPON, "faxanadu.block.draw-weapon"));
}
/* The drawn sprites with this tile: where, and in which palette. */
typedef struct { unsigned n; uint8_t x, y, palette[8]; } Drawn;
static Drawn drawn(uint8_t tile) {
    Drawn d = { 0, 0xFF, 0xFF, { 0 } };
    for (unsigned i = 0; i < 256; i += 4) {
        const uint8_t *o = ram + SHADOW_OAM + i;
        if (o[0] == NOT_DRAWN || (o[1] & 0xF0) != tile) continue;
        if (o[3] < d.x) d.x = o[3];
        if (o[0] < d.y) d.y = o[0];
        if (d.n < 8) d.palette[d.n] = o[2] & 3;
        ++d.n;
    }
    return d;
}
static bool all_in(const Drawn *d, uint8_t palette) {
    for (unsigned i = 0; i < d->n && i < 8; ++i) if (d->palette[i] != palette) return false;
    return d->n > 0;
}

/* One pass of the sprite loop over the magic in SLOT: Sprite_GetBounds,
 * Player_CheckShieldHitByMagic, CurrentSprite_CheckHitPlayer. The magic is
 * 16x16; the hero 16x32; his shield 8 wide at his front, 8 below his top,
 * where Player_DrawShield put its box this frame. */
static void sprite_loop(void) {
    draw_hero();
    ram[CURRENT_SPRITE_INDEX] = SLOT;
    if (ram[ENTITIES + SLOT] == 0xFF) return;
    ram[SPRITE_BOX_LEFT] = ram[SPRITE_X + SLOT]; ram[SPRITE_BOX_TOP] = ram[SPRITE_Y + SLOT];
    ram[SPRITE_BOX_WIDTH] = 15; ram[SPRITE_BOX_HEIGHT] = 15;
    bool swinging = ram[PLAYER_FLAGS] & 0x80;              /* animation frames 4-6 */
    if (ram[SELECTED_SHIELD] < 3 && !swinging) {
        uint8_t w = (uint8_t)(ram[SPRITE_BOX_WIDTH] + 7), h = (uint8_t)(ram[SPRITE_BOX_HEIGHT] + 0x0F);
        if ((uint8_t)(ram[SHIELD_X] + 7 - ram[SPRITE_BOX_LEFT]) < w &&
            (uint8_t)(ram[SHIELD_Y] + 0x0F - ram[SPRITE_BOX_TOP]) < h) {
            handle_shield_hit_by_magic();
        }
    }
    if (ram[ENTITIES + SLOT] == 0xFF) return;
    uint8_t w = (uint8_t)(ram[SPRITE_BOX_WIDTH] + 0x0B), h = (uint8_t)(ram[SPRITE_BOX_HEIGHT] + 0x1B);
    if ((uint8_t)(ram[PLAYER_POS_X] + 0x0B - ram[SPRITE_BOX_LEFT]) < w &&
        (uint8_t)(ram[POS_Y] + 0x1B - ram[SPRITE_BOX_TOP]) < h)
        handle_touch_enemy_magic();
}

/* A frame of the main loop, with the controller bytes the host latches. */
static uint8_t latched;
static void frame(uint8_t pad, bool main_loop_runs) {
    uint8_t buttons[2] = { pad, 0 };
    fax_block_input(buttons);
    latched = buttons[0];
    if (main_loop_runs)                                    /* the JSR at $DB68 pushes $DB6A */
        CHECK(SITES_FROM(GAME_LOOP_COUNTDOWN_ITEMS, MAIN_LOOP_COUNTDOWN_PUSHED + 1, "faxanadu.block.main-loop"));
    fax_block_frame_end();
}

/* The hero at x=100 on the floor facing right, a shot coming at him. */
static void scene(uint8_t shield, bool facing_right, int dx, int dy) {
    ram[PLAYER_POS_X] = 100; ram[POS_Y] = 144;
    ram[PLAYER_FLAGS] = facing_right ? FACING_RIGHT : 0;
    ram[SELECTED_SHIELD] = shield;
    ram[HP_U] = 80; ram[INVINCIBILITY] = 0; ram[STATUS_FLAG] = 0; ram[DURATION_OINTMENT] = 0xFF;
    ram[ENTITIES + SLOT] = MAGIC_SPRITE;
    ram[SPRITE_X + SLOT] = (uint8_t)(100 + dx); ram[SPRITE_Y + SLOT] = (uint8_t)(144 + dy);
    ram[PORTRAIT] = 0xFF;
    pose = ram[PLAYER_FLAGS] & 0x80 ? 4 : 0;
    sound_count = 0;
}
static void hold(bool on) { key_held = on; }

static void fresh(void) {
    model_reset();
    memset(ram, 0, sizeof ram);
    cpu.s = 0xF0;
    model_activate("faxanadu.block");
    model_routine(SHIELD_STOPS_MAGIC, shield_stops_magic);
    CHECK(enabled && hook_enabled("faxanadu.block.flash") && hook_enabled("faxanadu.block.end-script"));
}

/* ---- the rules ---- */

static void the_rules(void) {
    /* Anything the shield slot holds raises Block, the Battle Helmet too. */
    CHECK(shield_slot_item(SMALL) && shield_slot_item(LARGE) && shield_slot_item(MAGIC_SHIELD));
    CHECK(shield_slot_item(HELMET) && !shield_slot_item(NONE) && !shield_slot_item(4));
    /* From the front: the shot's middle level with or ahead of the hero's. */
    CHECK(from_the_front(100, true, 120, 15) && from_the_front(100, true, 101, 15));
    CHECK(!from_the_front(100, true, 80, 15) && from_the_front(100, false, 80, 15));
    CHECK(from_the_front(100, false, 100, 15) && !from_the_front(100, false, 110, 15));
    /* The tutorial's line, word-wrapped to 16 columns and at most 4 rows. */
    char out[80];
    CHECK(wrap("V", out) && !strcmp(out, "Hold V to raise\nyour shield\nagainst magic."));
    CHECK(wrap("B", out) && !strcmp(out, "Hold B to raise\nyour shield\nagainst magic."));
    CHECK(wrap("LEFT SHIFT", out) && !strcmp(out, "Hold LEFT SHIFT\nto raise your\nshield against\nmagic."));
    CHECK(wrap("RT", out) && !strcmp(out, "Hold RT to raise\nyour shield\nagainst magic."));
    CHECK(!wrap("KEYPAD ENTER", out));                         /* 5 rows: compose() falls back to "Block" */
    CHECK(wrap("Block", out) && !strcmp(out, "Hold Block to\nraise your\nshield against\nmagic."));
    uint8_t bytes[FAX_TRAVEL_SLOT_LEN];
    CHECK(encode("Hold V\nto", bytes));
    static const uint8_t want[] = { 'H', 'o', 'l', 'd', 0xFD, 'V', 0xFE, 't', 'o', 0xFF, 0xFF };
    CHECK(!memcmp(bytes, want, sizeof want) && bytes[FAX_TRAVEL_SLOT_LEN - 1] == 0xFF);
    CHECK(!encode("caf\xc3\xa9", bytes));
}

/* ---- standing still ---- */

static void it_stands_still(void) {
    fresh();
    scene(SMALL, true, 60, 0);
    frame(RIGHT, true);
    CHECK(latched == RIGHT);                                   /* walking, Block not held */
    hold(true);
    CHECK(fax_block_raised());
    frame(RIGHT | A | B | UP | DOWN | LEFT | START | SELECT, true);
    CHECK(latched == (START | SELECT));                        /* no walking, jumping, swinging or climbing */
    CHECK(stood == 1);
    /* Without a pass of the main loop in the last frame (a text box, a menu,
     * a screen change), the buttons are the player's. */
    frame(A, false);
    CHECK(latched == 0);                                       /* the last frame was in play */
    frame(A, false);
    CHECK(latched == A);
    frame(A, true);
    CHECK(latched == A);                                       /* this frame's input came before the pass */
    frame(A, true);
    CHECK(latched == 0);
    /* Played from somewhere else than Game_MainLoop (the ending): not in play. */
    frame(0, false);
    CHECK(SITES_FROM(GAME_LOOP_COUNTDOWN_ITEMS, 0xDC10, "faxanadu.block.main-loop"));
    fax_block_frame_end();
    frame(A, false);
    CHECK(latched == A);
    /* No shield, dead, released, or off: nothing. The Battle Helmet raises it. */
    ram[SELECTED_SHIELD] = NONE;
    frame(RIGHT, true);
    CHECK(latched == RIGHT && !fax_block_raised());
    ram[SELECTED_SHIELD] = HELMET;
    frame(RIGHT, true);
    CHECK(latched == 0 && fax_block_raised());
    ram[SELECTED_SHIELD] = MAGIC_SHIELD;
    frame(RIGHT, true);
    CHECK(latched == 0);
    ram[PLAYER_IS_DEAD] = 1;
    frame(RIGHT, true);
    CHECK(latched == RIGHT);
    ram[PLAYER_IS_DEAD] = 0;
    hold(false);
    frame(RIGHT, true);
    CHECK(latched == RIGHT);
    hold(true);
    isolated_scope = true;
    CHECK(!fax_block_raised());                                /* never inside Seamless's isolated rooms */
    isolated_scope = false;
    model_reset();
    CHECK(!fax_block_raised() && !hook_enabled("faxanadu.block.flash"));
}

/* ---- enemy magic ---- */

static void magic_on_the_shield(void) {
    /* The stock shield: the shot is gone with sound $0C, but half its 8 HP
     * (the Small Shield) gets through, with knockback. */
    fresh();
    scene(SMALL, true, 12, 8);
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 76 && ram[INVINCIBILITY] == 0x3C);
    CHECK(sound_count == 1 && last_sound() == SOUND_SHIELD && shield_saved == 0);
    scene(LARGE, true, 12, 8);
    sprite_loop();
    CHECK(ram[HP_U] == 78);
    scene(MAGIC_SHIELD, true, 12, 8);
    sprite_loop();
    CHECK(ram[HP_U] == 79);
    /* Raised: nothing gets through. */
    hold(true);
    for (uint8_t shield = SMALL; shield <= MAGIC_SHIELD; ++shield) {
        scene(shield, true, 12, 8);
        sprite_loop();
        CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && !(ram[STATUS_FLAG] & 2));
        CHECK(sound_count == 1 && last_sound() == SOUND_SHIELD);
    }
    CHECK(shield_saved == 3 && magic_blocked == 0);
}

static void magic_on_the_hero(void) {
    /* A shot at his head misses the shield and takes the full 8 HP. */
    fresh();
    scene(SMALL, true, 10, -10);
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 72 && last_sound() == 0x04);
    /* Raised, from the front: stopped as the shield stops it. */
    hold(true);
    scene(SMALL, true, 10, -10);
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 80 && sound_count == 1 && last_sound() == SOUND_SHIELD);
    CHECK(magic_blocked == 1);
    /* Facing left, the shot from the left is the front, too. */
    scene(LARGE, false, -10, -10);
    sprite_loop();
    CHECK(ram[HP_U] == 80 && magic_blocked == 2);
    /* Even while invincible it is stopped, not passed through. */
    scene(SMALL, true, 10, -10);
    ram[INVINCIBILITY] = 20;
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 80 && magic_blocked == 3);
}

static void not_from_behind(void) {
    fresh();
    hold(true);
    scene(SMALL, false, 10, 0);                 /* facing left, the shot comes from the right */
    sprite_loop();
    CHECK(ram[HP_U] == 72 && last_sound() == 0x04 && magic_blocked == 0 && shield_saved == 0);
    /* No shield: Block does nothing. */
    scene(NONE, true, 10, 0);
    sprite_loop();
    CHECK(ram[HP_U] == 72 && magic_blocked == 0);
    /* The Battle Helmet from behind: nothing either. */
    scene(HELMET, false, 10, 0);
    sprite_loop();
    CHECK(ram[HP_U] == 72 && magic_blocked == 0);
    /* Off: as the game was. */
    model_reset();
    scene(SMALL, true, 10, -10);
    sprite_loop();
    CHECK(ram[HP_U] == 72);
}

static void the_battle_helmet(void) {
    /* In the original it never stops magic: the shield check skips it, and
     * a shot at the shield's height takes the full 8 HP. */
    fresh();
    scene(HELMET, true, 10, 8);
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 72 && last_sound() == 0x04 && shield_saved == 0);
    /* Raised, it stops magic from the front, at any height, through the
     * touch handler, with the shield's sound. */
    hold(true);
    scene(HELMET, true, 10, 8);
    sprite_loop();
    CHECK(ram[ENTITIES + SLOT] == 0xFF && ram[HP_U] == 80 && sound_count == 1 && last_sound() == SOUND_SHIELD);
    scene(HELMET, false, -10, -10);
    sprite_loop();
    CHECK(ram[HP_U] == 80 && magic_blocked == 2 && shield_saved == 0);
}

/* ---- what Block looks like ---- */

static void the_shield_is_raised(void) {
    /* Where it goes: 6 higher and 2 further the way he faces, short of the
     * screen's edges. */
    uint8_t x = 108, y = 152;
    raise_shield(&x, &y, true);
    CHECK(x == 110 && y == 146);
    x = 100; y = 152;
    raise_shield(&x, &y, false);
    CHECK(x == 98 && y == 146);
    x = 1; y = 5;
    raise_shield(&x, &y, false);
    CHECK(x == 0 && y == 0);
    x = 254; y = 6;
    raise_shield(&x, &y, true);
    CHECK(x == 255 && y == 0);
    x = 253;
    raise_shield(&x, &y, true);
    CHECK(x == 255);

    /* Not held: where the game draws it, its box and its drawing alike. */
    fresh();
    for (uint8_t shield = SMALL; shield <= MAGIC_SHIELD; ++shield)
        for (int right = 0; right < 2; ++right) {
            hold(false);
            scene(shield, right, 60, 0);
            draw_hero();
            Drawn d = drawn(SHIELD_TILE);
            uint8_t at = right ? 108 : 100;
            CHECK(d.n == 2 && d.x == at && d.y == 152 + HUD && all_in(&d, 1));
            CHECK(ram[SHIELD_X] == at && ram[SHIELD_Y] == 152);
            /* Held: drawn before his face and out in front; the box stays. */
            hold(true);
            draw_hero();
            d = drawn(SHIELD_TILE);
            CHECK(d.n == 2 && d.x == (right ? 110 : 98) && d.y == 146 + HUD && all_in(&d, 1));
            CHECK(ram[SHIELD_X] == at && ram[SHIELD_Y] == 152);
            Drawn body = drawn(BODY_TILE);
            CHECK(body.n == 8 && body.x == 100 && body.y == 144 + HUD && all_in(&body, 0));   /* no glint */
            /* Let go: back where the game draws it. */
            hold(false);
            draw_hero();
            d = drawn(SHIELD_TILE);
            CHECK(d.x == at && d.y == 152 + HUD);
        }
    CHECK(shields_raised == 6 && glints == 0);
    /* Walking and in a jump too (poses 1-3); not in a swing (4-6) or on a
     * ladder (7), whose shield poses stay as they are. */
    hold(true);
    for (uint8_t p = 0; p < 8; ++p) {
        scene(SMALL, false, 60, 0);
        pose = p;
        draw_hero();
        Drawn d = drawn(SHIELD_TILE);
        CHECK(p < 4 ? d.x == 98 && d.y == 146 + HUD : d.x == 100 + SHIELD_SPRITE_OFFSETS_X[p * 2] && d.y == 152 + HUD);
    }
    /* Drawn the same in an isolated machine (Seamless world's room edges),
     * which counts nothing. */
    unsigned raised = shields_raised;
    pose = 0;
    scene(LARGE, true, 60, 0);
    isolated_scope = true;
    draw_hero();
    isolated_scope = false;
    CHECK(drawn(SHIELD_TILE).x == 110 && shields_raised == raised);
    /* The magic tests above ran through this drawing: the box was the game's. */
    /* Off: as the game draws it. */
    model_reset();
    draw_hero();
    CHECK(drawn(SHIELD_TILE).x == 108 && drawn(SHIELD_TILE).y == 152 + HUD);
}

/* Draws of a glint: the body's palette in each of `n` passes. */
static void passes(unsigned n, uint8_t want[]) {
    for (unsigned i = 0; i < n; ++i) {
        draw_hero();
        Drawn body = drawn(BODY_TILE);
        CHECK(body.n == 8);
        want[i] = all_in(&body, 1) ? 1 : all_in(&body, 0) ? 0 : 9;
    }
}

static void a_glint_where_nothing_is_raised(void) {
    static const uint8_t GLINT[16] = { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 };
    uint8_t seen[16];
    /* The Battle Helmet: no shield drawn. Block going up makes the body glint
     * in the shield's palette, twice, 4 draws each; sprite 0 keeps its own. */
    fresh();
    pose = 0;
    scene(HELMET, true, 60, 0);
    passes(4, seen);
    CHECK(!memcmp(seen, (uint8_t[4]){ 0, 0, 0, 0 }, 4) && drawn(SHIELD_TILE).n == 0);
    hold(true);
    passes(16, seen);
    CHECK(!memcmp(seen, GLINT, 16) && glints == 1);
    CHECK((ram[SHADOW_OAM + 2] & 3) == 3);
    /* Held on, nothing more. Let go and up again: another. */
    passes(4, seen);
    CHECK(!memcmp(seen, (uint8_t[4]){ 0, 0, 0, 0 }, 4));
    hold(false);
    passes(1, seen);
    hold(true);
    passes(2, seen);
    CHECK(seen[0] == 1 && seen[1] == 1 && glints == 2);
    /* Let go in the middle: it stops at once. */
    hold(false);
    passes(1, seen);
    CHECK(seen[0] == 0);
    hold(true);
    passes(1, seen);
    CHECK(seen[0] == 1 && glints == 3);
    /* The sprite slots the game rotates: the other range draws the same. */
    hold(false);
    passes(1, seen);
    ram[SPRITES_START_SLOT_RANGE] = 0x20;
    hold(true);
    passes(16, seen);
    CHECK(!memcmp(seen, GLINT, 16) && (ram[SHADOW_OAM + 2] & 3) == 3);
    ram[SPRITES_START_SLOT_RANGE] = 0;
    /* A state loaded with Block held: no glint for a button held through it. */
    hold(false);
    passes(1, seen);
    hold(true);
    passes(2, seen);
    fax_block_loaded();
    passes(4, seen);
    CHECK(!memcmp(seen, (uint8_t[4]){ 0, 0, 0, 0 }, 4));
    /* In an isolated machine: the live glint's draw, and nothing counted. */
    hold(false);
    passes(1, seen);
    hold(true);
    passes(1, seen);
    unsigned left = glint, count = glints;
    isolated_scope = true;
    passes(1, seen);
    isolated_scope = false;
    CHECK(seen[0] == 1 && glint == left && glints == count);

    /* A shield: raised, so no glint. */
    fresh();
    scene(SMALL, true, 60, 0);
    passes(1, seen);
    hold(true);
    passes(8, seen);
    CHECK(!memcmp(seen, (uint8_t[8]){ 0 }, 8) && glints == 0 && shields_raised == 8);
    /* A shield in a swing or on a ladder, where it is not raised: a glint. */
    for (uint8_t p = 4; p < 8; ++p) {
        hold(false);
        scene(MAGIC_SHIELD, false, 60, 0);
        pose = p;
        passes(1, seen);
        hold(true);
        passes(16, seen);
        CHECK(!memcmp(seen, GLINT, 16) && drawn(SHIELD_TILE).x == 100 + SHIELD_SPRITE_OFFSETS_X[p * 2]);
    }
    CHECK(glints == 4);
    /* No shield, or off: nothing. */
    hold(false);
    pose = 0;
    scene(NONE, true, 60, 0);
    passes(1, seen);
    hold(true);
    passes(4, seen);
    CHECK(!memcmp(seen, (uint8_t[4]){ 0 }, 4) && glints == 4);
    model_reset();
    scene(HELMET, true, 60, 0);
    hold(true);
    passes(4, seen);
    CHECK(!memcmp(seen, (uint8_t[4]){ 0 }, 4));
}

/* ---- Sugata's flash, without Sugata's flash's feature ---- */

static unsigned finished;
static void flash_finish(void) {                /* $AB60: LDX CurrentSpriteIndex, JMP Sprite_FinishBehavior */
    cpu.x = ram[CURRENT_SPRITE_INDEX];
    ram[BEHAVIORS + cpu.x] |= 0x80;
    ++finished;
}
/* $AB44 to the end: sound 4, iframes, knockback, 10 HP, finish. */
static void flash_damage(void) {
    if (!SITES(FLASH_HIT, "faxanadu.sugata.hit", "faxanadu.block.flash")) return;
    cpu.a = 0x04;
    CycModRegs r = cpu;
    cyc_mod_call_commit(SOUND_PLAY_EFFECT, &r);
    damage_hero(10);
    flash_finish();
}

static void the_flash_is_blocked_from_anywhere(void) {
    fresh();
    model_routine(FLASH_FINISH, flash_finish);
    ram[CURRENT_SPRITE_INDEX] = 5;
    for (int side = 0; side < 2; ++side) {
        scene(SMALL, side, 0, 0);
        finished = 0;
        flash_damage();
        CHECK(ram[HP_U] == 70 && finished == 1 && last_sound() == 0x04);   /* the game's flash */
    }
    hold(true);
    for (uint8_t shield = SMALL; shield <= MAGIC_SHIELD; ++shield)
        for (int side = 0; side < 2; ++side) {
            scene(shield, side, 0, 0);
            finished = 0;
            flash_damage();
            CHECK(ram[HP_U] == 80 && ram[INVINCIBILITY] == 0 && !(ram[STATUS_FLAG] & 2));
            CHECK(finished == 1 && ram[BEHAVIORS + 5] & 0x80 && sound_count == 1 && last_sound() == SOUND_SHIELD);
        }
    CHECK(flashes_blocked == 6);
    scene(HELMET, true, 0, 0);
    flash_damage();
    CHECK(ram[HP_U] == 80 && flashes_blocked == 7 && last_sound() == SOUND_SHIELD);
    scene(NONE, true, 0, 0);
    flash_damage();
    CHECK(ram[HP_U] == 70 && flashes_blocked == 7);
}

/* ---- the tutorial ---- */

/* The interaction-script interpreter (bank 12), as far as a shop goes:
 * IScripts_InvokeNextAction reads the action at IScriptOrCHRAddr +
 * IScriptOffset; $03 shows a message (B on it ends the script), $00 ends.
 * Every end is IScriptAction_EndScript. Returns the messages shown. */
#define SHOP_SCRIPT 0xA390u     /* in the model's bank 12: 03 $02 ("Thank you for shopping"), 00 */
static uint8_t shown[8];
static unsigned shown_count;
static bool box_open, dismiss;
static uint16_t next_at(void) {
    return (uint16_t)((ram[ISCRIPT_ADDR] | ram[ISCRIPT_ADDR + 1] << 8) + ram[ISCRIPT_OFFSET]);
}
static void end_script(void);
static void invoke_next_action(void) {
    for (;;) {
        uint16_t at = next_at();
        uint8_t action = rom_read(12, at);
        if (action == 0x00) { end_script(); return; }
        CHECK(action == 0x03);
        uint8_t message = rom_read(12, (uint16_t)(at + 1));
        ram[ISCRIPT_OFFSET] = (uint8_t)(ram[ISCRIPT_OFFSET] + 2);
        shown[shown_count++] = message;
        if (dismiss) { dismiss = false; end_script(); return; }   /* B: straight to EndScript */
    }
}
static void end_script(void) {
    if (!SITES(ISCRIPT_ACTION_END_SCRIPT, "faxanadu.block.end-script")) return;
    box_open = false;
}
/* A shop: the purchase (Player_AddToInventory), then the rest of its script. */
static void buy(uint8_t shield, bool b_on_thanks) {
    mapped = 12;
    box_open = true;
    shown_count = 0;
    ram[SHIELD_INVENTORY + ram[NUMBER_OF_SHIELDS]] = shield;
    ram[NUMBER_OF_SHIELDS]++;
    ram[ISCRIPT_ADDR] = (uint8_t)SHOP_SCRIPT; ram[ISCRIPT_ADDR + 1] = SHOP_SCRIPT >> 8; ram[ISCRIPT_OFFSET] = 0;
    dismiss = b_on_thanks;
    invoke_next_action();
    CHECK(!box_open);
    mapped = 14;
    frame(0, true);                         /* the main loop goes on */
}
/* Message $C3, as Messages_Load would find it in bank 13. */
static void message_c3(char *out) {
    size_t n = 0;
    for (uint16_t a = FAX_TRAVEL_SLOT; rom_read(13, a) != 0xFF; ++a) {
        uint8_t v = rom_read(13, a);
        out[n++] = v == 0xFD ? ' ' : v == 0xFE ? '\n' : (char)v;
    }
    out[n] = 0;
}
static void shop_fresh(void) {
    fresh();
    model_routine(ISCRIPTS_INVOKE_NEXT_ACTION, invoke_next_action);
    memset(rom[12], 0xFF, sizeof rom[12]);
    memset(rom[13], 0xFF, sizeof rom[13]);
    rom[12][SHOP_SCRIPT - 0x8000] = 0x03; rom[12][SHOP_SCRIPT - 0x8000 + 1] = 0x02; rom[12][SHOP_SCRIPT - 0x8000 + 2] = 0x00;
    ram[SELECTED_SHIELD] = NONE;
    frame(0, true);                         /* in play, owning no shield */
}

static void the_first_shield_teaches_once(void) {
    char text[80];
    shop_fresh();
    CHECK(!owned && !taught);
    buy(SMALL, false);
    CHECK(shown_count == 2 && shown[0] == 0x02 && shown[1] == FAX_TRAVEL_SLOT_ID);   /* thanks, then the line */
    message_c3(text);
    CHECK(!strcmp(text, "Hold V to raise\nyour shield\nagainst magic."));
    CHECK(rom_read(12, FAX_TRAVEL_SAY) == 0x03 && rom_read(12, FAX_TRAVEL_SAY + 1) == 0xC3 &&
          rom_read(12, FAX_TRAVEL_SAY + 2) == 0x00);
    CHECK(taught && tutorials == 1 && phase == QUIET && owned);
    CHECK(overrides_used() == 3 + FAX_TRAVEL_SLOT_LEN);       /* exactly Guru travel's SAY step and slot */
    /* Another shield: the thanks alone. */
    buy(LARGE, false);
    CHECK(shown_count == 1 && tutorials == 1);
    /* Taught is kept: with no shield again, a new one says nothing. */
    ram[NUMBER_OF_SHIELDS] = 0;
    frame(0, true);
    CHECK(!owned);
    buy(MAGIC_SHIELD, false);
    CHECK(shown_count == 1 && tutorials == 1);
}

static void b_on_the_thanks_still_teaches(void) {
    shop_fresh();
    buy(SMALL, true);
    CHECK(shown_count == 2 && shown[1] == FAX_TRAVEL_SLOT_ID && tutorials == 1);
    /* B on our line ends the script too. */
    shop_fresh();
    mapped = 12;
    ram[SHIELD_INVENTORY] = SMALL; ram[NUMBER_OF_SHIELDS] = 1;
    ram[ISCRIPT_ADDR] = (uint8_t)SHOP_SCRIPT; ram[ISCRIPT_ADDR + 1] = SHOP_SCRIPT >> 8; ram[ISCRIPT_OFFSET] = 2;
    box_open = true; shown_count = 0; dismiss = true;
    end_script();                               /* the shop's END: our line, dismissed with B */
    CHECK(!box_open && shown_count == 1 && shown[0] == FAX_TRAVEL_SLOT_ID && phase == QUIET && tutorials == 1);
    mapped = 14;
    /* Whatever happened in a script, a main-loop pass after it is quiet. */
    phase = SAYING;
    frame(0, true);
    CHECK(phase == QUIET && owned);
}

static void not_for_a_shield_already_owned(void) {
    /* The hero owned the Large Shield before the script: no line. */
    shop_fresh();
    ram[SHIELD_INVENTORY] = LARGE; ram[NUMBER_OF_SHIELDS] = 1;
    frame(0, true);
    buy(SMALL, false);
    CHECK(shown_count == 1 && tutorials == 0 && !taught);
    /* Nor one in hand. */
    shop_fresh();
    ram[SELECTED_SHIELD] = MAGIC_SHIELD;
    frame(0, true);
    buy(SMALL, false);
    CHECK(tutorials == 0);
    /* Owning the Battle Helmet counts: a shield after it teaches nothing. */
    shop_fresh();
    ram[SHIELD_INVENTORY] = HELMET; ram[NUMBER_OF_SHIELDS] = 1;
    frame(0, true);
    buy(SMALL, false);
    CHECK(shown_count == 1 && tutorials == 0);
    /* Scripts that give nothing: nothing. */
    shop_fresh();
    mapped = 12; box_open = true;
    ram[ISCRIPT_ADDR] = (uint8_t)SHOP_SCRIPT; ram[ISCRIPT_ADDR + 1] = SHOP_SCRIPT >> 8; ram[ISCRIPT_OFFSET] = 0;
    invoke_next_action();
    mapped = 14;
    CHECK(!box_open && tutorials == 0);
    /* Off: nothing. */
    shop_fresh();
    model_reset();
    buy(SMALL, false);
    CHECK(shown_count == 1 && tutorials == 0);
}

/* Player_PickUpBattleHelmet: IScript $8C ("You got the Battle Helmet"),
 * then the helmet into ShieldInventory. */
#define GOT_HELMET_MESSAGE 0xB6
static void pick_up_battle_helmet(void) {
    mapped = 12;
    box_open = true;
    shown_count = 0;
    ram[ISCRIPT_ADDR] = (uint8_t)ISCRIPT_GOT_BATTLE_HELMET; ram[ISCRIPT_ADDR + 1] = ISCRIPT_GOT_BATTLE_HELMET >> 8;
    ram[ISCRIPT_OFFSET] = 1;                 /* after the entity byte */
    invoke_next_action();
    CHECK(!box_open);
    mapped = 14;
    ram[SHIELD_INVENTORY + ram[NUMBER_OF_SHIELDS]] = HELMET;
    ram[NUMBER_OF_SHIELDS]++;
    frame(0, true);
}

static void the_battle_helmet_teaches_too(void) {
    char text[80];
    /* Picked up as the first shield-slot item: the line follows the game's. */
    shop_fresh();
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 1] = 0x03;   /* shown here as SHOW_MESSAGE */
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 2] = GOT_HELMET_MESSAGE;
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 3] = 0x00;
    pick_up_battle_helmet();
    CHECK(shown_count == 2 && shown[0] == GOT_HELMET_MESSAGE && shown[1] == FAX_TRAVEL_SLOT_ID);
    message_c3(text);
    CHECK(!strcmp(text, "Hold V to raise\nyour shield\nagainst magic.") && tutorials == 1 && taught && owned);
    /* With a shield already: only the game's line. */
    shop_fresh();
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 1] = 0x03;
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 2] = GOT_HELMET_MESSAGE;
    rom[12][ISCRIPT_GOT_BATTLE_HELMET - 0x8000 + 3] = 0x00;
    ram[SHIELD_INVENTORY] = SMALL; ram[NUMBER_OF_SHIELDS] = 1;
    frame(0, true);
    pick_up_battle_helmet();
    CHECK(shown_count == 1 && tutorials == 0);
}

static void taught_is_remembered(void) {
    char dir[] = "/tmp/block_test_XXXXXX", here[1024];
    CHECK(mkdtemp(dir) && getcwd(here, sizeof here));
    CHECK(chdir(dir) == 0);
    saves_on = true;
    shop_fresh();
    CHECK(!taught);
    buy(SMALL, false);
    FILE *f = fopen("faxanadu_block.txt", "r");
    char line_in[32] = "";
    CHECK(f && fgets(line_in, sizeof line_in, f) && !strcmp(line_in, "block.taught\n") && !fgets(line_in, 32, f));
    fclose(f);
    /* The next start reads it: a first shield then says nothing. */
    shop_fresh();
    CHECK(taught);
    buy(SMALL, false);
    CHECK(shown_count == 1 && tutorials == 0);
    remove("faxanadu_block.txt");
    shop_fresh();
    CHECK(!taught);
    /* Another field kept there is kept, and is not this one. */
    f = fopen("faxanadu_block.txt", "w");
    CHECK(f && fputs("other.field\r\nblock.taughtX\n", f) >= 0 && fclose(f) == 0);
    shop_fresh();
    CHECK(!taught);
    buy(SMALL, false);
    CHECK(tutorials == 1 && remembered(TAUGHT) && remembered("other.field") && remembered("block.taughtX"));
    remove("faxanadu_block.txt");
    /* --no-save: taught for the session only. */
    saves_on = false;
    shop_fresh();
    buy(SMALL, false);
    CHECK(tutorials == 1 && fopen("faxanadu_block.txt", "r") == NULL);
    CHECK(chdir(here) == 0 && rmdir(dir) == 0);
}

static void save_states(void) {
    shop_fresh();
    uint8_t rec[8];
    CHECK(model_state_get("faxanadu.block", rec, sizeof rec) == 3);
    CHECK(rec[0] == 1 && rec[1] == 0 && rec[2] == QUIET);
    CHECK(model_state_get("faxanadu.block", rec, 2) == -1);
    /* Saved while the line shows (in the middle of the shop's script)... */
    buy(SMALL, false);
    phase = SAYING; owned = false;
    CHECK(model_state_get("faxanadu.block", rec, sizeof rec) == 3 && rec[2] == SAYING);
    /* ... and loaded after Guru travel wrote its own line into the slot. */
    for (unsigned i = 0; i < FAX_TRAVEL_SLOT_LEN; ++i)
        cyc_mod_set_prg_read_override(13u * 0x4000u + (FAX_TRAVEL_SLOT - 0x8000u + i), 0xFF, i < 4 ? 'G' : 0xFF);
    phase = QUIET; owned = true;
    CHECK(model_state_set("faxanadu.block", rec, 3));
    CHECK(phase == SAYING && !owned);
    fax_block_loaded();
    char text[80];
    message_c3(text);
    CHECK(!strcmp(text, "Hold V to raise\nyour shield\nagainst magic."));
    /* A state from before Block: nothing to say, and nothing to teach. */
    CHECK(model_state_set("faxanadu.block", NULL, 0) && phase == QUIET && owned);
    uint8_t bad[3] = { 2, 0, 0 }, worse[3] = { 1, 2, 0 };
    CHECK(!model_state_set("faxanadu.block", bad, 3) && !model_state_set("faxanadu.block", worse, 3));
    CHECK(!model_state_set("faxanadu.block", rec, 2));
}

int main(void) {
    the_rules();
    it_stands_still();
    magic_on_the_shield();
    magic_on_the_hero();
    not_from_behind();
    the_battle_helmet();
    the_flash_is_blocked_from_anywhere();
    the_shield_is_raised();
    a_glint_where_nothing_is_raised();
    the_first_shield_teaches_once();
    the_battle_helmet_teaches_too();
    b_on_the_thanks_still_teaches();
    not_for_a_shield_already_owned();
    taught_is_remembered();
    save_states();
    printf("block_test: %u checks passed\n", checks);
    return 0;
}
