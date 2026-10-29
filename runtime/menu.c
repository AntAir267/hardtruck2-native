/* MORE OPTIONS: the port's own settings (options.txt) as a second page of the game's GRAPHICS
   screen, built from the game's own menu parts (work/decomp/menu/MENU.md) [design point 9].

   The GRAPHICS screen gets an arrow right of AUTODETECT ("aright", as the D3D CARD row's); it
   turns the screen's page over to a panel made like the CONTROLLERS screen's: its art
   (c\backCtrlOpt.txr), a table of text rows (0x512eb0) on its 20-pixel grid, the device box at
   the top holding the page's name between two arrows (the left one goes back), DEFAULT and
   CANCEL. Each row is an option's name; its value is drawn in the row's two small cells by the
   screen's draw (as the screen draws the 3D card's name). Return, a click or Right gives the next
   value, Left the previous one; Escape goes back to the first page.

   A change applies at once (options_changed) and is saved in options.txt; CANCEL (cmd 2) puts
   back the values the page had when it was first opened since the screen was shown. The game's
   own settings (TRUCK.INI) are untouched.

   Hooks (builds/ht2_gog13.py): the screen's constructor 0x515460 (thiscall(parent); the page is
   added after it), OnCommand 0x515ac0 (thiscall(cmd), this = screen+0x40; the original ignores
   ids above 0x8a, so it runs first for every id) and draw 0x5162e0 (thiscall(surface), this =
   screen+0x40) [verified: asm_515460.txt, asm_515ac0.txt, 5162e0.c]. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "funcs.h"
#include "options.h"

/* the game's menu toolkit [verified: MENU.md] */
#define G_NEW        0x631d33u   /* operator new (cdecl) */
#define G_CONTAINER  0x5bcc90u   /* thiscall(text, flags) */
#define G_ADD        0x5bd2a0u   /* thiscall(child, after) */
#define G_FOCUS      0x5bd250u   /* thiscall(child) */
#define G_FIT        0x5bd420u   /* thiscall() */
#define G_GRID       0x5bd630u   /* thiscall(cols (<0: -rows), hgap, vgap) */
#define G_STATE      0x5bcc50u   /* thiscall on a state (template) */
#define G_IMAGE      0x5eea00u   /* (name) -> image */
#define G_BUTTON     0x50e7b0u   /* (name, cmd, x, y) */
#define G_ARROW      0x50dd50u   /* (name, cmd, x, y) */
#define G_TEXTROW    0x512eb0u   /* (text, cmd) */
#define G_TEXT       0x5e97a0u   /* (surface, x, y, font, str, colour, 0) */
#define G_TEXTW      0x5e9710u   /* thiscall on the font (str) */
#define VT_CONTAINER 0x64cc40u
#define VT_TEXTROW   0x64cc0cu
#define S_MENU       0x6755d4u   /* "MENU" */
#define S_ARIGHT     0x675bb8u
#define S_ALEFT      0x675bb0u
#define S_CANC       0x675bdcu   /* "c\canc" */
#define S_DEF        0x675bd4u   /* "c\def" */
#define S_CTRLOPT    0x675bc0u   /* "c\backCtrlOpt.txr" */
#define T_FOCUSED    0x6cd368u   /* (no image) */
#define T_BACKGROUND 0x6cd2f0u   /* +0x1c = 0x6cd30c: its image */
#define T_NORMAL     0x6cd318u
#define MENU_FON     0x6cdc84u
#define LARGE_FON    0x6cdc88u
#define CARD_NAME    0x6cd790u
#define MESS         0x71d7b8u   /* tooltips: mess[cmd + 0x1a] */

enum { CMD_MORE = 0x11c, CMD_BACK, CMD_NEXT, CMD_DEFAULT, CMD_ROW0 = 0x120 };

