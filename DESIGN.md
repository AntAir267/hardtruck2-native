# Hard Truck 2 native: design notes

A native Linux port of Hard Truck 2: King of the Road (SoftLab-NSK / Buka, 1998-2002; "Дальнобойщики
2"), GOG 1.3, by static recompilation of the original king.exe: the game's own x86 code is
translated into C and built against a stand-in for Windows and DirectX 7 on SDL3 + Vulkan. The
same method as 1NSANE: Recompiled and td6-native, whose translator and runtime this project forks.
Bring your own game: the repository holds only our tools and runtime, never the game's code, data
or the generated C.

## Decisions

### 1. Goal (Ant, 2026-10-26)
"Decompile this to run on Linux with Vulkan and add native controller support." As the other
ports: faithful by default (physics, AI, timing, look; original bugs kept except crashes), extras
as options.

### 2. Translation and runtime: fork of td6-native (2026-10-26)
Why: king.exe is the same kind of program as TD6.exe (32-bit MSVC, statically linked CRT,
DirectX 7 through DirectDrawCreateEx). tools/ is td6-native's translator unchanged at the fork;
runtime/ is its runtime minus the TD6-only parts (bink.c, wma.c; widescreen.c and padgame.c are
empty stand-ins until the game's own hooks are found). builds/ht2_gog13.py holds this build's
knowledge (addresses only).

### 3. The game and its files
- Source: Ant's GOG installer setup_hard_truck_2_king_of_the_road_1.3_amd_rdna_fix_(52932).exe in
  ~/Downloads/Games, unpacked with innoextract (never run) into
  ~/.local/share/hardtruck2-native/game (554 MB; the installer's tmp/, app/, commonappdata/ and
  __redist/ left out). GOG's ddraw.dll there is its "DirectX 1-7 wrapper" (dxcfg.exe/ini
  configure it): not used, our runtime is DDRAW.dll.
- king.exe: 2,908,160 bytes, sha256 4412a5f6..., linked 2002-03-19, base 0x400000, no
  relocations, entry 0x61f57a. .text 0x401000 (2.3 MB), .rdata 0x64a000, .data 0x66b000,
  .rsrc 0x77f000. MSVC with a static CRT and MFC-style classes (CTempGdiObject); many debug
  strings with C++ function signatures (AI, players, orders, police, mafia).
- Imports: KERNEL32 124, USER32 114, GDI32 36, DDRAW (DirectDrawCreateEx, DirectDrawEnumerateA),
  DINPUT (DirectInputCreateA), DSOUND #1, WINMM (timeGetTime, mmio*), MSVFW32 (ICOpen,
  ICDecompress, ICSendMessage, ICClose: the movies), WSOCK32 (8 by ordinal: network play),
  ADVAPI32 (registry writes), ole32 (CoCreateInstance, structured storage), OLEAUT32 #4 #6,
  WINSPOOL (printer), SHELL32 (drag and drop), COMCTL32 #17. DirectPlay error strings: network
  play through DirectPlay (CoCreateInstance) [guess].
- Settings: TRUCK.INI ([ENV] xres/yres/fullscreen/d3d, [RENDER] fog, xpyramid/ypyramid = the
  view's half-angles [guess], [EFFECTS], [JOYSTICK], [DRVn] per display driver), kmjin.ini (keys
  [guess]), WEATHER.INI.
- Movies: kotr.avi, kotrDemo.avi (and KotrNewgame.avi named in the exe), Indeo Video 5 (IV50),
  decoded by the game through Video for Windows (ICDecompress): FFmpeg has an Indeo 5 decoder.
- Music: MUSIC\track01l.mp3..track12, found with "?*.mp3"; how the game decodes MP3 is not known
  yet (no ACM or DirectShow import; maybe loaded at run time) [guess].
- Data: COMMON\, ENV\ (maps: *.b3d models, *.res resources, *.way roads), MENV, MTB, MTR, *.sch
  schemes, *.bne, *.fon fonts, LOAD.RES, MENU.SFT.

