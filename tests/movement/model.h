/* A model of the machine for the movement features' unit tests (falling pose,
 * forgiving jumps, the ladder fixes and holding on to ladders), without the
 * emulator or a ROM. Include it after the feature's .c file.
 *
 * It holds CPU RAM, the registers and one screen of blocks, and plays the
 * hero's update as bank 15 has it (chipx86's disassembly), instruction by
 * instruction where it matters: Player_CheckHandleVertMovement ($E2C8) with
 * its ladder, fall and jump code, Player_CheckHandleJump's horizontal movement
 * and knockback ($E174), Player_HandleIFrames, Player_CheckHandleAttack, and
 * both pose routines ($ECAC, $B927). Each hook site fires where the ROM has
 * it, with the registers the instruction before left; a branch after a site
 * goes the way the registers the hook leaves say. So the tests check what the
 * game does, not the modules' own arithmetic.
 *
 * What the model leaves out: walls (the screens have none where the hero
 * moves sideways), and the screens around this one (a test that reached them
 * fails). Blocks are '#' solid, 'H' ladder, '.' air. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL %s line %u: %s\n", __FILE__, __LINE__, #test);                                       \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* RAM */
enum {
    M_JOY = 0x16,
    M_NEW = 0x19,
    M_AREA = 0x24,
    M_FLIP = 0x29,
    M_SCREEN = 0x63,
    M_X_FRAC = 0x9D,
    M_X = 0x9E,
    M_MODIFIER = 0xA0,
    M_Y = 0xA1,
    M_TICK = 0xA3,
    M_FLAGS = 0xA4,
    M_STATUS = 0xA5,
    M_JC = 0xA6,
    M_ACCEL = 0xA9,
    M_ACCEL_U = 0xAA,
    M_IFRAMES = 0xAD,
    M_ATTACK_FRAME = 0xAE,
    M_ATTACK_TIMER = 0xAF,
    M_HANG = 0xB1,
    M_BLOCKS_RESULT = 0xB7,
    M_TITLE = 0x437,
};
/* Buttons, Player_Flags, Player_StatusFlag */
enum { B_RIGHT = 0x01, B_LEFT = 0x02, B_DOWN = 0x04, B_UP = 0x08, B_B = 0x40, B_A = 0x80 };
enum {
    F_JUMPING = 0x01,
    F_HOLDING = 0x02,
    F_FALLING = 0x04,
    F_CAN_CLIMB = 0x08,
    F_CLIMBING = 0x10,
    F_MOVING = 0x20,
    F_RIGHT = 0x40,
    F_ATTACKING = 0x80
};
enum { S_ATTACKING = 0x01, S_KNOCKBACK = 0x02, S_WING_BOOTS = 0x80 };
enum { FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_N = 0x80 };

static uint8_t ram[0x800];
static CycModRegs cpu = { .s = 0xF0, .p = 0x24 };
static char world[13][17];

/* PLAYER_JUMP_CURVE ($E4D6), PLAYER_ATTACK_ANIMATION_PHASES, the pose tables */
static const uint8_t curve[32] = { 8, 4, 4, 4, 4, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 4, 4, 4, 4, 8 };
static const uint8_t attack_phases[3] = { 8, 3, 8 }, attack_frames[3] = { 4, 5, 6 }, walk_frames[4] = { 0, 1, 2, 1 };
static const uint8_t accel_delta[4] = { 2, 4, 6, 8 };

/* ---- the host API the modules use ---- */
static bool isolated;
static uint8_t snapshot[0x800];
static unsigned isolated_calls;
uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) {
    if (addr >= 0x2000) return false;
    ram[addr & 0x7FF] = value;
    return true;
}
void cyc_mod_regs(CycModRegs *out) { *out = cpu; }
void cyc_mod_set_regs(const CycModRegs *in) {
    cpu.a = in->a;
    cpu.x = in->x;
    cpu.y = in->y;
    cpu.s = in->s;
    cpu.p = in->p;
}
bool cyc_mod_isolated(void) { return isolated; }
bool cyc_mod_isolate_begin(void) {
    if (isolated) return false;
    memcpy(snapshot, ram, sizeof ram);
    isolated = true;
    return true;
}
void cyc_mod_isolate_end(void) {
    memcpy(ram, snapshot, sizeof ram);
    isolated = false;
}
static uint8_t passable(uint8_t dir);
static void check_if_on_ladder(void);
/* Player_CheckIfPassable and Player_CheckIfOnLadder are modelled. */
bool cyc_mod_call(uint16_t routine, CycModRegs *regs) {
    if (!isolated || (routine != 0xE6C8 && routine != 0xE752)) return false;
    ++isolated_calls;
    if (routine == 0xE752) {
        check_if_on_ladder();
        return true;
    }
    regs->a = passable(regs->x);
    regs->p = (uint8_t)((regs->p & ~(FLAG_Z | FLAG_N)) | (regs->a ? 0 : FLAG_Z));
    return true;
}

