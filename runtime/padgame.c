/* What the port does with controllers in Hard Truck 2 (pad.h).

   The game reads joysticks through DirectInput (c_dfDIJoystick; it lists them once at start-up)
   and maps them with kmjin.ini: one section per DirectInput product name, else a generic one by
   axes and buttons, each line an action and its input ("VVK_CARV_ACCEL = J A2-": axis 2 towards
   its minimum; "J B1" button 1; "J povB1" the hat's first direction) [verified: kmjin.ini, the
   format strings at 0x6834xx, run logs 2026-10-26]. Axes count from 1 in DIJOYSTATE's order
   (X, Y, Z, Rx, Ry, Rz); the hat's directions, as the game decodes them (0x4e0ea7, exact angles
   only): 1 up, 2 left, 3 down, 4 right [verified: jump table 0x4e0ff8; the numbering from 1 is a
   guess from the buttons'].

   The controller pad.c shows (X = left stick across, Y = the triggers together, Z = left stick up and down, Rx/Ry = right stick; buttons 1 A, 2 B, 3 X, 4 Y, 5 LB,
   6 RB, 7 Back, 8 Start, 9 left stick, 10 right stick; the D-pad as the hat) is named PAD_NAME
   for every controller, and the user folder's kmjin.ini (the game's own plus our section, written
   at start-up) gives it the Xbox layout below. RT accelerates with "J A2+" here, though the
   CONTROLLERS screen then shows A2- for it (and the generic profiles' "ACCEL = J A2-" is LT on
   this controller) [verified: race runs 2026-10-26, LT held with A2- accelerated]. The game's CONTROLLERS screen shows it and keeps
   every binding changeable.

   Menus (8-bit screens): the D-pad or left stick moves as the arrow keys do, A is Return, B is
   Escape. While driving, Start is Escape and the right stick and its click are the camera keys
   (below: the game takes only buttons for those actions, and button 10 not at all) [verified:
   the CONTROLLERS screen showed no binding for axes 4/5 or button 10 on them]. The game reads the keyboard as window messages, so
   these go to the window like real keys (win_key). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "input.h"
#include "pad.h"
#include "gfx.h"

#define PAD_NAME "Xbox Controller"

static const char profile[] =
    "[" PAD_NAME "]\r\n"
    ";Hard Truck 2 native: the Xbox layout\r\n"
    "type = 0x10204\r\n"
    "nums = 6 16 1\r\n"
    "VVK_CARV_LEFT              = J A1-      (VA_JOYSTICK_00)\r\n"
    "VVK_CARV_RIGHT             = J A1+      (VA_JOYSTICK_00)\r\n"
    "VVK_CARV_ACCEL             = J A2+      (VA_JOYSTICK_01)\r\n"
    "VVK_CARV_BRAKE             = J A2-      (VA_JOYSTICK_01)\r\n"
    "VVK_CARV_GEARUP            = J B6       (VK_JOYBUTTON_05)\r\n"
    "VVK_CARV_GEARDOWN          = J B5       (VK_JOYBUTTON_04)\r\n"
    "VVK_CARV_HANDBRAKE         = J B1       (VK_JOYBUTTON_00)\r\n"
    "VVK_HORN                   = J B2       (VK_JOYBUTTON_01)\r\n"
    "VVK_START_ENGINE           = J B3       (VK_JOYBUTTON_02)\r\n"
    "VVK_ATTACH_SEMITRAILER     = J B4       (VK_JOYBUTTON_03)\r\n"
    "VVK_MAP                    = J B7       (VK_JOYBUTTON_06)\r\n"
    "VVK_BEAMS                  = J B9       (VK_JOYBUTTON_08)\r\n"
    "VVK_PAGER_NEXT             = J povB1    (VK_JOYPOVBUTTON_00)\r\n"
    "VVK_WIPER                  = J povB3    (VK_JOYPOVBUTTON_02)\r\n"
    "VVK_BUY                    = J povB4    (VK_JOYPOVBUTTON_03)\r\n"
    "VVK_SELL                   = J povB2    (VK_JOYPOVBUTTON_01)\r\n"
    "\r\n";

const char *padgame_device_name(const char *name)
{
    (void)name;
    return PAD_NAME;
}

/* Rumble (options.txt rumble = on | off, default on): the controller is a force-feedback joystick,
   and the game's effects (cFFBEF_offRoad: periodic, on rough ground; constant forces) play on its
   motors (dinput.c). Its wheel-centring condition effects are not offered (it carries on
   without them, as with a joystick that has none). */
int ifc_rumble_wanted(void)
{
    return strcasecmp(option_str("rumble", "on"), "off") != 0;
}

/* The user folder's kmjin.ini: the game's with our section first (written again when the game's
   changes or ours is missing; a section the player edited is kept as it is) */
static void write_profiles(void)
{
    char gpath[1200], upath[1200];
    snprintf(gpath, sizeof gpath, "%s/kmjin.ini", game_dir);
    snprintf(upath, sizeof upath, "%s/kmjin.ini", user_dir);
    FILE *u = fopen(upath, "rb");
    if (u) {
        char line[256];
        int have = 0;
        while (fgets(line, sizeof line, u))
            if (!strncmp(line, "[" PAD_NAME "]", sizeof PAD_NAME + 1)) { have = 1; break; }
        fclose(u);
        if (have)
            return;
    }
    FILE *g = fopen(gpath, "rb");
    if (!g) {
        LOG(LOG_ALWAYS, "controller: no kmjin.ini in the game folder");
        return;
    }
    fseek(g, 0, SEEK_END);
    long n = ftell(g);
    fseek(g, 0, SEEK_SET);
    char *text = malloc(n > 0 ? n : 1);
    size_t got = fread(text, 1, n, g);
    fclose(g);
    u = fopen(upath, "wb");
    if (u) {
        fwrite(profile, 1, sizeof profile - 1, u);
        fwrite(text, 1, got, u);
        fclose(u);
        LOG(LOG_ALWAYS, "controller: the Xbox layout written to %s", upath);
    }
    free(text);
}