typedef struct {
    const char *name, *key, *tip, *def;
    const char *vals[7], *shown[7];
} Row;
static const Row rows[] = {
    {"Display", "display", "a window or the whole screen (Alt+Enter too)", "window",
     {"window", "fullscreen"}, {"Window", "Full"}},
    {"Resolution", "resolution", "native: drawn at the screen's own resolution", "native",
     {"native", "original"}, {"Native", "Original"}},
    {"Widescreen", "widescreen", "the 3D world as wide as the screen", "on",
     {"on", "off"}, {"On", "Off"}},
    {"Anti-aliasing", "antialiasing", "4x: smoother edges, more work for the GPU", "off",
     {"off", "4x"}, {"Off", "4x"}},
    {"Textures", "textures", "smooth: sharper textures in the distance", "original",
     {"original", "smooth"}, {"Original", "Smooth"}},
    {"Colours", "colour_precision", "high: the 3D world in full colour, not 16-bit", "original",
     {"original", "high"}, {"Original", "High"}},
    {"Vsync", "vsync", "frames wait for the screen: no tearing", "on",
     {"on", "off"}, {"On", "Off"}},
    {"Frame limit", "frame_limit", "a cap on the frame rate", "off",
     {"off", "screen", "60", "30"}, {"Off", "Screen", "60", "30"}},
    {"Rumble", "rumble", "the controller's motors", "on",
     {"on", "off"}, {"On", "Off"}},
    {"Dead zone", "controller_deadzone", "the sticks' dead zone against drift", "6",
     {"0", "3", "6", "10", "15", "20"}, {"0%", "3%", "6%", "10%", "15%", "20%"}},
    {"Renderer", "renderer", "native: the game's meshes lit on the GPU (new)", "original",
     {"original", "native"}, {"Original", "Native"}},
};
#define NROWS (int)(sizeof rows / sizeof rows[0])
#define COL_ROWS 8      /* the first column's rows; the rest go to the second */

static uint32_t screen, root, pa, page, panel, table, more, back, row_el[NROWS];
static uint32_t gname[NROWS][7], gtext;       /* guest copies of the shown values; a scratch string */
static char *orig[NROWS];                      /* the values when the page was first opened */
static int shown, snapped;

static uint32_t thiscall(CPU *c, uint32_t self, uint32_t fn, int nargs, ...)
{
    uint32_t args[8], ecx = c->ecx;
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++)
        args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    c->ecx = self;
    uint32_t r = guest_callv(c, fn, nargs, args);
    c->ecx = ecx;
    return r;
}

static uint32_t container(CPU *c, uint32_t flags)
{
    uint32_t p = guest_call(c, G_NEW, 1, 0x128u);
    thiscall(c, p, G_CONTAINER, 2, S_MENU, flags);
    W32(p + 0x120, 0);
    W32(p + 0x124, 0);
    W32(p, VT_CONTAINER);
    return p;
}

static void move_to(uint32_t el, int32_t x, int32_t y)     /* as the screens move a container */
{
    int32_t dx = x - (int32_t)R32(el + 0x2c), dy = y - (int32_t)R32(el + 0x30);
    W32(el + 0x2c, (uint32_t)x);
    W32(el + 0x30, (uint32_t)y);
    W32(el + 0x34, R32(el + 0x34) + (uint32_t)dx);
    W32(el + 0x38, R32(el + 0x38) + (uint32_t)dy);
}

/* ------------------------------------------------------------------ values */
static int value_index(int i)
{
    const char *v = option_str(rows[i].key, rows[i].def);
    for (int k = 0; k < 7 && rows[i].vals[k]; k++)
        if (!strcasecmp(v, rows[i].vals[k]))
            return k;
    return -1;      /* something else in options.txt (e.g. resolution = 2): shown as it is */
}

static int set_value(int i, const char *v)
{
    if (!strcasecmp(option_str(rows[i].key, rows[i].def), v))
        return 0;
    option_set(rows[i].key, v);
    LOG(LOG_ALWAYS, "options page: %s = %s", rows[i].key, v);
    return 1;
}

/* applied and kept at once: the screen has no OK, and with the keyboard or a controller it is
   left only through the main menu's buttons */
static void commit(void)
{
    options_changed();
    options_save();
}

static void step(int i, int d)
{
    int n = 0;
    while (n < 7 && rows[i].vals[n]) n++;
    int k = value_index(i);
    k = k < 0 ? 0 : (k + d + n) % n;
    if (set_value(i, rows[i].vals[k]))
        commit();
}

static void snapshot(void)
{
    if (snapped) return;
    for (int i = 0; i < NROWS; i++) {
        free(orig[i]);
        orig[i] = strdup(option_str(rows[i].key, rows[i].def));
    }
    snapped = 1;
}

/* ------------------------------------------------------------------ pages */
static void show_page(CPU *c, int on)
{
    if (!page || on == shown) return;
    shown = on;
    W32(pa + 0x28, on);
    W32(page + 0x28, !on);
    if (on) {
        snapshot();
        thiscall(c, root, G_FOCUS, 1, page);
        thiscall(c, page, G_FOCUS, 1, panel);
        thiscall(c, panel, G_FOCUS, 1, table);
        thiscall(c, table, G_FOCUS, 1, row_el[0]);
    } else {
        thiscall(c, root, G_FOCUS, 1, pa);
        thiscall(c, pa, G_FOCUS, 1, more);
    }
}

