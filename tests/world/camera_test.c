/* Unit tests for Seamless's camera (src/fax_camera.c, #166's slice 4: the
 * view shows only rooms), without the emulator.
 *
 * A map is a set of places holding rooms, as fax_world.c's topology() lays
 * them out, and the places where the hero can pass between two rooms one
 * above the other. The tests walk the hero, a pixel at a time, over maps of
 * the game's own shapes: along every row at every height he can stand at,
 * and up and down through every seam one above the other, with the opening
 * at each of its 16 block columns. On every step:
 *
 * - his body is in the view;
 * - standing anywhere in a room with a room beside it, the view lies over no
 *   place without a room (the plan's "on no frame");
 * - while his body is across a seam one above the other, it lies over one
 *   only where no view over rooms both above and below shows him;
 * - it moves at most a few pixels for each of his (counted for walking,
 *   for climbing, and for the steps across a seam).
 *
 * A room alone in its row (an arena) is centred, as before, and a map with
 * no empty place sees the old camera's view, centred on him and kept inside
 * the box round the rooms.
 *
 * With the ROM's path as the argument, it also lays out every area of the
 * real game from its scroll links (bank 3), from every room, as topology()
 * does, the boss screens on their own, and walks them all. */
#include "fax_camera.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = FAX_CAMERA_W, H = FAX_CAMERA_H, RW = FAX_CAMERA_ROOM_W, RH = FAX_CAMERA_ROOM_H, MAX = 96 };

static int failures, checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; if (failures < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- a map ---- */

typedef struct {
    const char *name;
    int n, col[MAX], row[MAX];
    int up[MAX];          /* the room joined above room i, or -1 */
    int id[MAX];          /* its screen, on a real map */
} Map;
static Map map;
static int open_at = -1;  /* the block column of every opening between rooms one above the other; -1 none */
enum { SPAN = 200 };
static short grid[SPAN][SPAN];   /* room index + 1 at (col, row) + SPAN / 2 */

static int at(int col, int row) {
    col += SPAN / 2, row += SPAN / 2;
    return col < 0 || row < 0 || col >= SPAN || row >= SPAN ? -1 : grid[row][col] - 1;
}
static void index_map(void) {
    memset(grid, 0, sizeof grid);
    for (int i = 0; i < map.n; ++i) grid[map.row[i] + SPAN / 2][map.col[i] + SPAN / 2] = (short)(i + 1);
}
static bool room_cb(void *ctx, int col, int row) { (void)ctx; return at(col, row) >= 0; }
static unsigned openings_cb(void *ctx, int col, int row) {
    (void)ctx;
    int d = at(col, row), u = at(col, row - 1);
    return d >= 0 && u >= 0 && map.up[d] == u && open_at >= 0 ? 1u << open_at : 0;
}
static const FaxCameraMap camera = { room_cb, openings_cb, NULL };

/* A map drawn in rows of rooms: '#' a room, '.' none; '|' between two rows'
 * same column joins them up and down (a line of its own, under the upper). */
static void draw(const char *name, const char *const *lines, int count) {
    memset(&map, 0, sizeof map);
    map.name = name;
    int row = 0;
    for (int l = 0; l < count; ++l) {
        const char *s = lines[l];
        if (strchr(s, '|')) continue;
        for (int c = 0; s[c]; ++c) if (s[c] == '#') { map.col[map.n] = c; map.row[map.n] = row; map.up[map.n] = -1; ++map.n; }
        ++row;
    }
    index_map();
    /* Then the joins: a '|' line joins the rows above and below it. */
    row = 0;
    for (int l = 0; l < count; ++l) {
        const char *s = lines[l];
        if (!strchr(s, '|')) { ++row; continue; }
        for (int c = 0; s[c]; ++c) if (s[c] == '|') {
            int d = at(c, row), u = at(c, row - 1);
            if (d >= 0 && u >= 0) map.up[d] = u;
        }
    }
}

/* ---- what is checked ---- */

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }

