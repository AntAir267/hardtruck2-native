/* port.h: Hard Truck 2: King of the Road for the AntiStatic runtime (third_party/antistatic). The
   game as the program sees it under Windows, and the port's names on the host; none of the game's
   code or data. */
#pragma once
#define PORT_TITLE "Hard Truck 2 native"
#define PORT_SLUG "hardtruck2-native"
#define PORT_ENV "HT2"                      /* HT2_KEYS, HT2_DUMP_FRAMES, HT2_NATIVE, ... */
/* the game installed as C:\KING (GOG's folder name), started there as "C:\KING\king.exe" */
#define PORT_VROOT "C:\\KING"
#define PORT_START_DIR ""
#define PORT_EXE "king.exe"
#define PORT_CMDLINE_ARGS ""
#define PORT_VOLUME_LABEL "KING"
/* king.exe's image ends below 0x700000; allocations from 0x2400000 as the port always made them
   (the hash baselines depend on where the heap lands) */
#define PORT_ARENA_START 0x02400000u
/* the DirectX 7 layer (third_party/antistatic/runtime/d3d7): 8-bit palettized display modes (the
   menus run in one: the mode search at 0x5d4240), and "renderer = native" packing the game's own
   vertex layouts into the 40-byte GPU record (DESIGN.md section 11) */
#define PORT_DDRAW_8BIT 1
#define PORT_D3D_NATIVE40 1
/* --test runs as Hard Truck 2's checks were recorded: other threads' clock readings don't move the
   shared clock [verified: lockstep 2026-10-26, timing values at 0x55cbd60]; the main thread's
   Sleep moves it (the clock is calibrated over a Sleep(2000) at 0x6035f0); the sound clock moves
   at frames only (a stopping buffer's cursor at 0x5c2383) [verified: lockstep 2026-10-26, frame 409] */
#define PORT_TEST_CLOCK_PER_THREAD 1
#define PORT_TEST_SLEEP_MOVES_CLOCK 1
#define PORT_TEST_SOUND_AT_FRAME 1
/* options the renderer starts with: the picture drawn at the screen's own resolution (as the port
   always has; the toolkit's default is the game's own size, scaled up) */
#define PORT_DEFAULT_RESOLUTION "native"
