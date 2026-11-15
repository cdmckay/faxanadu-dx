/* Unit tests for Hold A to speed up text (fax_text_speed.c), without the
 * emulator or a ROM. The module runs against a model of the machine: CPU RAM
 * and the game's text box as the disassembly has it. Each frame the NMI reads
 * the pad into Joy1_ButtonMask and Joy1_ChangedButtonMask, the message loop
 * calls TextBox_ShowNextChar (whose call to TextBox_ShowNextChar_IfReady runs
 * the hook first, as the engine does) and then checks whether to continue or
 * dismiss, as IScriptAction_ShowMessage and IScriptAction_ShowQuestionMessage
 * do. */
#include "fax_text_speed.c"
#include <string.h>

static unsigned checks;
#define CHECK(test) do{++checks;if(!(test)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#test);exit(1);}}while(0)

/* ---- the machine ---- */
#define MESSAGE_ID     0x0213u
#define MESSAGE_PAUSED 0x0215u   /* TextBox_MessagePaused */
enum { A = 0x80, B = 0x40, PAUSE = 0xFC, END = 0xFF };
static uint8_t ram[0x800];
static bool hook_on;

uint8_t cyc_mod_peek(uint16_t addr) { return addr < 0x2000 ? ram[addr & 0x7FF] : 0; }
bool cyc_mod_poke(uint16_t addr, uint8_t value) { if (addr >= 0x2000) return false; ram[addr & 0x7FF] = value; return true; }
int nes_mod_register_function_entry_plugin(const char *id, uint16_t addr, NESModFunctionEntryCallback cb) { (void)id; (void)addr; (void)cb; return 1; }
int nes_mod_set_function_hook_enabled(const char *id, int on) {
    CHECK(!strcmp(id, "faxanadu.text-speed.if-ready"));
    hook_on = on;
    return 1;
}
int nes_mod_register_reset_callback(NESModActivationCallback cb) { (void)cb; return 1; }
int nes_mod_register_activation_plugin(const char *id, NESModActivationCallback cb) { (void)id; (void)cb; return 1; }

/* ---- the text box ---- */
static uint8_t msg[256];
static unsigned at, drawn, now;
static unsigned drawn_on[256];       /* the frame each character was drawn */
static bool clacked[256];            /* and whether it asked for the typing sound */

/* Messages_Load: '|' is a $FC page break. */
static void load(const char *s) {
    unsigned n = 0;
    for (; *s; ++s) msg[n++] = *s == '|' ? PAUSE : (uint8_t)*s;
    msg[n] = END;
    at = drawn = now = 0;
    ram[MESSAGE_ID] = 0x22; ram[MESSAGE_PAUSED] = 0; ram[TEXTBOX_TIMER] = 0;
    ram[JOY1_BUTTON_MASK] = ram[JOY1_CHANGED_BUTTON_MASK] = 0;
}
/* TextBox_ShowNextChar: INC TextBox_Timer, set TextBox_PlayTextSound, then
 * TextBox_ShowNextChar_IfReady draws (or reaches a pause or the end) when the
 * timer is a multiple of 4. TextBox_ShowMessageWithSound plays the typing
 * sound for the character if TextBox_PlayTextSound is still set. */
static void show_next_char(void) {
    ++ram[TEXTBOX_TIMER];
    ram[TEXTBOX_PLAY_TEXT_SOUND] = 1;
    if (hook_on) if_ready_hook(TEXTBOX_SHOW_NEXT_CHAR_IF_READY);
    if (!ram[MESSAGE_ID] || ram[MESSAGE_PAUSED] || (ram[TEXTBOX_TIMER] & 3)) return;
    uint8_t c = msg[at++];
    if (c == END) ram[MESSAGE_ID] = 0;                                          /* typed out */
    else if (c == PAUSE) { if (msg[at] == END) ram[MESSAGE_ID] = 0; else ram[MESSAGE_PAUSED] = 0xFF; }
    else clacked[drawn] = ram[TEXTBOX_PLAY_TEXT_SOUND] != 0, drawn_on[drawn++] = now;
}
typedef enum { GOES_ON, NEXT, DISMISSED } Result;
/* One frame of a message loop. The NMI's Input_HandleOnInterrupt computes
 * the newly pressed buttons as (held ^ previous) & held. */
