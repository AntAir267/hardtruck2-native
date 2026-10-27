/* What the port does with controllers in Hard Truck 2 (pad.h): nothing game-specific yet; the game
   sees the controller through DirectInput as a joystick (pad.c, dinput.c). */
#include "runtime.h"
#include "options.h"
#include "input.h"
#include "pad.h"

int ifc_rumble_wanted(void) { return 0; }
void padgame_init(void) {}
void padgame_frame(unsigned long frame) { (void)frame; }