static int row_of(uint32_t el)
{
    for (int i = 0; i < NROWS; i++)
        if (row_el[i] == el) return i;
    return -1;
}

/* a row's key (thiscall(key, isChar), the element vtable's +0x28): Left/Right step its value,
   Return the next one; the rest as the game's text row */
static uint32_t row_key_orig;
METHOD(row_key, 2)
{
    uint32_t self = c->ecx, key = ARG(0), is_char = ARG(1);
    int i = row_of(self);
    if (i >= 0 && !is_char && (key == 0x25 || key == 0x27 || key == 0x0d)) {
        step(i, key == 0x25 ? -1 : 1);
        return 0x8000;
    }
    return thiscall(c, self, row_key_orig, 2, key, is_char);
}

/* the page's key: Escape (not taken by what has the focus) goes back to the first page */
static uint32_t page_key_orig;
METHOD(page_key, 2)
{
    uint32_t self = c->ecx, key = ARG(0), is_char = ARG(1);
    uint32_t r = thiscall(c, self, page_key_orig, 2, key, is_char);
    if (!(r & 0x8000) && key == 0x1b && !is_char && shown) {
        show_page(c, 0);
        return 0x8000;
    }
    return r;
}

static uint32_t vtable_with_key(uint32_t vt, void (*fn)(CPU *c), const char *name, uint32_t *orig_fn)
{
    uint32_t v = galloc(0x40);
    for (int k = 0; k < 0x40; k += 4)
        W32(v + k, R32(vt + k));
    *orig_fn = R32(vt + 0x28);
    W32(v + 0x28, thunk_addr(fn, name));
    return v;
}

static void tip(int cmd, const char *s) { W32(MESS + 4 * (uint32_t)(cmd + 0x1a), gstrdup(s)); }

static int graphics_ctor_hook(CPU *c)
{
    screen = c->ecx;
    F_00515460_orig(c);
    root = screen + 0x40;
    pa = R32(root + 0x120);
    if (!pa || R32(root + 0x124) != pa) {
        LOG(LOG_ALWAYS, "options page: the GRAPHICS screen is not as expected, no page");
        return 1;
    }
    uint32_t pa_focus = R32(pa + 0x124);

    /* the way in: an arrow right of AUTODETECT (348,157 154x49) */
    more = guest_call(c, G_ARROW, 4, S_ARIGHT, (uint32_t)CMD_MORE, 515u, 167u);
    thiscall(c, pa, G_ADD, 2, more, 0u);
    thiscall(c, pa, G_FOCUS, 1, pa_focus);

    /* the page, built as the CONTROLLERS screen's (0x513330: 0x51350a..0x513a0a) */
    page = container(c, 0);
    W32(page, vtable_with_key(VT_CONTAINER, page_key, "options_page_key", &page_key_orig));
    thiscall(c, page, G_ADD, 2, guest_call(c, G_BUTTON, 4, S_CANC, 2u, 0x143u, 0x1eau), 0u);
    panel = container(c, 4);
    thiscall(c, panel, G_ADD, 2, guest_call(c, G_BUTTON, 4, S_DEF, (uint32_t)CMD_DEFAULT, 0x15eu, 0x1bbu), 0u);
    uint32_t keep_bg = R32(0x6cd30c);
    W32(0x6cd30c, guest_call(c, G_IMAGE, 1, S_CTRLOPT));
    /* the game gives only the panel's focused state (1) the art, from T_BACKGROUND, the others
       T_FOCUSED, which has none (0x50c85d); with the focus on CANCEL (outside the panel) the panel
       was then not drawn at all and the first page's pixels showed through [verified: frame dump
       2026-10-26]: every state gets the art */
    for (uint32_t k = 0; k < 4; k++)
        thiscall(c, panel + 0x44 + 0x24 * k, G_STATE, 1, T_BACKGROUND);
    W32(0x6cd30c, keep_bg);

    table = container(c, 0);
    uint32_t vt = vtable_with_key(VT_TEXTROW, row_key, "options_row_key", &row_key_orig);
    for (int i = NROWS - 1; i >= 0; i--) {      /* each add goes to the head: last first */
        row_el[i] = guest_call(c, G_TEXTROW, 2, gstrdup(rows[i].name), (uint32_t)(CMD_ROW0 + i));
        W32(row_el[i], vt);
        thiscall(c, table, G_ADD, 2, row_el[i], 0u);
        tip(CMD_ROW0 + i, rows[i].tip);
        for (int k = 0; k < 7 && rows[i].shown[k]; k++)
            gname[i][k] = gstrdup(rows[i].shown[k]);
    }
    thiscall(c, table, G_GRID, 3, (uint32_t)-COL_ROWS, (uint32_t)-180, (uint32_t)-20);
    move_to(table, 0xdd, 0xc7);
    thiscall(c, panel, G_ADD, 2, table, 0u);
    uint32_t next = guest_call(c, G_ARROW, 4, S_ARIGHT, (uint32_t)CMD_NEXT, 0x202u, 0x9du);
    W32(next + 0x28, 1);                        /* one page after this: nothing further */
    thiscall(c, panel, G_ADD, 2, next, 0u);
    back = guest_call(c, G_ARROW, 4, S_ALEFT, (uint32_t)CMD_BACK, 0x103u, 0x9du);
    thiscall(c, panel, G_ADD, 2, back, 0u);
    move_to(panel, 0xc8, 0x99);
    thiscall(c, panel, G_FIT, 0);
    thiscall(c, page, G_ADD, 2, panel, 0u);
    thiscall(c, root, G_ADD, 2, page, 0u);
    thiscall(c, root, G_FOCUS, 1, pa);
    W32(page + 0x28, 1);

    tip(CMD_MORE, "more options: resolution, widescreen, controller");
    tip(CMD_BACK, "back to the 3D card and resolution");
    tip(CMD_DEFAULT, "the port's own defaults");
    gtext = galloc(64);
    LOG(LOG_ALWAYS, "options page: added to the GRAPHICS screen (%d rows)", NROWS);
    return 1;
}

