/* MSVFW32 (Video for Windows codecs): Hard Truck 2's movies (kotr.avi, kotrDemo.avi: Indeo Video 5)
   are read by the game itself through mmio and decoded with ICDecompress. Not done yet: the
   codec cannot be opened, so the game goes on without its movies [guess: how it reacts]. */
#include "runtime.h"

static int skip_pending;
void video_skip_press(void) { skip_pending = 1; }
int video_skip_taken(void) { int s = skip_pending; skip_pending = 0; return s; }

API(MSVFW32, ICOpen, 3)
{
    uint32_t t = ARG(0), h = ARG(1);
    LOG(LOG_ALWAYS, "ICOpen('%.4s', '%.4s', mode %u): no codecs yet", (char *)&t, (char *)&h, ARG(2));
    return 0;
}
API(MSVFW32, ICClose, 1) { return 0; }
API(MSVFW32, ICSendMessage, 4) { return 0xffffffffu; }       /* ICERR_UNSUPPORTED */
API(MSVFW32, ICDecompress, 6) { return 0xffffffffu; }
/* a movie playing: its frame due (from PeekMessageA) */
void video_tick(void) {}