/* Whether the view at (x, y) lies over a place with no room. */
static bool black(int x, int y) {
    for (int r = (y >= 0 ? y / RH : -((RH - 1 - y) / RH)); r * RH < y + H; ++r)
        for (int c = (x >= 0 ? x / RW : -((RW - 1 - x) / RW)); c * RW < x + W; ++c)
            if (at(c, r) < 0) return true;
    return false;
}
/* Whether views over rooms both above and below the seam at row dn's top
 * show his body (hx, hy) whole, his body across it, over a stretch of two or
 * more columns with rooms in both rows: 96 of them side by side, for the
 * camera moves onto them in full only where it has that much room (so a
 * walk past never swings it). */
static bool clean_across(int hx, int hy, int dn) {
    (void)hy;
    for (int a = fdiv(hx, RW) - 3; a <= fdiv(hx, RW) + 3; ++a) {
        int b = a;
        while (at(b, dn - 1) >= 0 && at(b, dn) >= 0) ++b;
        if (b - a >= 2) {
            int lo = a * RW, hi = b * RW - W;
            if (lo < hx + 16 - W) lo = hx + 16 - W;
            if (hi > hx) hi = hx;
            if (hi - lo >= 96) return true;
        }
    }
    return false;
}
static void place(int hx, int hy, int *x, int *y) {
    fax_camera_place(&camera, hx, hy, x, y);
    CHECK(*x <= hx && *x + W >= hx + 16 && *y <= hy && *y + H >= hy + 32,
          "%s: his body (%d, %d) out of the view (%d, %d)", map.name, hx, hy, *x, *y);
}
/* The most the view moved for a pixel of his, walking, climbing, and while
 * his body was across a seam one above the other (more than one in a sweep
 * is allowed: the sweep is a pixel a step). */
static int worst_walk, worst_open_walk, worst_climb, worst_straddle, straddle_black, straddle_steps;
static char worst_walk_at[96], worst_climb_at[96], worst_straddle_at[96];

/* Along every row of a room he can stand in, a pixel at a time, through
 * every room beside it. */
static void walk_rows(void) {
    for (int i = 0; i < map.n; ++i) {
        int c = map.col[i], r = map.row[i];
        if (at(c - 1, r) >= 0) continue;              /* from each run's left end */
        int last = c;
        while (at(last + 1, r) >= 0) ++last;
        for (int hy = 0; hy <= 176; hy += 8) {
            int px = 0, py = 0;
            for (int hx = c * RW; hx <= last * RW + 240; ++hx) {
                int wy = r * RH + hy, x, y;
                place(hx, wy, &x, &y);
                if (last > c)
                    CHECK(!black(x, y), "%s: walking row %d at %d, x %d: the view (%d, %d) lies over an empty place",
                          map.name, r, hy, hx, x, y);
                if (hx > c * RW) {
                    int moved = abs(x - px) > abs(y - py) ? abs(x - px) : abs(y - py);
                    if (open_at >= 0) { if (moved > worst_open_walk) worst_open_walk = moved; }
                    else if (moved > worst_walk) {
                        worst_walk = moved;
                        snprintf(worst_walk_at, sizeof worst_walk_at, "%s row %d y %d x %d", map.name, r, hy, hx);
                    }
                }
                px = x, py = y;
            }
        }
    }
}

/* Up and down through every seam one above the other, at every X of the
 * column, a pixel at a time from the middle of the room below to the middle
 * of the room above. */
