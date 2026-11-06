# Hard Truck 2: Recompiled

Hard Truck 2: King of the Road (SoftLab-NSK), translated from its Windows program into C and
rebuilt as a native game for Linux and Windows: Vulkan graphics, SDL3 input and sound, controller
support with rumble, and a MORE OPTIONS page inside the game's own options menu (resolution,
widescreen, anti-aliasing, texture filtering, vsync, frame limit, dead zone, renderer). The
original look is always one option away.

**None of the game is in this repository.** You need your own copy from GOG (Hard Truck 2 - King
of the Road, the 1.3 release). The translation of its king.exe is made on your own computer.

## State

Playable: menus, driving, keyboard and controller, music and movies work, and the translated
code gives the same results as the original in the project's checks (lockstep: the game's memory
compared frame by frame with the original code run in an emulator; and per-function tests). It has **not
yet been played start to finish**, so expect bugs; reports are welcome.

Known gaps: the instrument bar stays 4:3 in widescreen; no multiplayer; the game's own control
profiles are keyboard-only (the controller has its own layout). The Windows build passes its
checks under Wine but has not been run on real Windows yet.

## Playing

There is no published download yet. When there is, it works like this:

1. Linux: run the AppImage. Windows: unzip the download and run hardtruck2-recompiled.exe
   (needs a graphics driver with Vulkan 1.3).
2. On first run it asks for the GOG installer (setup_hard_truck_2_...exe) or a folder where the
   game is installed, and finds it on its own in Downloads or the usual games folders. It checks
   every file against the GOG 1.3 release, then translates and compiles the game on your
   computer (a few minutes, once). The installer is only unpacked, never run.
3. After that it starts the game straight away.

Settings and saves: ~/.local/share/hardtruck2-native (Windows: %APPDATA%\hardtruck2-native).

## Building from source

BUILDING.md: put the game's files in place from your own GOG copy, then `make`. DESIGN.md is the
project's full record: how the translator and runtime work, every decision, and the checks.

## Helping

CONTRIBUTING.md has the rules (the main one: no game files, ever). Bug reports from playing are
the most useful thing right now.

## Licence

The Unlicense: public domain (LICENSE). Take anything you like. CREDITS.md lists the libraries
and their licences.