static Result frame(uint8_t pad) {
    ++now;
    ram[JOY1_CHANGED_BUTTON_MASK] = (uint8_t)((pad ^ ram[JOY1_BUTTON_MASK]) & pad);
    ram[JOY1_BUTTON_MASK] = pad;
    show_next_char();
    uint8_t pressed = ram[JOY1_CHANGED_BUTTON_MASK];
    if (!ram[MESSAGE_ID]) return pressed & A ? NEXT : pressed & B ? DISMISSED : GOES_ON;   /* ▲ or ? */
    if (ram[MESSAGE_PAUSED] && (pressed & A)) { ram[MESSAGE_PAUSED] = 0; return GOES_ON; } /* ▼ */
    return pressed & B ? DISMISSED : GOES_ON;
}
static bool waiting(void) { return !ram[MESSAGE_ID] || ram[MESSAGE_PAUSED]; }
/* Frames until the page waits for a button, with A held or not. */
static unsigned type_out(const char *s, bool hold) {
    load(s);
    for (unsigned f = 1; f < 2000; ++f) {
        CHECK(frame(hold ? A : 0) == GOES_ON);
        if (waiting()) return f;
    }
    CHECK(!"the page did not type out");
    return 0;
}
/* Hold A for `frames` frames; nothing may continue or answer. */
static void hold(unsigned frames) { while (frames--) CHECK(frame(A) == GOES_ON); }

#define LONG "Glad you could/come./Disaster has/befallen us."   /* the King's first page (message $35) */

static void ready_frames(void) {
    CHECK(speeding(A, 0));
    CHECK(speeding(A | B, B));               /* B pressed while A is held: B still dismisses */
    CHECK(!speeding(A, A));                  /* the frame A is pressed keeps the game's pace */
    CHECK(!speeding(0, 0) && !speeding(B, B) && !speeding(B, 0));
    for (unsigned t = 0; t < 256; ++t) {
        uint8_t next = ready((uint8_t)t);
        CHECK((next & 3) == 0);                                  /* a ready frame */
        CHECK((uint8_t)(next - t) < 4);                          /* the nearest one, wrapping at 256 */
    }
    unsigned sounds = 0;
    for (unsigned t = 0; t < 256; t += 4) sounds += sound((uint8_t)t);
    CHECK(sounds == 16);                                         /* every 4th ready frame */
}

static void magic_button_masks(void) {
    /* The Magic button (fax_magic.c) ORs Up held and B new into the joypad
     * masks for Player_CastMagic alone, and leaves A as the pad has it. With
     * or without that, the hook decides the same, for every pad state; and
     * the hook writes neither mask (writes_only_the_timer_and_sound). */
    enum { UP = 0x08 };
    for (unsigned held = 0; held < 256; ++held)
        for (unsigned changed = 0; changed < 256; ++changed) {
            if (changed & ~held) continue;                       /* new buttons are held buttons */
            CHECK(speeding((uint8_t)(held | UP | B), (uint8_t)(changed | B)) ==
                  speeding((uint8_t)held, (uint8_t)changed));
        }
}

static void writes_only_the_timer_and_sound(void) {
    for (unsigned i = 0; i < sizeof ram; ++i) ram[i] = (uint8_t)(i * 7 + 1);
    ram[JOY1_BUTTON_MASK] = A; ram[JOY1_CHANGED_BUTTON_MASK] = 0;
    ram[TEXTBOX_TIMER] = 0x41; ram[TEXTBOX_PLAY_TEXT_SOUND] = 1;
    uint8_t before[sizeof ram];
    memcpy(before, ram, sizeof ram);
    CHECK(if_ready_hook(TEXTBOX_SHOW_NEXT_CHAR_IF_READY) == 0);   /* the routine always runs */
    CHECK(ram[TEXTBOX_TIMER] == 0x44 && ram[TEXTBOX_PLAY_TEXT_SOUND] == 0);
    ram[TEXTBOX_TIMER] = before[TEXTBOX_TIMER]; ram[TEXTBOX_PLAY_TEXT_SOUND] = 1;
    CHECK(!memcmp(before, ram, sizeof ram));
    ram[TEXTBOX_TIMER] = 0x4F;
    CHECK(if_ready_hook(TEXTBOX_SHOW_NEXT_CHAR_IF_READY) == 0);
    CHECK(ram[TEXTBOX_TIMER] == 0x50 && ram[TEXTBOX_PLAY_TEXT_SOUND] == 1);   /* a sound frame */
    ram[JOY1_BUTTON_MASK] = 0; ram[TEXTBOX_TIMER] = 0x41; ram[TEXTBOX_PLAY_TEXT_SOUND] = 1;
    CHECK(if_ready_hook(TEXTBOX_SHOW_NEXT_CHAR_IF_READY) == 0);
    CHECK(ram[TEXTBOX_TIMER] == 0x41 && ram[TEXTBOX_PLAY_TEXT_SOUND] == 1);   /* not held: untouched */
}