/* ------------------------------------------------------------------ menus by pad */
/* menus: arrows, Return, Escape, Tab; driving: Escape (Start) and the keys of the camera actions,
   which the game binds to buttons only: the right stick looks left/right (Del, PgDn) and moves
   the camera closer/further (Home, End), the right stick's click is the outside view (V) - the
   game's keyboard defaults (its CONTROLLERS screen) */
enum { MK_UP, MK_DOWN, MK_LEFT, MK_RIGHT, MK_ENTER, MK_ESCAPE, MK_TAB, MK_LOOKL, MK_LOOKR, MK_NEAR,
       MK_FAR, MK_OUTSIDE, MK_N };
static const int mk_dik[MK_N] = {0xc8, 0xd0, 0xcb, 0xcd, 0x1c, 0x01, 0x0f, 0xd3, 0xd1, 0xc7, 0xcf, 0x2f};
static int mk_down[MK_N];

static void mk_set(int k, int down)
{
    if (mk_down[k] == down)
        return;
    mk_down[k] = down;
    extern void win_key(int dik, int down);
    win_key(mk_dik[k], down);
}

void padgame_frame(unsigned long frame)
{
    (void)frame;
    int want[MK_N] = {0};
    static int was_menus = -1, latched;
    if (pad_count()) {
        int menus = gfx_mode.set && gfx_mode.bpp == 8;     /* the 8-bit screens are the menus */
        /* a button held while the screen changes (Start skipping a movie) acts again only once
           everything is let go, so it does not also press the new screen's button */
        int any = pad_held(PAD_A) || pad_held(PAD_B) || pad_held(PAD_START);
        if (was_menus >= 0 && menus != was_menus && any)
            latched = 1;
        was_menus = menus;
        if (latched && !any)
            latched = 0;
        if (latched) {
            for (int k = 0; k < MK_N; k++)
                mk_set(k, 0);
            return;
        }
        int lx = pad_axis_value(PAD_LX), ly = pad_axis_value(PAD_LY);
        if (menus) {
            want[MK_UP] = pad_held(PAD_DUP) || ly < -20000;
            want[MK_DOWN] = pad_held(PAD_DDOWN) || ly > 20000;
            want[MK_LEFT] = pad_held(PAD_DLEFT) || lx < -20000;
            want[MK_RIGHT] = pad_held(PAD_DRIGHT) || lx > 20000;
            want[MK_ENTER] = pad_held(PAD_A) || pad_held(PAD_START);
            want[MK_ESCAPE] = pad_held(PAD_B);
            /* RB: Tab, the menus' next group (the arrows only move inside a group: a screen's
               panel, its button row) [verified: 0x5bd960 case 9] */
            want[MK_TAB] = pad_held(PAD_RB);
        } else {
            int rx = pad_axis_value(PAD_RX), ry = pad_axis_value(PAD_RY);
            want[MK_ESCAPE] = pad_held(PAD_START);
            want[MK_LOOKL] = rx < -16000;
            want[MK_LOOKR] = rx > 16000;
            want[MK_NEAR] = ry < -16000;
            want[MK_FAR] = ry > 16000;
            want[MK_OUTSIDE] = pad_held(PAD_RSTICK);
        }
    }
    for (int k = 0; k < MK_N; k++)
        mk_set(k, want[k]);
}

/* A new player profile (0x5275c0 finds no "<name>.pl1") starts with the controller as its device
   when one is connected: control = 1, the first joystick ([EFFECTS] control in TRUCK.INI, kept in
   the game object's +0x500; 0 is the keyboard only) [verified: 0x5275c0, 0x5134ba, a race with
   control = 1 driven by the controller]. A profile that exists keeps its choice. */
#include "funcs.h"
static int profile_open_hook(CPU *c)
{
    uint32_t drv = R32(0x6cdc38);
    int fresh = 0;
    if (drv) {
        char name[64], path[1300];
        snprintf(name, sizeof name, ".\\%s.pl1", gstr(drv + 0x278));
        fresh = !path_to_host(name, path, sizeof path, 0);
    }
    F_005275c0_orig(c);
    uint32_t game = R32(0x6cdbcc);
    if (fresh && pad_count() && game && R32(game + 0x500) == 0) {
        /* as the CONTROLLERS screen's arrow does (0x514200 case 0x8f): select device 1 (0x530730,
           cdecl) and load its bindings (0x512ce0, on the game object); then keep the choice */
        W32(game + 0x500, 1);
        guest_call(c, 0x530730, 1, 1u);
        uint32_t ecx = c->ecx;
        c->ecx = game;
        guest_call(c, 0x512ce0, 0);
        c->ecx = ecx;
        guest_call(c, 0x603330, 4, 0x671998u, 0x675768u, 1u, R32(0x6957c8));  /* [EFFECTS] control = 1 */
        LOG(LOG_ALWAYS, "controller: a new profile, its device is the controller");
    }
    return 1;
}

void padgame_init(void)
{
    write_profiles();
    hook_profile_open = profile_open_hook;
}