### 4. What the runtime adds for this game (2026-10-26)
- Translator: `enter`, `xlatb`, 16-bit `shld/shrd`, `bt/bts/btr/btc` (memory with a register
  offset: the CRT's charset bitmaps), moves from/to `ds`/`es` (flat, 0x23), and the MMX of the one
  routine that has it (0x5fe182, a colour fade: movd, punpcklbw, pmullw, paddusw, psraw,
  packsswb, emms; registers in CPU.mm[]). The 8-bit `div` had a `%%` bug (fixed here, also in
  td6-native's copy [guess: same bug there, not checked]). Result: 12,271 functions, no
  untranslated instruction. Jump tables in builds/ht2_gog13.py [verified: disassembly].
- Windows: king.exe is an MFC 4.2 application (AfxFrameOrView42s windows, CBT-hook subclassing,
  OnIdle game loop). USER32/GDI32 and kernel32x.c come from li2-native (LA Rush's multi-window
  layer); inifile.c (li2-native's, plus section/key listing: kmjin.ini's joystick sections are
  read by listing their keys); winmisc.c (string resources, OLE BSTRs, file mappings in files.c,
  and failing stand-ins for WSOCK32, WINSPOOL, drag and drop); LoadLibrary/GetProcAddress find
  any runtime function by name (li2-native's approach): the game loads dsound.dll itself.
- dplay.c: an IDirectPlay4A with no connections. The game stops with a fatal error without one
  (D1gate.cpp) [verified: run]. No network play.
- storage.c: OLE structured storage (real compound files, read and written): player profiles
  (DRIVER.pl1: streams settings, ver, $dev_000, storage listofgames) and the shipped movie.mvp.
  Checked: our files' directories read back correctly with an independent script.
- mmio: opening by a file handle (adwInfo[0]) as well as by name and from memory.
- ddraw: SetCooperativeLevel accepts NORMAL with EXCLUSIVE|FULLSCREEN (the game asks for both).
- game.c: hooks for this game. image_ready (0x5b9ec0): see the comment there (an on-demand,
  non-square image drawn as a surface had no pixels in Direct3D mode).
- Renderer: the shipped TRUCK.INI has d3d=off (the 8-bit software renderer, which draws through
  GDI in a window here: nothing shown yet); d3d=on uses DirectDraw 7 + Direct3D 7 (HAL device,
  640x480x16 for the menus) on the Vulkan renderer. Tests use a TRUCK.INI with d3d=on in the
  test user folder.

### Open (see the handoff)
- Movies: the game reads kotr.avi itself (mmio) and decodes through MSVFW32 (Indeo 5), and asks
  GetAVIInfo.dll (an ATL COM server shipped with the game, CLSID {568df44f-...}, interface
  {5f3b918f-...} through IID 0x67e348 [guess: which is the class and which the interface]) for the
  file's layout. Both missing: the game skips the movies with "avi subsystem error".
- C++ exceptions: not supported (tdor3-native's eh.c + tools/eh.py would bring them).
- Reference: run the original under Proton to compare (no Wine or virtual display installed;
  GE-Proton and gamescope are).

### 5. Renderer and settings (2026-10-26)
- The game as GOG ships it (TRUCK.INI d3d=off, cres=1) is the port's: menus are 8-bit
  palettized DirectDraw at 800x600 (mode search 0x5d4240: 1 byte per pixel =
  DDPF_PALETTEINDEXED8), presented each frame by Blt(primary <- an 800x600 P8 system-memory back
  buffer); the loading screen too; the 3D world is Direct3D 7 (640x480x16, DrawPrimitive /
  DrawIndexedPrimitive[Strided]) even with d3d=off [verified: run logs, frame dumps]. The runtime
  offers 8-bit modes and the Vulkan upload turns indexes into colours (ddraw_palette_argb;
  SetEntries marks 8-bit surfaces dirty). The software rasterizer (swrast, used when Vulkan is
  off, e.g. a plain --test run) has no palettes: test runs that look at frames set
  HT2_GPU_OFFSCREEN=1.
- d3d=on: the loading screen becomes a Direct3D session (640x480x16, or 800x600x16 with cres=2)
  and its first overlay image crashes at 0x5d6f45 (a non-square on-demand 16-bit image copied
  from NULL). The original does the same under Wine (GE-Proton 10-32, gamescope headless,
  work/ref-wine: its logs; the game copy and Proton prefix were deleted - to redo, copy the game folder there with the test TRUCK.INI, drop GOG's ddraw.dll, and run GE-Proton's proton script with STEAM_COMPAT_DATA_PATH in that folder under gamescope --backend headless) [verified]: GOG ships d3d=off probably for this. game.c's image_ready hook
  avoids the crash, but the menus then show the loading screen's 16-bit surfaces in their 8-bit
  screens (black). Not pursued: d3d=off already gives Direct3D for the 3D world.
- fullscreen must be on: the game's windowed mode draws through GDI (nothing shown). game.c makes
  the user folder's TRUCK.INI from the game's at the first start and keeps fullscreen=on; the
  port's own window (or full screen) shows the game's picture scaled.
- The test clock: the game calibrates its clock over Sleep(2000) at start-up (0x6035f0, against
  timeGetTime); --test runs' Sleep on the main thread moves the virtual clock (other threads'
  Sleep is real time: the music thread retries DirectShow every 0.4 s).

### 6. Controllers (2026-10-26, padgame.c)
- The game reads joysticks with DirectInput (c_dfDIJoystick, listed once at start-up; dead zone
  20 % except X) and maps them with kmjin.ini (section = DirectInput product name; "J A2+",
  "J B1", "J povB1"). The keyboard comes through window messages.
- Every controller is shown as "Xbox Controller"; the user folder's kmjin.ini is the game's with
  our section first (written when missing). Layout: left stick steers, RT accelerates, LT brakes
  (the triggers are one axis: the profile says ACCEL = J A2+, which the game's screen shows as
  A2-), RB/LB gear up/down, A parking brake, B horn, X start engine, Y hook trailer, Back map, L3
  headlights, D-pad up pager / down wipers / right load cargo / left unload. The camera actions
  take buttons only (axes 4/5 and button 10 are not accepted there), so while driving the right
  stick and R3 are keys: look left/right (Del/PgDn), camera closer/further (Home/End), outside
  view (V). Start is Escape while driving; in the 8-bit menus D-pad/left stick are the arrow
  keys, A/Start Return, B Escape.
- A new player profile's device is the controller when one is connected (hook on 0x5275c0: as
  the CONTROLLERS screen's arrow, 0x530730(1) and 0x512ce0 on the game object, then [EFFECTS]
  control=1). An existing profile keeps its choice.
- Verified headless with the scripted controller: menus, a fresh install driving with RT and
  the left stick, look right, outside view. Not yet tried with a real pad by Ant.
- Rumble (options.txt rumble = on|off, default on): the controller is a force-feedback joystick.
  The game's effects [verified: its log strings and CreateEffect calls]: cFFBEF_offRoad (sine,
  rough ground: the big motor, up to full on grass), cFFBEF_bounce (short constant forces: bumps),
  a square wave, cFFBEF_Constant (endless constant force: a wheel's steering torque, left off the
  motors), ToCenter / ResistOnMove (spring, damper: not offered, the game goes on without them).

### 7. Resolution and widescreen (2026-10-26)
- resolution = native (the default here): the renderer draws render targets at the size the
  picture has in the window (vk.c, from td6-native); the game keeps its own 640x480 / 800x600
  coordinates. Its GRAPHICS screen still offers 640x480 to 1600x1200 at 16 bits.
- widescreen = on (default; off, or an aspect such as 16:9): Hor+ (widescreen.c). The world
  camera's projection is made by the game from a fixed horizontal view ([EFFECTS] xpyramid,
  camera +0x58; y scale = x scale * viewport width/height: 1.02152 / 1.52041 for the 640x430
  driving viewport) [verified: run log]. d3d.c keeps the program's x scale and, for a
  perspective projection with a full-width viewport, uses it divided by the widening
  (widescreen_projection_x); vk.c spreads draws made with such a projection across the wider
  picture (proj_widened) and keeps everything else 4:3 in the middle: the instrument bar, the
  pager, messages, menus (8-bit screens), the small 101x76 view (its own projection). GetTransform
  gives the program its own matrix back.
