/* Seamless's camera: the view shows only rooms (#166's slice 4).
 *
 * The view is 424 wide and exactly a room tall (208) below the status bar.
 * Centred on the hero it can lie over places of the map that hold no room,
 * and those showed black. From the centred place, it keeps off them:
 *
 * - Sideways it stays over his row's run of rooms (the rooms side by side
 *   with his, joined or not). 256 + 256 is more than 424, so any room beside
 *   his fills the rest. A room alone in its row (a boss arena, a shaft) is
 *   centred, black at its sides, as before.
 * - Up and down it follows him into the row above or below only where the
 *   columns it spans hold rooms there. That limit eases in: the view may
 *   reach a line into the other row for each pixel its span is clear of
 *   that row's empty places (K). So as he walks toward one, the view comes
 *   down to his row a pixel for each of his, and is there before the empty
 *   place would come into sight: the plan's glide and its look-ahead, as
 *   one. Where his own row has no room either, the other's empty place
 *   doesn't count, so at the map's side it follows him as before.
 * - Where he can pass up or down (an opening or a ladder between two rooms
 *   one above the other), near it the view moves sideways onto the columns
 *   both rows hold rooms in, so it can follow him through without black:
 *   in full where those views have room to show him clear of their sides
 *   (ROOM), in part where they have less. Where there are none (the two
 *   rows' rooms don't overlap by two), black can't be helped: the view keeps
 *   his row until his body reaches the seam, then follows him through in
 *   the 32 pixels he straddles it.
 *
 * Every step is continuous in his place, so the view never jumps, and it
 * holds no state: it is where his place puts it, in every frame and every
 * save state. tests/world/camera_test.c checks it over the game's maps. */
#include "fax_camera.h"
#include <stdlib.h>

enum {
    W = FAX_CAMERA_W,
    H = FAX_CAMERA_H,
    RW = FAX_CAMERA_ROOM_W,
    RH = FAX_CAMERA_ROOM_H,
    ONE = 256,     /* a weight's whole */
    REACH = 3,     /* rooms looked at each side of his */
    K = 1,         /* lines into the other row for each pixel of its clearance */
    STRADDLE = 16, /* his middle this near a row's edge: his body reaches across it */
    NEAR = 16,
    FAR = 48,       /* onto both rows' columns: wholly with his body at the edge, not at all 32 from it */
    OPEN_FALL = 96, /* ... and not at all this far from where he can pass */
    ROOM = 96,      /* ... all the way where those views span this many pixels or more, and
                            short of them in proportion where they span fewer: as they run out the
                            view goes back to his row's at most 4 pixels for each of his, and
                            never goes to one that would show him at its very side */
    INSET = 32,     /* ... and that far inside them where they span twice that */
};

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* From a to b by t of ONE. */
static int mix(int a, int b, int t) { return a + fdiv((b - a) * t + ONE / 2, ONE); }
/* 0 at v = from, ONE at v = to, in a line between. */
static int ramp(int v, int from, int to) { return clampi((v - from) * ONE / (to - from), 0, ONE); }
static bool room(const FaxCameraMap *m, int col, int row) { return m->room(m->ctx, col, row); }

/* How far the span [l, r) of `row` is from the nearest place there with no
 * room over a room of row `mine`, his: less than 0 by as much as it lies
 * over one. A place empty in both rows never counts: the view keeps off it
 * in his row anyway, so it may follow him up or down right to his row's end. */
static int clearance(const FaxCameraMap *m, int row, int mine, int l, int r) {
    int best = REACH * RW;
    for (int j = fdiv(l, RW) - REACH; j <= fdiv(r - 1, RW) + REACH; ++j) {
        if (room(m, j, row) || !room(m, j, mine)) continue;
        int a = j * RW, b = a + RW, d;
        if (a >= r) d = a - r;
        else if (b <= l) d = l - b;
        else d = (l > a ? l : a) - (r < b ? r : b);
        if (d < best) best = d;
    }
    return best;
}

/* The view's left edge on `row` for the centred cx: over the row's run of
 * rooms round column c, or centred on c's room when it has none beside it. */
static int along(const FaxCameraMap *m, int row, int c, int cx) {
    int a = c, b = c;
    while (a > c - REACH && room(m, a - 1, row)) --a;
    while (b < c + REACH && room(m, b + 1, row)) ++b;
    if (a == b) return c * RW + (RW - W) / 2;
    return clampi(cx, a * RW, (b + 1) * RW - W);
}