typedef struct {
    const char *id;
    uint16_t addr;
    NESModFunctionEntryCallback cb;
    int on;
} Site;
static Site sites[16];
static unsigned nsites;
static NESModActivationCallback reset_cb, activate_cb;
static const char *plugin_id;
static NESModSavestateGet save_get;
static NESModSavestateSet save_set;
static NESModSavestateValidate save_validate;
static const char *save_id;
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) {
    if (!id || !*id || !cb || nsites == 16) return 0;
    sites[nsites++] = (Site){ id, addr, cb, 0 };
    return 1;
}
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    for (unsigned i = 0; i < nsites; i++)
        if (!strcmp(sites[i].id, id)) {
            sites[i].on = on;
            return 1;
        }
    return 0;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) {
    reset_cb = cb;
    return 1;
}
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) {
    plugin_id = id;
    activate_cb = cb;
    return 1;
}
void nes_mod_set_local_only(const char *name, int required) {
    (void)name;
    (void)required;
}
int nes_mod_register_savestate_hook(const char *id, NESModSavestateGet get, NESModSavestateSet set) {
    save_id = id;
    save_get = get;
    save_set = set;
    return 1;
}
int nes_mod_register_savestate_validator(const char *id, NESModSavestateValidate validate) {
    (void)id;
    save_validate = validate;
    return 1;
}

static inline bool site_on(uint16_t addr) {
    for (unsigned i = 0; i < nsites; i++)
        if (sites[i].addr == addr && sites[i].on) return true;
    return false;
}
/* The scheduler at a hook site: the enabled callbacks run before the
 * instruction; one that handles it returns from the routine. */
static bool site(uint16_t addr) {
    for (unsigned i = 0; i < nsites; i++)
        if (sites[i].addr == addr && sites[i].on && sites[i].cb(addr)) return true;
    return false;
}
static void set_nz(uint8_t value) {
    cpu.p = (uint8_t)((cpu.p & ~(FLAG_N | FLAG_Z)) | (value & 0x80) | (value ? 0 : FLAG_Z));
}

/* ---- the screen ---- */
static void load_screen(const char *const rows[13]) {
    for (int r = 0; r < 13; r++) {
        CHECK(strlen(rows[r]) == 16);
        memcpy(world[r], rows[r], 17);
    }
}
static char block(unsigned px, unsigned py) { return world[(py & 0xFF) >> 4][(px & 0xFF) >> 4]; }

/* Area_CanMoveImmediatelyRight with Arg_PixelPosY = py: the block at X+4,
 * and the next one when the hero straddles them (X & 15 in 4..11). */
static uint8_t move_immediately(uint8_t py) {
    uint8_t x = ram[M_X];
    uint8_t solid = block((uint8_t)(x + 4), py) == '#';
    if ((uint8_t)((x & 0x0F) - 4) < 8) solid |= block((uint8_t)(x + 4) + 16u, py) == '#';
    return ram[M_BLOCKS_RESULT] = solid;
}
/* Player_CheckIfPassable ($E6C8): X = direction; A (and Blocks_Result) 0 when passable. */
static uint8_t passable(uint8_t dir) {
    uint8_t y = ram[M_Y];
    if (dir < 2) return ram[M_BLOCKS_RESULT] = 0; /* no walls in the model */
    if (dir == 2) {
        uint8_t py = (uint8_t)(y - 1);
        if (py >= 0xF0) return ram[M_BLOCKS_RESULT] = 0; /* the row above the screen: open */
        return move_immediately(py);
    }
    uint8_t py = (uint8_t)(y + 0x20);
    CHECK(py < 0xD0); /* the screen below is not modelled */
    return move_immediately(py);
}