- Culling: draw counts at a given frame are the same at 4:3 and 32:9, and a 32:9 frame shows no
  missing objects at its sides [guess: the game's view-based culling, if any, is wide enough;
  the camera's [RENDER] xpyramid/ypyramid (0.8/0.6 defaults, TRUCK.INI 1.2/0.9) go to the camera
  object too, 0x5df9c5]. Off in --test runs unless HT2_WIDESCREEN=1.

### 9. Options in the game's own menu (Ant, 2026-10-26)
GAME-GOALS.md (~/DevWork/Games) asks for every setting a port adds to be in the game's own
options menu, in its style; options.txt stays for testing and power users. Ant chose the
game's own menu over an overlay. Defaults: GAME-GOALS says the original game; Ant chose to keep
native resolution and widescreen on by default for this port (everything else original).
Under way: mapping the GRAPHICS screen's code and the menu toolkit (work/decomp/menu/MENU.md)
for a new page.

### 8. options.txt (2026-10-26)
Written at the first start (outside test runs) listing every option at its default, commented
out (options.c): display, resolution (native default), widescreen (on), antialiasing (off | 4x),
textures (original | smooth), colour_precision (original | high), vsync, frame_limit,
render_thread, rumble, controller_deadzone. Checked in driving frames: 4x (drawn at 2x each way),
smooth and high change the picture and run. Not listed: antialiasing taa / fsr / dlss (td6-native's
temporal anti-aliasing follows TD6's scene; not checked here, FSR 2 and DLSS absent on the 890M),
menu_fps (no menu/driving test for this game: it does nothing), render_scale (temporal only).

## Handoff (2026-10-26)
State: playable headless and in a window: menus, loading, driving, keyboard and controller.
Installed with make install (applications menu "Hard Truck 2: King of the Road",
~/.local/opt/hardtruck2-native). Private GitHub repository AntAir267/hardtruck2-native (branch master), made 2026-10-26 at
Ant's request.
Music: dshow.c (DirectShow filter graph: RenderFile decodes the MP3 whole with FFmpeg, played as
a runtime voice; Run/Stop/GetState, EC_COMPLETE for the next track, IBasicAudio volume); the
game plays music in the world only. Movies: getaviinfo.c (IGetAVIInfo natively, after
work/decomp/avi/SPEC.md) + vfw.c (Indeo 5 by FFmpeg behind ICOpen/ICSendMessage/ICDecompress,
24-bit output as the game asks); the intro movie plays with its sound; Start (Escape) skips it.
The BSTR helper 0x5c1090 relies on MultiByteToWideChar writing what fits before failing.
Missing: a test with a real controller; the instrument bar stays 4:3 (black at its sides in widescreen).
Tests: build/ht2 --test --user work/testuser (fresh folder: TRUCK.INI and kmjin.ini are made);
HT2_PAD="frame:BUTTON*hold,frame:AXIS=value*hold" (no PLUG: connected from start-up), HT2_KEYS,
HT2_GPU_OFFSCREEN=1 HT2_DUMP_FRAMES=<dir> HT2_DUMP_EVERY=<n> [HT2_DUMP_FROM=<frame>]. Main menu:
A A starts a new game (driving from about frame 1400 at the virtual 60 fps). Ghidra snapshots in
work/rea (king.exe, GetAVIInfo.dll). Lockstep: tools/lockstep.py --seed <folder with TRUCK.INI>.

## Progress

- 2026-10-26: project made, installer unpacked, translator and runtime forked from td6-native.
  Headless --test runs go: MFC start-up, TRUCK.INI and kmjin.ini, LOAD.RES, DirectPlay, the
  player profile, DirectDraw/Direct3D 7 at 640x480x16, menu fonts, the loading screen's overlay.
- 2026-10-26 (night): native resolution and Hor+ widescreen by default.
- 2026-10-26 (evening): music (DirectShow) and movies (IGetAVIInfo + Indeo 5).
- 2026-10-26 (later): menus on Vulkan (8-bit palettes); the keyboard reaches the game (the
  window layer lost its main window when the first frame window was destroyed); loading screen
  and driving; the test clock fix; controllers; make install.