static int graphics_command_hook(CPU *c)
{
    uint32_t cmd = R32(c->esp + 4);
    F_00515ac0_orig(c);
    if (!page) return 1;
    if (cmd == CMD_MORE) {
        show_page(c, 1);
    } else if (cmd == CMD_BACK) {
        show_page(c, 0);
    } else if (cmd == CMD_DEFAULT) {
        int any = 0;
        for (int i = 0; i < NROWS; i++)
            any |= set_value(i, rows[i].def);
        if (any) commit();
    } else if (cmd >= CMD_ROW0 && cmd < (uint32_t)(CMD_ROW0 + NROWS)) {
        step((int)cmd - CMD_ROW0, 1);
    } else if (cmd == 1 && R32(0x674a28)) {        /* left (not vetoed): the changes stay */
        snapped = 0;
        show_page(c, 0);
    } else if (cmd == 2) {                          /* CANCEL: put the values back */
        int any = 0;
        for (int i = 0; snapped && i < NROWS; i++)
            any |= set_value(i, orig[i]);
        if (any) commit();
        snapped = 0;
        show_page(c, 0);
    }
    return 1;
}

static void text(CPU *c, uint32_t surf, int x, int y, uint32_t font, const char *s, uint32_t colour, int centre)
{
    snprintf((char *)(uintptr_t)gtext, 64, "%s", s);
    if (centre)
        x -= (int)thiscall(c, font, G_TEXTW, 1, gtext) / 2;
    guest_call(c, G_TEXT, 7, surf, (uint32_t)x, (uint32_t)y, font, gtext, colour, 0u);
}

/* The panel's two small cells of a row (a binding's key and joystick on the CONTROLLERS screen)
   become one for the value: the divider between them (x 358..366 and 538..546, rows y 190..440)
   is covered with the cell's own inside, copied from the column left of it, on the menu surface
   after the screen's draw (the surface: +0 width, +4 bytes per pixel, +0x18 the pixels [verified:
   pixel probe 2026-10-26, the divider's colours at x 358..366]) */
static void merge_cells(uint32_t surf)
{
    uint32_t px = R32(surf + 0x18), w = R16(surf);
    if (!px || R16(surf + 4) != 1 || w < 600 || R16(surf + 2) < 450) return;
    for (uint32_t y = 190; y < 440; y++) {
        uint32_t line = px + y * w;
        for (int k = 0; k < 2; k++) {
            uint32_t x0 = k ? 538 : 358;
            uint8_t v = (uint8_t)R8(line + x0 - 8);
            for (uint32_t x = x0; x <= x0 + 8; x++)
                W8(line + x, v);
        }
    }
}