/* ---- the hero's update (bank 15) ---- */
static bool is_climbing(void) { /* Player_IsClimbing */
    uint8_t f = ram[M_FLAGS];
    return (f & F_CAN_CLIMB) && ((ram[M_X] & 0x0F) == 0 || !(f & F_FALLING)) && (f & F_CLIMBING);
}
static void check_if_on_ladder(void) { /* Player_CheckIfOnLadder ($E752) */
    uint8_t x = ram[M_X], y = ram[M_Y];
    bool straddle = (x & 0x0F) == 8;
    bool can = block((uint8_t)(x + 7), y) == 'H' || (straddle && block((uint8_t)(x + 7) + 16u, y) == 'H') ||
               block((uint8_t)(x + 7), (uint8_t)(y + 0x1F)) == 'H' ||
               (straddle && block((uint8_t)(x + 7) + 16u, (uint8_t)(y + 0x1F)) == 'H');
    uint8_t f = ram[M_FLAGS] & ~F_CAN_CLIMB;
    ram[M_FLAGS] = can ? (uint8_t)(f | F_CAN_CLIMB) : (uint8_t)(f & ~F_CLIMBING);
}
static void climb_up(void) { /* Player_CheckHandleClimbUp ($E301) */
    ram[M_FLAGS] &= 0xDA;
    cpu.x = 2; /* LDX #$02, JSR Player_CheckIfPassable */
    cpu.a = passable(2);
    set_nz(cpu.a);
    site(0xE30C); /* BNE @_return */
    if (!(cpu.p & FLAG_Z)) return;
    ram[M_TICK]++;
    if (ram[M_STATUS] & S_WING_BOOTS) ram[M_Y]--; /* flying: a pixel an update */
    else { /* climbing: $A0/256 of a pixel */ bool borrow = ram[M_MODIFIER] < 0xA0;
        ram[M_MODIFIER] -= 0xA0;
        ram[M_Y] -= borrow;
    }
    CHECK(ram[M_Y] < 0xF0); /* the screen above is not modelled */
}
static void climb_down(void) { /* Player_CheckHandleClimbDown ($E349) */
    ram[M_FLAGS] &= 0xDA;
    if (passable(3)) return;
    ram[M_TICK]++;
    unsigned sum = ram[M_MODIFIER] + (ram[M_STATUS] & S_WING_BOOTS ? 0x80u : 0xC0u);
    ram[M_MODIFIER] = (uint8_t)sum;
    ram[M_Y] = (uint8_t)(ram[M_Y] + (ram[M_STATUS] & S_WING_BOOTS ? 1 : 0) + (sum >> 8));
    CHECK(ram[M_Y] < 0xC1);
}
static void update_climb_or_jump(void) { /* Player_UpdateClimbOrJump ($E444) */
    if (!(ram[M_FLAGS] & F_JUMPING)) {
        cpu.x = ram[M_NEW]; /* LDX Joy1_ChangedButtonMask */
        set_nz(cpu.x);
        site(0xE44B); /* BPL Player_ClearJumpingAndHoldingToClimb */
        if (!(cpu.p & FLAG_N)) {
            ram[M_FLAGS] &= 0xFC;
            return;
        }
        if (is_climbing()) return;
        if (passable(2)) return;
        ram[M_FLAGS] |= F_JUMPING | F_HOLDING;
        ram[M_JC] = 0;
    }
    uint8_t i = ram[M_JC];
    if (i < 0x10) {                  /* @_jumpRising */
        CHECK(ram[M_Y] >= curve[i]); /* the screen above is not modelled */
        ram[M_Y] -= curve[i];
        if (passable(2)) { /* a block above: cap, start falling */
            uint8_t y = ram[M_Y];
            ram[M_Y] = (uint8_t)((y & 0xF0) + (y & 0x0F ? 0x10 : 0));
            ram[M_JC] = 0x0F;
        }
        ram[M_JC]++;
    } else { /* @_jumpFalling */
        ram[M_Y] += curve[i];
        if (passable(3)) { /* @_setLandedOnBlock */
            ram[M_Y] &= 0xF0;
            ram[M_FLAGS] &= ~F_JUMPING;
            return;
        }
        ram[M_JC]++;
    }
}
static void grab_climb_or_jump(void) { /* Player_GrabClimbLadderOrJump ($E43A) */
    ram[M_FLAGS] &= ~F_FALLING;
    ram[M_HANG] = 0;
    update_climb_or_jump();
}
static void ladder_or_jump(void) { /* Player_HandleVertLadderOrJump ($E399) */
    if (ram[M_JC] >= 0x20) ram[M_FLAGS] &= ~F_JUMPING;
    if (ram[M_FLAGS] & F_JUMPING) {
        grab_climb_or_jump();
        return;
    }
    cpu.a = passable(3); /* LDA #$03 (for LDX), JSR Player_CheckIfPassable */
    set_nz(cpu.a);
    site(0xE3B2); /* BEQ @_checkAdjacent */
    if (!(cpu.p & FLAG_Z)) {
        grab_climb_or_jump();
        return;
    }
    /* Area_CheckClimbAdjacentImpassable: false anywhere on screen; on to @_setFalling */
    site(0xE3D1);
    ram[M_FLAGS] |= F_FALLING;
    if (ram[M_FLAGS] & F_CAN_CLIMB) {
        grab_climb_or_jump();
        return;
    }
    if ((ram[M_STATUS] & S_WING_BOOTS) && (ram[M_JOY] & B_A)) ram[M_Y] += 1;
    else ram[M_Y] += 8;     /* @_climbDown: the fall step */
    CHECK(ram[M_Y] < 0xC1); /* the screen below is not modelled */
    cpu.a = passable(3);    /* @_checkCapFall */
    set_nz(cpu.a);
    site(0xE423); /* BEQ @_return */
    if (!(cpu.p & FLAG_Z)) ram[M_Y] &= 0xF0;
}
static void vert_movement(void) { /* Player_CheckHandleVertMovement ($E2C8) */
    site(0xE2C8);
    bool flying = (ram[M_STATUS] & S_WING_BOOTS) && (ram[M_JOY] & B_A);
    check_if_on_ladder();
    if (!flying) {
        if (!(ram[M_FLAGS] & F_CAN_CLIMB) || !(ram[M_JOY] & (B_UP | B_DOWN))) {
            ladder_or_jump();
            return;
        }
        ram[M_FLAGS] |= F_CLIMBING;
        if (ram[M_X] & 0x0F) {
            ladder_or_jump();
            return;
        }
    }
    if (ram[M_JOY] & B_DOWN) climb_down(); /* @_checkClimbUpDown */
    else if (ram[M_JOY] & B_UP) climb_up();
    else ladder_or_jump();
}
static void standard_accel(void) {
    ram[M_ACCEL] = 0xC0;
    ram[M_ACCEL_U] = 0;
}
static void update_accel(void) { /* Player_UpdateAcceleration */
    if (ram[M_STATUS] & S_KNOCKBACK) {
        ram[M_ACCEL] = 0;
        ram[M_ACCEL_U] = 8;
        return;
    }
    if ((ram[M_JOY] & (B_UP | B_DOWN)) && (ram[M_FLAGS] & F_CAN_CLIMB)) {
        standard_accel();
        return;
    }
    unsigned accel = ram[M_ACCEL] | ram[M_ACCEL_U] << 8;
    if (accel >= 0x180) return;
    accel += accel_delta[(ram[M_TITLE] >> 2) & 3];
    ram[M_ACCEL] = (uint8_t)accel;
    ram[M_ACCEL_U] = (uint8_t)(accel >> 8);
}
static void move(bool right) { /* Player_TryMoveRight / Player_TryMoveLeft */
    if (!(ram[M_FLAGS] & F_MOVING)) standard_accel();
    update_accel();
    ram[M_FLAGS] = (uint8_t)(right ? ram[M_FLAGS] | F_MOVING | F_RIGHT : (ram[M_FLAGS] & ~F_RIGHT) | F_MOVING);
    int x = ram[M_X] << 8 | ram[M_X_FRAC], d = ram[M_ACCEL_U] << 8 | ram[M_ACCEL];
    x = right ? x + d : x - d;
    CHECK(x >= 0 && x < 0xF100); /* the screens around are not modelled */
    ram[M_X] = (uint8_t)(x >> 8);
    ram[M_X_FRAC] = (uint8_t)x;
}
static void horizontal(void) { /* Player_CheckHandleJump ($E174) */
    uint8_t f = ram[M_FLAGS];
    if (ram[M_STATUS] & S_KNOCKBACK) { /* Player_HandleKnockback ($E153) */
        if (site(0xE153)) return;
        uint8_t facing = f & F_RIGHT;
        ram[M_FLAGS] = facing ^ F_RIGHT; /* every other flag is dropped here */
        move(!facing);
        ram[M_FLAGS] = (uint8_t)((ram[M_FLAGS] & ~F_RIGHT) | facing);
        return;
    }
    if (!(ram[M_STATUS] & S_WING_BOOTS) && (f & (F_JUMPING | F_FALLING))) {
        if (f & F_MOVING) {
            move(f & F_RIGHT);
            return;
        } /* in the air: momentum */
    } else {
        if (f & F_ATTACKING) {
            ram[M_FLAGS] &= ~F_MOVING;
            return;
        }
        if (ram[M_JOY] & (B_LEFT | B_RIGHT)) {
            ram[M_TICK]++;
            move(ram[M_JOY] & B_RIGHT);
            return;
        }
    }
    if ((ram[M_JOY] & (B_UP | B_DOWN)) && (ram[M_X] & 0x0F) && (f & F_CAN_CLIMB)) { /* line up with a ladder */
        ram[M_TICK]++;
        move((ram[M_X] & 0x0F) >= 8);
        return;
    }
    ram[M_FLAGS] &= ~F_MOVING;
}
static void iframes(void) { /* Player_HandleIFrames */
    if (ram[M_IFRAMES]) {
        uint8_t phase = --ram[M_IFRAMES];
        if (phase > 0x39) {
            standard_accel();
            return;
        }
        if (phase == 0x39) standard_accel();
    }
    ram[M_STATUS] &= ~S_KNOCKBACK;
}
static void attack(void) { /* Player_CheckHandleAttack */
    if (!(ram[M_FLAGS] & F_ATTACKING)) {
        if (is_climbing() || !(ram[M_NEW] & B_B)) {
            ram[M_STATUS] &= ~S_ATTACKING;
            return;
        }
        if (ram[M_STATUS] & S_ATTACKING) return;
        ram[M_FLAGS] |= F_ATTACKING;
        ram[M_STATUS] |= S_ATTACKING;
        ram[M_ATTACK_TIMER] = ram[M_ATTACK_FRAME] = 0;
    }
    if (++ram[M_ATTACK_TIMER] < attack_phases[ram[M_ATTACK_FRAME]]) return;
    ram[M_ATTACK_TIMER] = 0;
    if (ram[M_ATTACK_FRAME] + 1 >= 3) ram[M_FLAGS] &= ~F_ATTACKING;
    else ram[M_ATTACK_FRAME]++;
}

