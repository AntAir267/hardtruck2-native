/* Hard Truck 2's own fixes and hooks (builds/ht2_gog13.py HOOKS).

   Images (the record 0x5b9d10 makes; 0x5e6f60 reads a .txr's header into it): +4 name, +8 pixels
   in memory, +0x14/+0x16 width/height, +0x18 bytes per pixel (the display's, 0x71f7f4), +0x1a
   mip levels (square images only), +0x20 offset of its data in the .res, +0x34 flags: 1 loaded,
   2 has its surface, 4 needed (loaded when its cluster is), 0x200 data in a mapped file, 0x400
   no copy in memory (no "memfix" after its name in the .res), 0x800 surfaces made later
   [verified: disassembly of 0x5b9ec0, 0x5ba4e0, 0x5ec2e4, 2026-10-26].

   With Direct3D (16-bit display), an image that is not square and has no copy in memory goes to
   0x5d6e00, which copies its pixels from +8 into a system-memory surface (in strips as wide as
   the screen), and +8 is NULL: LOAD.RES's 800x600 c\backCtrl.txr ("noload", no "memfix") crashed
   there on the first loading screen [verified: run 2026-10-26]. Whether the original crashes the
   same way on Windows is not known [guess: it would; nothing on this path loads the pixels].
   The fix: such an image keeps a copy in memory (as "memfix" ones do), so 0x5b9ec0 reads its
   pixels before the surface is made. Square images (textures) are not changed. */
#include "runtime.h"
#include "funcs.h"

static int image_ready_hook(CPU *c)
{
    uint32_t img = c->ecx, flags = R32(img + 0x34);
    uint16_t w = R16(img + 0x14), h = R16(img + 0x16), bpp = R16(img + 0x18);
    if ((flags & 0x405) == 0x404 && !(flags & 0x200) && bpp > 1 && R16(img + 0x1a) == 0 &&
        !(w == 1 && h == 1) && R32(img + 0x20) && !R32(img + 8) && R32(0x71f868)) {
        W32(img + 0x34, flags & ~0x400u);
        LOG(LOG_API, "image %s %ux%u: pixels kept in memory (drawn as a surface)",
            R32(img + 4) ? gstr(R32(img + 4)) : "?", w, h);
    }
    return 0;
}

void game_init(void)
{
    hook_image_ready = image_ready_hook;
}
