/* Named fields beside the game: a seam for save slots.
 *
 * A field of the player's progress that is set is a line naming it in a small
 * text file beside the game, as Guru travel keeps faxanadu_gurus.txt. Save
 * slots (#24) will keep named fields in the save (fax_save_get/set); when they
 * land, only these two functions change. With saves off (--no-save) nothing is
 * kept, so what they remember lasts the session. */
#include "fax_remember.h"
#include "cyc_host_extras.h"
#ifdef CYC_WITH_SDL
#include <SDL.h>
#endif
#include <stdio.h>
#include <string.h>

static bool sidecar_path(const char *name, char out[1100]) {
#ifdef CYC_WITH_SDL
    char *base = SDL_GetBasePath();
    if (!base) return false;
    int n = snprintf(out, 1100, "%s%s", base, name);
    SDL_free(base);
#else
    int n = snprintf(out, 1100, "%s", name);
#endif
    return n > 0 && n < 1100;
}

bool fax_remembered(const char *file, const char *field) {
    char path[1100], text[64];
    bool found = false;
    if (!cyc_host_saves_enabled() || !sidecar_path(file, path)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    while (!found && fgets(text, sizeof text, f)) {
        text[strcspn(text, "\r\n")] = 0;
        found = !strcmp(text, field);
    }
    fclose(f);
    return found;
}

void fax_remember(const char *file, const char *field) {
    char path[1100], temp[1120], kept[1024] = "", text[64];
    if (fax_remembered(file, field) || !cyc_host_saves_enabled() || !sidecar_path(file, path)) return;
    FILE *f = fopen(path, "r");                       /* the fields already kept, kept */
    if (f) {
        size_t n = 0;
        while (fgets(text, sizeof text, f)) {
            text[strcspn(text, "\r\n")] = 0;
            size_t len = strlen(text);
            if (len && n + len + 2 < sizeof kept) { memcpy(kept + n, text, len); n += len; kept[n++] = '\n'; kept[n] = 0; }
        }
        fclose(f);
    }
    snprintf(temp, sizeof temp, "%s.tmp", path);
    f = fopen(temp, "w");
    if (!f) return;
    bool ok = fprintf(f, "%s%s\n", kept, field) > 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(temp); return; }
#ifdef _WIN32
    extern bool faxanadu_replace_file(const char *, const char *);
    if (!faxanadu_replace_file(temp, path)) remove(temp);
#else
    if (rename(temp, path) != 0) remove(temp);
#endif
}
