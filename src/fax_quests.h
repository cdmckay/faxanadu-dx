#pragma once
/* The quest log (fax_quests.c): when someone in the game makes a request
 * (fax_quest_table.c), the text box says "New quest" and its name after the
 * line, and "Quest complete" when it is done. A Quest log button (L, or the
 * right stick on a controller) pauses the game on a list of the quests, the
 * active ones with their objectives first. A "!" floats over anyone with a
 * quest to give, and a "?" over anyone who is the next step of a quest under
 * way. What was given is kept in the file in play. It registers its own
 * hooks; the host calls these. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "cyc_host_extras.h"

void fax_quests_frame_end(void);
void fax_quests_loaded(void); /* a save state was loaded: put the line back if it was showing */
/* The "!" and "?" marks over the people in the presented picture, after
 * Seamless has composed it and before the zone banner. */
const uint32_t *fax_quests_marks_present(const uint32_t *picture, int *w, int *h);
/* "Quest complete" for a quest done outside a conversation, in the zone
 * banner's place below the HUD. */
const uint32_t *fax_quests_banner_present(const uint32_t *picture, int *w, int *h);
/* The log, over everything, while it is open. */
const uint32_t *fax_quests_log_present(const uint32_t *picture, int *w, int *h);
void fax_quests_event(const void *event);
bool fax_quests_load_setting(const char *key, const char *value); /* QuestLogKey, QuestLogButton */
void fax_quests_save_settings(FILE *f);
const char *fax_quests_controls_label(void); /* "Quest log: L / LT", for the runtime menu */
/* The binding, for the launcher's Controls page and the layout's move (cyc_host_extras.h). */
const CycHostBinding *fax_quests_binding(void);
void fax_quests_tcp_setup(void);
bool fax_quests_showing(void); /* the log is open */
/* Open the log as its button does, at the main loop's next pause check: the
 * Select menu's QUESTS row (fax_menu.c). False where it cannot (the map open,
 * or the quest table not read). */
bool fax_quests_request(void);
/* Whether `quest` (fax_quest_table.h's FAX_QUEST_*) is given: in the log now,
 * or in the file in play's quests.given (false without the field, or with it
 * damaged), as the log reads it when the file is continued. */
bool fax_quests_given(unsigned quest);
bool fax_quests_given_in_file(unsigned quest);
