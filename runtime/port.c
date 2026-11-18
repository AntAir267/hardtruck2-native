/* Hard Truck 2's hooks into the AntiStatic runtime (third_party/antistatic): its display set-up,
   the game code it adds before the program starts, and its options. */
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"

void ddraw_init(void), widescreen_init(void), widescreen_options(void), padgame_init(void);
void menu_init(void), native_init(void), vk_options(void), padgame_frame(unsigned long frame);
void d3d_native_set(int on);

/* DirectDraw and Direct3D 7 (the toolkit's d3d7), then the port's widescreen and controller code */
void port_graphics_init(void)
{
    ddraw_init();
    widescreen_init();
    padgame_init();
}

/* after game_init (game.c): the MORE OPTIONS page and the hand-written functions */
void port_runtime_ready(void)
{
    menu_init();
    native_init();
}

/* the MORE OPTIONS page changed something (the toolkit's options_changed did its own first) */
void port_options_changed(void)
{
    widescreen_options();
    vk_options();
    d3d_native_set(!strcasecmp(option_str("renderer", "original"), "native"));
}

/* the COM classes the port provides, after the toolkit's DirectSound: DirectPlay with no
   connections (a failed one parks the game's main thread: 0x5dcb90), the AVI reader for the movies,
   DirectShow for the music (the old runtime's order) */
uint32_t dplay_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv);
uint32_t getaviinfo_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv);
uint32_t dshow_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv);
uint32_t port_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv)
{
    uint32_t hr = dplay_cocreate(clsid, iid, ppv);
    if (hr == 0x80040154u)              /* REGDB_E_CLASSNOTREG: not that one */
        hr = getaviinfo_cocreate(clsid, iid, ppv);
    if (hr == 0x80040154u)
        hr = dshow_cocreate(clsid, iid, ppv);
    return hr;
}

/* the controller's menu keys and prompts, each frame (padgame.c) */
void port_pad_frame(unsigned long frame) { padgame_frame(frame); }

/* options.txt: the first start writes every option at its default, commented out, with these
   comments above them */
static const char *const help[][2] = {
    {"display", "display: window or fullscreen (Alt+Enter switches too)"},
    {"resolution", "resolution: native (drawn at the screen's own resolution), original (the game's own "
                   "640x480 / 800x600, scaled up) or a multiple of the game's size such as 2"},
    {"widescreen", "widescreen: on (the 3D world as wide as the screen, the same height of view), off "
                   "(4:3 as shipped), or a shape such as 16:9 or 21:9; the instrument bar and menus stay 4:3"},
    {"antialiasing", "antialiasing: off, or 4x (drawn at twice the size each way and scaled down: smoother "
                     "edges, more work for the graphics card)"},
    {"textures", "textures: original (as the game asks) or smooth (trilinear and anisotropic filtering on "
                 "the 3D world's textures: sharper roads at a distance)"},
    {"colour_precision", "colour_precision: original (16-bit colour, as the game draws) or high (the 3D "
                         "world drawn with more precision, then dithered: less banding in the sky and fog)"},
    {"vsync", "vsync: on (frames wait for the screen) or off"},
    {"frame_limit", "frame_limit: off, screen (3 under the screen's refresh, for G-Sync / FreeSync) or a rate"},
    {"render_thread", "render_thread: on (drawing on a thread of its own: faster) or off"},
    {"rumble", "rumble: on (the game's force feedback on the controller's motors) or off"},
    {"controller_deadzone", "controller_deadzone: the sticks' dead zone against resting drift, percent"},
    {"renderer", "renderer: original (the D3D stand-in lights the meshes) or native (the GPU does: being built)"},
    {NULL, NULL}};

const char *port_option_help(const char *key)
{
    for (int h = 0; help[h][0]; h++)
        if (!strcasecmp(help[h][0], key))
            return help[h][1];
    return NULL;
}

int port_option_default(int i, const char **key, const char **value)
{
    static const char *const def[][2] = {
        {"display", "window"}, {"resolution", "native"}, {"widescreen", "on"}, {"antialiasing", "off"},
        {"textures", "original"}, {"colour_precision", "original"}, {"vsync", "on"},
        {"frame_limit", "off"}, {"render_thread", "on"}, {"rumble", "on"}, {"controller_deadzone", "6"}};
    if (i < 0 || i >= (int)(sizeof def / sizeof def[0]))
        return 0;
    *key = def[i][0];
    *value = def[i][1];
    return 1;
}
