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

### Open
- Movies: the game reads kotr.avi itself (mmio) and decodes through MSVFW32 (Indeo 5), and asks
  GetAVIInfo.dll (an ATL COM server shipped with the game, CLSID {568df44f-...}, interface
  {5f3b918f-...} through IID 0x67e348 [guess: which is the class and which the interface]) for the
  file's layout. Both missing: the game skips the movies with "avi subsystem error".
- C++ exceptions: not supported (tdor3-native's eh.c + tools/eh.py would bring them).
- Reference: run the original under Proton to compare (no Wine or virtual display installed;
  GE-Proton and gamescope are).

### Display modes the game uses [verified: run logs 2026-10-26]
- TRUCK.INI [ENV]: d3d (on = Direct3D 7), fullscreen, xres/yres, cres (colour bytes). With
  d3d=on, fullscreen=on: the loading screen is a Direct3D session (640x480x16, or 800x600x16 with
  cres=2), then DirectDraw is released and made again for the menus at 800x600x8 with a
  256-colour palette (mode search 0x5d4240: 1 byte per pixel = DDPF_PALETTEINDEXED8). The
  runtime offers 8-bit modes now and the Vulkan upload turns indexes into colours
  (ddraw_palette_argb; SetEntries marks 8-bit surfaces dirty).
- Menus present by Blt(primary <- an 800x600 P8 system-memory back buffer) each frame.

## Handoff (2026-10-26, end of the first session)
State: builds clean; a headless --test run with work/testuser/TRUCK.INI (d3d=on,
fullscreen=on) runs 600 frames into the menu loop with no crash. Frames dump black.
Next problem: every menu frame Blts surf#91 (640x1200 RGB565: the loading screen's 16-bit strip
surface of c\backCtrl.txr, from the released Direct3D-session DirectDraw) into the 8-bit back
buffer; RGB565 -> P8 gives black. The image record still has flag 2 (has a surface) and +0x48
pointing at the old surface, so nothing rebuilds it for 8-bit. To find: what the game does to
its images when it releases DirectDraw (Windows would hand back DDERR_SURFACELOST/invalid
objects for the old surfaces; the earlier "ddrw mask fail ... invalid DirectDraw object"
warnings are those calls) and whether game.c's image_ready fix (pixels kept for non-square
images at 16-bit) is right or should go once the mode switch is understood. 0x5da550 is the
.b3d world loader (it rebuilds surfaces of a module's images), not the menu's.
Ghidra: REA snapshot work/rea/king.rea-snapshot (reopen king.exe with it). work/king.asm is
objdump's disassembly (regenerate if missing).
Test command shape: build/ht2 --test --user work/testuser --frames N --log ddraw,d3d,win;
HT2_DUMP_FRAMES=<dir> HT2_DUMP_EVERY=<n> for frame dumps (PPM).

## Progress

- 2026-10-26: project made, installer unpacked, translator and runtime forked from td6-native.
  Headless --test runs go: MFC start-up, TRUCK.INI and kmjin.ini, LOAD.RES, DirectPlay, the
  player profile, DirectDraw/Direct3D 7 at 640x480x16, menu fonts, the loading screen's overlay.
