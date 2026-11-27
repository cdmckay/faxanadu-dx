#pragma once
/* A model of the machine for Block's and Sugata's flash's unit tests
 * (model.c): CPU RAM and the stack, banks 12-14 of ROM with the read
 * overrides, the registers, the hook sites as the engine runs them, and
 * the few routines the features run or return through. No emulator, no ROM:
 * the tests play the game's own code, instruction by instruction as the
 * disassembly has it, around the sites. */
#include "cyc_mod.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned checks;
#define CHECK(test)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(test)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%u: %s\n", __FILE__, __LINE__, #test);                                            \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

extern uint8_t ram[0x800];
extern uint8_t rom[16][0x4000]; /* banks 12-14 are used; $8000-$BFFF of the mapped one */
extern unsigned mapped;         /* the bank at $8000 (14 in the main loop, 12 in scripts) */
extern CycModRegs cpu;
extern bool isolated_scope, saves_on;

/* What the game read: a ROM byte with the read overrides applied. */
uint8_t rom_read(unsigned bank, uint16_t addr);
unsigned overrides_used(void);

/* Sounds played through Sound_PlayEffect, in order. */
extern uint8_t sounds[64];
extern unsigned sound_count;
uint8_t last_sound(void);

/* The stack, as a JSR or a pushed RTS dispatch leaves it. */
void push16(uint16_t v);
uint16_t pop16(void);

/* A routine the features run with cyc_mod_call_commit, or return through. */
typedef void (*ModelCode)(void);
void model_routine(uint16_t addr, ModelCode code);

/* Enter a hooked instruction, from a JSR or dispatch whose RTS goes to
 * `back`: its sites run in game.toml order (`ids`, the enabled ones only)
 * until one handles it. True when none did: the game's own
 * code goes on. When one did, whatever it made the RTS go to runs first
 * (model routines), then the routine has returned. */
bool sites(uint16_t addr, uint16_t back, const char *const *ids, unsigned n);
#define MODEL_BACK 0xFFF0u /* where a routine's RTS goes, when it does not matter */
#define SITES_FROM(addr, back, ...)                                                                                    \
    sites((addr), (back), (const char *const[]){ __VA_ARGS__ },                                                        \
          sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(const char *))
#define SITES(addr, ...) SITES_FROM((addr), MODEL_BACK, __VA_ARGS__)
bool hook_enabled(const char *id);

/* Mods activate and deactivate as the host does. */
void model_reset(void); /* every reset callback */
void model_activate(const char *plugin);

/* The mod's save-state record. */
int model_state_get(const char *id, uint8_t *buf, int cap);
int model_state_set(const char *id, const uint8_t *buf, int len);