static void climb_seams(void) {
    for (int i = 0; i < map.n; ++i) {
        int u = map.up[i];
        if (u < 0) continue;
        int c = map.col[i], dn = map.row[i];
        for (int lx = 0; lx <= 240; lx += 16) {
            open_at = lx / 16;
            int hx = c * RW + lx, px = 0, py = 0;
            for (int hy = dn * RH + 88; hy >= (dn - 1) * RH + 88; --hy) {
                int x, y;
                place(hx, hy, &x, &y);
                int e = hy + 16 - dn * RH;
                bool across = e > -16 && e < 16;
                if (!across) {
                    bool alone = at(c - 1, fdiv(hy + 16, RH)) < 0 && at(c + 1, fdiv(hy + 16, RH)) < 0;
                    if (!alone)
                        CHECK(!black(x, y), "%s: climbing at column %d x %d, y %d: black at (%d, %d), not across the seam",
                              map.name, c, lx, hy, x, y);
                } else {
                    ++straddle_steps;
                    if (black(x, y)) {
                        ++straddle_black;
                        CHECK(!clean_across(hx, hy, dn), "%s: climbing at column %d x %d, y %d: black at (%d, %d) though a view over rooms shows him",
                              map.name, c, lx, hy, x, y);
                    }
                }
                if (hy < dn * RH + 88) {
                    int moved = abs(x - px) > abs(y - py) ? abs(x - px) : abs(y - py);
                    int *worst = across ? &worst_straddle : &worst_climb;
                    char *where = across ? worst_straddle_at : worst_climb_at;
                    if (moved > *worst) {
                        *worst = moved;
                        snprintf(where, 96, "%s col %d x %d y %d (%d,%d)->(%d,%d)", map.name, c, lx, hy, px, py, x, y);
                    }
                }
                px = x, py = y;
            }
        }
        open_at = -1;
    }
}

static void walk_map(void) {
    walk_rows();
    for (open_at = 0; open_at < 16; open_at += 5) walk_rows();   /* under an opening, too */
    open_at = -1;
    climb_seams();
}

/* ---- the camera before slice 4: centred, kept inside the rooms' box ---- */
static void old_camera(int hx, int hy, int *x, int *y) {
    int l = map.col[0], r = l, t = map.row[0], b = t;
    for (int i = 0; i < map.n; ++i) {
        if (map.col[i] < l) l = map.col[i];
        if (map.col[i] > r) r = map.col[i];
        if (map.row[i] < t) t = map.row[i];
        if (map.row[i] > b) b = map.row[i];
    }
    int cx = hx + 8 - W / 2, cy = hy + 16 - H / 2, xmax = (r + 1) * RW - W, ymax = (b + 1) * RH - H;
    *x = xmax < l * RW ? (l * RW + xmax) / 2 : cx < l * RW ? l * RW : cx > xmax ? xmax : cx;
    *y = ymax < t * RH ? ymax : cy < t * RH ? t * RH : cy > ymax ? ymax : cy;
}