static void pace(void) {
    const unsigned n = (unsigned)strlen(LONG);
    CHECK(type_out(LONG, false) == 4 * (n + 1));       /* every 4th frame, the end included */
    for (unsigned i = 1; i < drawn; ++i) CHECK(drawn_on[i] - drawn_on[i - 1] == 4);
    for (unsigned i = 0; i < drawn; ++i) CHECK(clacked[i]);   /* each with the typing sound */
    /* Held: the press frame at the game's pace, then a character a frame,
     * and the typing sound every 4th frame, its pace without A. */
    CHECK(type_out(LONG, true) == n + 2);
    unsigned last = 0;
    for (unsigned i = 1; i < drawn; ++i) CHECK(drawn_on[i] - drawn_on[i - 1] == 1);
    for (unsigned i = 0; i < drawn; ++i)
        if (clacked[i]) { CHECK(!last || drawn_on[i] - last == 4); last = drawn_on[i]; }
    CHECK(last);
    CHECK((double)(4 * (n + 1)) / (n + 2) > 3.8);      /* about 4x */
}

static void release(void) {
    /* Hold A through 10 characters, then let go: back to every 4th frame. */
    load(LONG);
    hold(11);
    CHECK(drawn == 10);
    while (!waiting()) CHECK(frame(0) == GOES_ON);
    CHECK(drawn == strlen(LONG) && drawn_on[10] - drawn_on[9] == 4);
    for (unsigned i = 11; i < drawn; ++i) CHECK(drawn_on[i] - drawn_on[i - 1] == 4 && clacked[i]);
}

static void page_break(void) {
    load("Don't have|negative");
    hold(1 + 10 + 1);                                   /* the press, 10 characters, the $FC */
    CHECK(ram[MESSAGE_PAUSED] && drawn == 10);
    unsigned stopped = at;
    hold(600);                                          /* held through the ▼: it waits */
    CHECK(ram[MESSAGE_PAUSED] && at == stopped && drawn == 10);
    CHECK(frame(0) == GOES_ON && ram[MESSAGE_PAUSED]);  /* release */
    CHECK(frame(A) == GOES_ON && !ram[MESSAGE_PAUSED]); /* a new press goes on */
    hold(8 + 1);                                        /* and holding types the next page fast */
    CHECK(drawn == 18 && !ram[MESSAGE_ID]);
    hold(600);                                          /* the end of the message waits too */
    CHECK(frame(0) == GOES_ON && frame(A) == NEXT);
}

static void question(void) {
    /* Held through a question: it is never answered. A new A accepts it, a new B declines it. */
    load("To the Guru of/Eolis?");
    hold(600);
    CHECK(!ram[MESSAGE_ID] && drawn == 21);
    CHECK(frame(0) == GOES_ON && frame(A) == NEXT);
    load("To the Guru of/Eolis?");
    hold(600);
    CHECK(frame(A | B) == DISMISSED);                   /* B, with A still held */
    load("To the Guru of/Eolis?");
    hold(5);
    CHECK(frame(A | B) == DISMISSED);                   /* B mid-text, as in the game */
}

static void press_frame_is_stock(void) {
    /* The frame A is pressed behaves exactly as without the mod, at every
     * timer phase: so the press that starts the speed-up can never finish a
     * page and continue it at once, unless the game itself would have. */
    for (unsigned phase = 0; phase < 4; ++phase)
        for (unsigned lead = 0; lead < 12; ++lead) {
            uint8_t with[sizeof ram], without[sizeof ram];
            Result r[2];
            unsigned reads[2];
            for (int on = 0; on < 2; ++on) {
                load("ab|cd");
                for (unsigned f = 0; f < lead; ++f) frame(0);
                ram[TEXTBOX_TIMER] = (uint8_t)((ram[TEXTBOX_TIMER] & ~3u) | phase);
                hook_on = on;
                r[on] = frame(A);
                reads[on] = at;
                memcpy(on ? with : without, ram, sizeof ram);
            }
            CHECK(r[0] == r[1] && reads[0] == reads[1] && !memcmp(with, without, sizeof ram));
        }
    /* One character before the ▼, pressed off a ready frame: the next frame
     * reaches the pause, and the held A does not continue it. */
    hook_on = true;
    load("ab|cd");
    while (drawn < 2) frame(0);
    CHECK(frame(A) == GOES_ON && !ram[MESSAGE_PAUSED] && at == 2);   /* the $FC is not read yet */
    CHECK(frame(A) == GOES_ON && ram[MESSAGE_PAUSED] && at == 3);
    hold(100);
    CHECK(ram[MESSAGE_PAUSED]);
}

static void switch_off(void) {
    deactivate();
    CHECK(!hook_on && !enabled);
    CHECK(type_out(LONG, true) == 4 * (strlen(LONG) + 1));   /* the original pace, held or not */
    activate();
    CHECK(hook_on && enabled);
    CHECK(type_out(LONG, true) == strlen(LONG) + 2);
}

int main(void) {
    activate();
    ready_frames();
    magic_button_masks();
    writes_only_the_timer_and_sound();
    pace();
    release();
    page_break();
    question();
    press_frame_is_stock();
    switch_off();
    printf("text_speed_test: %u checks passed\n", checks);
    return 0;
}