static int graphics_draw_hook(CPU *c)
{
    if (!page) return 0;
    uint32_t self = c->ecx, surf = R32(self - 0x34);
    uint8_t name0 = (uint8_t)R8(CARD_NAME);
    if (shown)
        W8(CARD_NAME, 0);                       /* the 3D card's name belongs to the first page */
    for (int i = 0; i < NROWS; i++)              /* text rows are toggles: no "checked" look */
        W32(row_el[i] + 0x11c, R32(row_el[i] + 0x11c) & ~2u);
    F_005162e0_orig(c);
    W8(CARD_NAME, name0);
    uint32_t mf = R32(MENU_FON), lf = R32(LARGE_FON);
    if (!shown) {
        text(c, surf, 547, 185, mf, "More", R32(T_NORMAL + 4), 0);   /* y: the bottom */
        return 1;
    }
    merge_cells(surf);
    text(c, surf, 401, 171 + (int)R32(lf + 0x14) / 2, lf, "More Options", 4, 1);
    uint32_t tx = R32(panel + 0x2c) + R32(table + 0x2c), ty = R32(panel + 0x30) + R32(table + 0x30);
    uint32_t focus = R32(table + 0x124);
    int fh = (int)R32(mf + 0x14);
    for (int i = 0; i < NROWS; i++) {
        uint32_t el = row_el[i];
        int x = (int)(tx + R32(el + 0x2c)), y = (int)(ty + R32(el + 0x30));
        int h = (int)(R32(el + 0x38) - R32(el + 0x30));
        int k = value_index(i);
        const char *v = k >= 0 ? rows[i].shown[k] : option_str(rows[i].key, rows[i].def);
        uint32_t col = R32((el == focus && R32(panel + 0x124) == table ? T_FOCUSED : T_NORMAL) + 4);
        text(c, surf, x + 141, y + h / 2 + fh / 2, mf, v, col, 1);     /* y: the text's bottom */
    }
    return 1;
}

/* ------------------------------------------------------------------ Escape leaves a sub-screen */
/* The main menu's screens (GRAPHICS, CONTROLLERS, SINGLE PLAYER, ...) open inside its circle and
   are left only by clicking another of its buttons: with the keyboard or a controller there was no
   way back. An Escape nothing on the screen takes becomes OnCommand(0x1b) (the screen root's
   input 0x5bf520 returns 0x801b), which no screen uses [verified: the switch tables of 0x514200,
   0x515ac0, 0x51a2d0, 0x51b880 send 0x1b to their default; 0x517e60, 0x518140, 0x51cb60 test
   other ids only]. Here it closes the screen as the main menu's switch does (0x527130): the
   screens that keep settings get OnCommand(1) first (save; they may refuse by clearing
   0x674a28), then the screen is hidden (vt+0x20), the main menu's open screen (+0x174) cleared
   and the main menu activated (vt+0x24), as the EXIT screen's own "no" does (0x517e60).
   Hook: 0x5d8260, a screen's input (thiscall(msg); msg: short type, 0x100 key down, +6 key). */
static void close_screen(CPU *c, uint32_t scr, uint32_t main)
{
    uint32_t rootp = R32(scr + 0x38);
    if (scr == R32(0x6cdbd0) || scr == R32(0x6cdbcc) || scr == R32(0x6cdc38)) {
        W32(0x674a28, 1);
        thiscall(c, rootp, R32(R32(rootp) + 0x3c), 1, 1u);
        if (!R32(0x674a28)) return;
    }
    if (R32(main + 0x174) == scr) {
        thiscall(c, scr, R32(R32(scr) + 0x20), 0);
        W32(main + 0x174, 0);
        thiscall(c, main, R32(R32(main) + 0x24), 0);
    }
    LOG(LOG_ALWAYS, "menus: Escape closed a screen (%08x)", scr);
}

static int screen_input_hook(CPU *c)
{
    uint32_t scr = c->ecx, msg = ARG(0), main = R32(0x6cdbe0);
    if (!main || scr == main || R32(main + 0x174) != scr || R16(msg) != 0x100 || R16(msg + 6) != 0x1b)
        return 0;
    /* 0x5d8260 itself, with 0x1b taken here */
    uint32_t rootp = R32(scr + 0x38);
    uint32_t r = thiscall(c, rootp, R32(R32(rootp) + 0x38), 1, msg), done = (r & 0x8000) != 0;
    uint32_t cmd = r & 0xffff7fffu;
    if (done && cmd == 0x1b)
        close_screen(c, scr, main);
    else if (done && cmd)
        thiscall(c, rootp, R32(R32(rootp) + 0x3c), 1, cmd);
    api_return(c, done, 1);
    return 1;
}

void menu_init(void)
{
    hook_screen_input = screen_input_hook;
    hook_graphics_ctor = graphics_ctor_hook;
    hook_graphics_command = graphics_command_hook;
    hook_graphics_draw = graphics_draw_hook;
}
