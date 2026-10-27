/* Widescreen (options.txt widescreen = on | off | <w>:<h>; on, the default, follows the shape of
   the window or screen): Hor+ for Hard Truck 2's 3D world.

   The game draws its world through Direct3D's own transform with a perspective projection made
   from a fixed horizontal view ([EFFECTS] xpyramid in TRUCK.INI) and its viewport's shape: the
   driving camera's viewport is 640x430 under the instrument bar, projection x scale 1.02,
   y scale 1.52 = x scale * 640/430 [verified: run log 2026-10-26]. With widescreen, a perspective
   projection used with a full-width viewport gets its x scale divided by the widening (d3d.c
   ws_projection): the same vertical view, more at the sides. The renderer then spreads such a
   camera's drawing across the wider picture (vk.c: proj_widened, the projection's y/x scale being
   ws_world_aspect); everything else - the instrument bar, the menus (8-bit screens), the small
   views - keeps its 4:3 shape in the middle.

   Off in --test runs (it changes what is drawn) unless HT2_WIDESCREEN is set. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

static int ws_on, allowed;
static float ws_want;            /* a set aspect, 0 = follow the screen */
float ws_aspect = 4.0f / 3.0f;   /* current target aspect (width / height) */
float ws_world_aspect;           /* y/x scale of the widened camera's projection */
int ws_race_depth, ws_hud_depth, ws_menu_depth;   /* (TD6's placement rules: unused here) */

static void parse(void)
{
    const char *v = option_str("widescreen", "on");
    ws_want = 0;
    if (!strcasecmp(v, "off") || !strcasecmp(v, "no") || !strcmp(v, "0")) {
        ws_on = 0;
    } else if (!strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcmp(v, "1")) {
        ws_on = 1;
    } else {
        float a = 0, b = 0;
        if (sscanf(v, "%f:%f", &a, &b) == 2 && a > 0 && b > 0) ws_want = a / b;
        else if (sscanf(v, "%f", &a) == 1 && a > 1.0f) ws_want = a;
        ws_on = ws_want > 0;
        if (!ws_on)
            LOG(LOG_ALWAYS, "options: widescreen \"%s\" is not on, off or an aspect like 16:9", v);
    }
    ws_aspect = ws_on && ws_want > 0 ? ws_want : 4.0f / 3.0f;
}

/* The game's view pyramid: besides the projection ([EFFECTS] xpyramid), the render object keeps
   [RENDER] xpyramid/ypyramid (+0xbe8/+0xbf0, doubles; TRUCK.INI 1.2/0.9) for what it draws at
   all; 0x5df9a0 gives them to the camera (+0x58/+0x54) and 0x4e04c0 to +0x24a0/+0x24a4. With only
   the projection widened, the game still dropped and cut off everything outside the 4:3 view:
   dead strips at the screen's sides and objects popping in there [verified: 2026-10-27, frames at
   2560x1440 with xpyramid 1.2 and 1.6: the side trees whole only with 1.6, the picture's scale the
   same]. While those two run, the horizontal one is widened as the projection is. */
static int pyramid_hook(CPU *c, void (*orig)(CPU *c))
{
    uint32_t o = c->ecx;
    uint64_t keep = R64(o + 0xbe8);
    float k = ws_on && ws_aspect > 4.0f / 3.0f + 0.001f ? ws_aspect / (4.0f / 3.0f) : 1.0f;
    if (k > 1.0f) {
        double x = RF64(o + 0xbe8) * k;
        uint64_t u;
        memcpy(&u, &x, 8);
        W64(o + 0xbe8, u);
    }
    orig(c);
    W64(o + 0xbe8, keep);
    return 1;
}
static int camera_pyramid_hook(CPU *c) { return pyramid_hook(c, F_005df9a0_orig); }
static int world_pyramid_hook(CPU *c) { return pyramid_hook(c, F_004e04c0_orig); }

void widescreen_init(void)
{
    allowed = !deterministic || getenv("HT2_WIDESCREEN");
    parse();
    if (ws_on && !allowed)
        ws_on = 0;
    hook_camera_pyramid = camera_pyramid_hook;
    hook_world_pyramid = world_pyramid_hook;
}

void widescreen_options(void)
{
    if (allowed) parse();
}

int widescreen_on(void) { return ws_on; }

/* The renderer tells us the shape of the picture area (window or screen). */
void widescreen_screen(int w, int h)
{
    if (!ws_on || ws_want > 0 || w <= 0 || h <= 0)
        return;
    float a = (float)w / (float)h;
    ws_aspect = a > 4.0f / 3.0f ? a : 4.0f / 3.0f;
}

/* The x scale a projection gets: divided by the widening for a perspective one used with a
   viewport as wide as the render target */
float widescreen_projection_x(float x, int perspective, uint32_t vp_x, uint32_t vp_w, uint32_t rt_w, float y)
{
    if (!ws_on || ws_aspect <= 4.0f / 3.0f + 0.001f || !perspective || vp_x != 0 || vp_w != rt_w || x == 0.0f)
        return x;
    float wx = x / (ws_aspect / (4.0f / 3.0f));
    ws_world_aspect = y / wx;
    return wx;
}