/* The pose routines, entered with the hook site first. */
static uint8_t body_frame(void) { /* Player_GetBodySpriteFrameOffset ($ECAC) */
    if (site(0xECAC)) return cpu.a;
    uint8_t f = ram[M_FLAGS];
    if (f & F_JUMPING) return f & F_ATTACKING ? attack_frames[ram[M_ATTACK_FRAME]] : 3;
    if (is_climbing()) {
        ram[M_FLIP] = (uint8_t)((ram[M_TICK] & 0x10) << 2);
        return 7;
    }
    if (f & F_ATTACKING) return attack_frames[ram[M_ATTACK_FRAME]];
    if ((ram[M_STATUS] & S_WING_BOOTS) && (ram[M_JOY] & B_A)) return 3;
    return walk_frames[f & F_MOVING ? (ram[M_TICK] >> 3) & 3 : 0];
}
static uint8_t gear_frame(void) { /* Maybe_Player_CalcAnimFrame ($B927, bank 14) */
    if (site(0xB927)) return cpu.a;
    uint8_t f = ram[M_FLAGS];
    if (f & F_JUMPING) return f & F_ATTACKING ? attack_frames[ram[M_ATTACK_FRAME]] : 3;
    if ((f >> 1) & 0x04) { /* LSR A, AND #$04: can climb */
        if (!(ram[M_X] & 0x0F) || !(f & F_FALLING))
            if (f & F_CLIMBING) {
                ram[M_FLIP] = (uint8_t)((ram[M_TICK] & 0x10) << 2);
                return 7;
            }
    }
    if (f & F_ATTACKING) return attack_frames[ram[M_ATTACK_FRAME]];
    if ((ram[M_STATUS] & S_WING_BOOTS) && (ram[M_JOY] & B_A)) return 3;
    return walk_frames[f & F_MOVING ? (ram[M_TICK] >> 3) & 3 : 0];
}

/* One frame of play: the controller read, GameLoop_UpdatePlayer, then the
 * draw (shield, body, weapon). */
typedef struct {
    uint8_t x, y, flags, body, gear;
} Frame;
static Frame step(uint8_t buttons) {
    ram[M_NEW] = buttons & (uint8_t)~ram[M_JOY];
    ram[M_JOY] = buttons;
    cpu.a = 0x5A;
    cpu.x = 0x33;
    cpu.y = 0x44;
    cpu.p = 0x24;
    attack();
    vert_movement();
    horizontal();
    iframes();
    uint8_t gear = gear_frame(), body = body_frame();
    CHECK(gear == gear_frame()); /* shield then weapon: the same pose */
    return (Frame){ ram[M_X], ram[M_Y], ram[M_FLAGS], body, gear };
}

/* The hero, at a block position, standing and facing right, on screen 3 of area 1. */
static void place(uint8_t x, uint8_t y) {
    memset(ram, 0, sizeof ram);
    ram[M_X] = x;
    ram[M_Y] = y;
    ram[M_FLAGS] = F_RIGHT;
    ram[M_JC] = 0x20;
    ram[M_AREA] = 1;
    ram[M_SCREEN] = 3;
    standard_accel();
}
