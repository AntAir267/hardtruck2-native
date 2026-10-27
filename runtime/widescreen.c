/* Widescreen (widescreen.h): not done for Hard Truck 2 yet. The game is drawn at its own aspect;
   these are the renderer's inputs with widescreen off. */
#include "runtime.h"
#include "widescreen.h"

float ws_aspect = 4.0f / 3.0f;
float ws_world_aspect;
int ws_race_depth, ws_hud_depth, ws_menu_depth;

void widescreen_init(void) {}
void widescreen_options(void) {}
int widescreen_on(void) { return 0; }
void widescreen_screen(int w, int h) { (void)w; (void)h; }