/* The left edges of the views over columns where both rows up and dn hold
 * rooms that still show his body, [X - 8, X + 8): in *lo to *hi, the stretch
 * nearest cx. False when there is none. */
static bool both(const FaxCameraMap *m, int up, int dn, int X, int cx, int *lo, int *hi) {
    int c = fdiv(X, RW), first = X + 8 - W, last = X - 8, best = -1;
    for (int j = c - REACH; j <= c + REACH;) {
        if (!room(m, j, up) || !room(m, j, dn)) {
            ++j;
            continue;
        }
        int a = j;
        while (j <= c + REACH && room(m, j, up) && room(m, j, dn)) ++j;
        if (j - a < 2) continue;
        int l = a * RW > first ? a * RW : first, r = j * RW - W < last ? j * RW - W : last;
        if (l > r) continue;
        int d = cx < l ? l - cx : cx > r ? cx - r : 0;
        if (best < 0 || d < best) best = d, *lo = l, *hi = r;
    }
    return best >= 0;
}

/* How near his body is to a block where he can pass between rows dn - 1 and
 * dn: ONE over one, falling to 0 at OPEN_FALL pixels away. */
static int opening(const FaxCameraMap *m, int dn, int X) {
    if (!m->openings) return 0;
    int c = fdiv(X, RW), best = 0;
    for (int j = c - 1; j <= c + 1; ++j) {
        unsigned bits = m->openings(m->ctx, j, dn);
        for (int bx = 0; bits && bx < 16; ++bx) {
            if (!(bits >> bx & 1)) continue;
            int a = j * RW + bx * 16, b = a + 16;
            int d = a >= X + 8 ? a - (X + 8) : b <= X - 8 ? X - 8 - b : 0;
            int w = ONE - d * ONE / OPEN_FALL;
            if (w > best) best = w;
        }
    }
    return best;
}

/* His row's view from x, moved by `near` (ONE: all the way) onto lo to hi,
 * the views on both rows' columns that show him. */
static int toward(int x, int lo, int hi, int near) {
    int room = hi - lo, inset = room / 2 < INSET ? room / 2 : INSET;
    int give = room < ROOM ? (ROOM - room) * (W / 2 - 8) / ROOM : 0; /* the most the way there can be, at none */
    return mix(x, clampi(x, lo + inset - give, hi - inset + give), near);
}

/* The lines the view may reach into a row whose empty places its span is
 * `clear` pixels from. */
static int reach(int clear) { return clear <= 0 ? 0 : K * clear; }

void fax_camera_place(const FaxCameraMap *m, int hx, int hy, int *x, int *y) {
    int X = hx + 8, Y = hy + 16, cx = X - W / 2, c = fdiv(X, RW);
    /* The edge between rows up and dn nearest his middle, at B; his middle e
     * below it (-104 to 103), and the centred view's lines above it. */
    int dn = fdiv(Y + RH / 2, RH), up = dn - 1, B = dn * RH, e = Y - B, centred = H / 2 - e;
    bool in_up = room(m, c, up), in_dn = room(m, c, dn);
    /* Near where he can pass between the two, onto the columns both hold. */
    int near = 0, lo = 0, hi = 0;
    if (in_up && in_dn && both(m, up, dn, X, cx, &lo, &hi)) near = ramp(abs(e), FAR, NEAR) * opening(m, dn, X) / ONE;
    /* With dn his row, the view reaching up as far as up's rooms let it; and
     * with up his row, down. */
    int xd = 0, od = 0, xu = 0, ou = 0;
    if (in_dn) {
        xd = toward(along(m, dn, c, cx), lo, hi, near);
        int r = reach(clearance(m, up, dn, xd, xd + W));
        od = centred < r ? centred : r;
        od = od < 0 ? 0 : od;
    }
    if (in_up) {
        xu = toward(along(m, up, c, cx), lo, hi, near);
        int r = reach(clearance(m, dn, up, xu, xu + W));
        ou = H - centred < r ? centred : H - r;
        ou = ou > H ? H : ou;
    }
    /* From one to the other while his body is across the edge. */
    int vx, over;
    if (in_dn && in_up) {
        int w = ramp(e, STRADDLE, -STRADDLE);
        vx = mix(xd, xu, w), over = mix(od, ou, w);
    } else if (in_dn) vx = xd, over = od;
    else if (in_up) vx = xu, over = ou;
    else vx = cx, over = centred;
    /* His body stays in the view. */
    over = clampi(clampi(over, STRADDLE - e, H - STRADDLE - e), 0, H);
    *x = vx;
    *y = B - over;
}