static void test_shapes(void) {
    {   /* Eolis: a row, nothing above or below. As before. */
        const char *m[] = { "#########" };
        draw("a row", m, 1);
        for (int hx = 0; hx < 9 * RW - 16; hx += 3) for (int hy = 0; hy <= 176; hy += 11) {
            int x, y, ox, oy;
            place(hx, hy, &x, &y);
            old_camera(hx, hy, &ox, &oy);
            CHECK(x == ox && y == oy, "a row: (%d, %d) at (%d, %d), before (%d, %d)", hx, hy, x, y, ox, oy);
        }
        walk_map();
    }
    {   /* A block of rooms with no empty place: as before, following him up and down too. */
        const char *m[] = { "####", "|..|", "####", ".|..", "####" };
        draw("a block", m, 5);
        for (int hx = 0; hx < 4 * RW - 16; hx += 5) for (int hy = 0; hy < 3 * RH - 32; hy += 7) {
            int x, y, ox, oy;
            place(hx, hy, &x, &y);
            old_camera(hx, hy, &ox, &oy);
            CHECK(x == ox && y == oy, "a block: (%d, %d) at (%d, %d), before (%d, %d)", hx, hy, x, y, ox, oy);
        }
        walk_map();
    }
    {   /* An arena: alone, centred, black at its sides, as before. */
        const char *m[] = { "#" };
        draw("an arena", m, 1);
        for (int hx = 0; hx <= 240; hx += 10) for (int hy = 0; hy <= 176; hy += 16) {
            int x, y;
            place(hx, hy, &x, &y);
            CHECK(x == (RW - W) / 2 && y == 0, "an arena: (%d, %d) at (%d, %d)", hx, hy, x, y);
        }
    }
    {   /* The Tower of Trunk from $0D: $10-$11-$12 between rows that share
         * a column with it at each end only ($0F under $10, $13 over $12). */
        const char *m[] = { "....##", "....|.", "..###.", "..|...", "###..." };
        draw("Trunk from $0D", m, 5);
        /* Out of the door onto $10, the old view's top left was black. */
        int x, y, ox, oy;
        place(2 * RW + 40, RH + 64, &x, &y);
        old_camera(2 * RW + 40, RH + 64, &ox, &oy);
        CHECK(black(ox, oy) && !black(x, y) && x == 2 * RW && y == RH, "$10: (%d, %d), before (%d, %d)", x, y, ox, oy);
        walk_map();
    }
    {   /* The Mist from $30: a corner of the row above empty, and of the row below. */
        const char *m[] = { "####..", "||||..", "######", "|||...", "###...", ".|....", ".#...." };
        draw("Mist from $30", m, 7);
        walk_map();
    }
    {   /* A ladder up from a row's end (Mist $10 under $07): the view moves onto the two columns both rows hold. */
        const char *m[] = { "###", "|..", "##." };
        draw("an end", m, 3);
        walk_map();
    }
    {   /* Shafts: a room alone in its row, between wider rows. */
        const char *m[] = { "###", ".|.", ".#.", ".|.", "###" };
        draw("a shaft", m, 5);
        walk_map();
    }
}

/* ---- the real maps ---- */

static uint8_t prg[16 * 16384];
static unsigned rd(unsigned bank, unsigned addr) { return prg[bank * 16384 + (addr & 16383)]; }
static unsigned word(unsigned bank, unsigned addr) { return rd(bank, addr) | rd(bank, addr + 1) << 8; }
static const unsigned counts[8] = { 9, 64, 83, 14, 10, 40, 32, 20 };
static const char *const areas[8] = { "Eolis", "Trunk", "Mist", "towns", "buildings", "Branch", "Dartmoor", "Zenis" };
/* The boss screens (SCREEN_EVENT_BOSS in their sprite lists, and Zenis $00,
 * the final boss's), arenas while the boss lives: they join nothing. */
static bool boss(unsigned a, unsigned s) {
    switch (a) {
    case 1: return s == 0x15 || s == 0x3B;
    case 2: return s == 0x34 || s == 0x4C;
    case 5: return s == 0x07 || s == 0x1D;
    case 6: return s == 0x15;
    case 7: return s == 0x00;
    default: return false;
    }
}
/* topology(): from `current`, every room joined both ways, placed by its
 * joins, none on a place another holds. */
