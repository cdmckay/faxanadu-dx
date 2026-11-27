#pragma once
/* Speaker names (fax_speaker.c): while someone with a portrait talks, their
 * name shows on a tab above the dialogue box, in the game's own font and text
 * box frame: "Guru of Apolune", "King of Eolis", "Key Seller". A text box
 * without a portrait (walking men and women, signs, items) gets none. It
 * registers itself; the host calls these. */
#include <stdbool.h>
#include <stdint.h>

/* IScript_PortraitID ($03C7) while a portrait shows: the script's entity byte
 * ($80 + n, ISCRIPT_ENTITY_* in chipx86's DEFS.asm) without its top bit. */
enum {
    FAX_PORTRAIT_KING,
    FAX_PORTRAIT_GURU,
    FAX_PORTRAIT_MARTIAL_ARTIST,
    FAX_PORTRAIT_MAGICIAN,
    FAX_PORTRAIT_DOCTOR,
    FAX_PORTRAIT_NURSE,
    FAX_PORTRAIT_PINK_SHIRT_GUY,
    FAX_PORTRAIT_SMOKING_GUY,
    FAX_PORTRAIT_MEAT_SALESMAN,
    FAX_PORTRAIT_TOOLS_SALESMAN,
    FAX_PORTRAIT_KEY_SALESMAN,
    FAX_PORTRAITS
};

/* A name is at most 18 characters, so its tab is no wider than the dialogue box. */
enum { FAX_SPEAKER_NAME_MAX = 18 };

/* The name of `portrait` talking in building `building` (Area_CurrentScreen
 * in AREA_BUILDINGS), whose door is in zone `zone` (fax_zone.h, FAX_ZONE_NONE
 * if none). Writes it to `out` and returns true, or returns false: no name. */
bool fax_speaker_name(unsigned portrait, unsigned building, int zone, char out[FAX_SPEAKER_NAME_MAX + 1]);

/* The picture to present: `picture` (or, if NULL, the engine's own) with the
 * speaker's name over it while a portrait box is on screen; otherwise
 * `picture` itself. */
const uint32_t *fax_speaker_present(const uint32_t *picture, int *w, int *h);
void fax_speaker_tcp_setup(void);