static void lay_out(unsigned a, unsigned current, char *name, size_t size) {
    unsigned tables = 0x8000u + word(3, 0x8000), data = 0x8000u + word(3, tables + 2 * a), scroll = 0x8000u + word(3, data + 4);
    unsigned n = counts[a], queue[MAX], head = 0, tail = 0, reverse[4] = { 1, 0, 3, 2 };
    int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 }, of[MAX];
    bool owned[MAX] = { false };
    memset(&map, 0, sizeof map);
    memset(grid, 0, sizeof grid);
    snprintf(name, size, "%s from $%02X", areas[a], current);
    map.name = name;
    for (unsigned i = 0; i < MAX; ++i) of[i] = -1;
    owned[current] = true; of[current] = map.n; map.id[map.n] = (int)current;
    map.col[map.n] = 0; map.row[map.n] = 0; map.up[map.n++] = -1;
    index_map();
    queue[tail++] = current;
    while (head < tail) {
        unsigned id = queue[head++];
        if (boss(a, id)) continue;
        for (unsigned d = 0; d < 4; ++d) {
            unsigned next = rd(3, scroll + id * 4 + d);
            if (next >= n || rd(3, scroll + next * 4 + reverse[d]) != id || boss(a, next)) continue;
            int x = map.col[of[id]] + dx[d], y = map.row[of[id]] + dy[d];
            int there = at(x, y);
            if (there >= 0 && there != of[next]) continue;
            if (!owned[next]) {
                owned[next] = true; of[next] = map.n; map.id[map.n] = (int)next;
                map.col[map.n] = x; map.row[map.n] = y; map.up[map.n++] = -1;
                index_map();
                queue[tail++] = next;
            }
            if (map.col[of[next]] == x && map.row[of[next]] == y && d == 2) map.up[of[id]] = of[next];
        }
    }
}

static void test_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL: cannot open %s\n", path); ++failures; return; }
    uint8_t header[16];
    if (fread(header, 1, 16, f) != 16) { fclose(f); ++failures; return; }
    if (header[6] & 4) fseek(f, 512, SEEK_CUR);
    size_t got = fread(prg, 1, sizeof prg, f);
    fclose(f);
    if (got != sizeof prg) { printf("FAIL: short ROM\n"); ++failures; return; }
    static char names[8][MAX][32];
    /* Each room's place, from the room laid out first, in every map walked:
     * a map laid out the same from another of its rooms is walked once. */
    static int seen_col[MAX * 2][MAX], seen_row[MAX * 2][MAX];
    unsigned maps = 0, layouts = 0;
    for (unsigned a = 0; a < 8; ++a) {
        unsigned seen = 0;
        for (unsigned s = 0; s < counts[a]; ++s) {
            lay_out(a, s, names[a][s], sizeof names[a][s]);
            if (map.n < 2) continue;
            ++layouts;
            int col[MAX], row[MAX], first = 0;
            for (int i = 0; i < MAX; ++i) col[i] = row[i] = 9999;
            for (int i = 1; i < map.n; ++i) if (map.id[i] < map.id[first]) first = i;
            for (int i = 0; i < map.n; ++i) col[map.id[i]] = map.col[i] - map.col[first], row[map.id[i]] = map.row[i] - map.row[first];
            bool again = false;
            for (unsigned k = 0; k < seen && !again; ++k)
                again = !memcmp(seen_col[k], col, sizeof col) && !memcmp(seen_row[k], row, sizeof row);
            if (again) continue;
            memcpy(seen_col[seen], col, sizeof col), memcpy(seen_row[seen], row, sizeof row), ++seen;
            ++maps;
            walk_map();
        }
    }
    printf("the real maps: %u laid out from every room, %u different, each walked\n", layouts, maps);
}

int main(int argc, char **argv) {
    test_shapes();
    if (argc > 1) test_rom(argv[1]);
    printf("the most the view moved for a pixel of his: walking %d (%s), under an opening %d, climbing %d (%s), across a seam %d (%s)\n",
           worst_walk, worst_walk_at, worst_open_walk, worst_climb, worst_climb_at, worst_straddle, worst_straddle_at);
    printf("steps across a seam one above the other: %d, %d of them with black that no view over rooms avoids\n",
           straddle_steps, straddle_black);
    CHECK(worst_walk <= 1, "walking, the view moved %d pixels for one of his", worst_walk);
    CHECK(worst_open_walk <= 4, "walking under an opening, the view moved %d pixels for one of his", worst_open_walk);
    CHECK(worst_climb <= 6, "climbing, the view moved %d pixels for one of his", worst_climb);
    CHECK(worst_straddle <= 7, "across a seam, the view moved %d pixels for one of his", worst_straddle);
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
