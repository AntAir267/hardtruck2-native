"""Per-build knowledge for Hard Truck 2: King of the Road 1.3 (GOG, "1.3 AMD RDNA fix" installer
52932; king.exe 2,908,160 bytes, linked 2002-03-19).

Addresses only: no code or data from the game lives in this repository.
"""
NAME = 'ht2-gog-1.3'
SHA256 = '4412a5f695dd016c9f92185b7d2d0be8ad3be787bfe2d77908f5b4d171eedd86'

# Functions reached only in ways discovery cannot see (found at run time, added here).
EXTRA_ENTRIES = []

# Addresses that look like code pointers but are not function entries.
NOT_ENTRIES = []

# Jump tables discovery cannot bound: jmp address -> (table address, entries)
JUMP_TABLES = {
    # bounds discovery cannot see [verified: disassembly, 2026-10-26]
    0x4e3d2a: (0x4e3e6b, 12),   # cmp [ebp-0x20], 0xb
    0x4eed8b: (0x4eee84, 7),    # cmp edx, 6 (flags kept across x87 code)
    0x5243da: (0x524a18, 4),    # cmp eax, edx with edx = 3
    0x552d0d: (0x553d6c, 4),    # cmp edx, 3 (the index reloaded from [esp+0x28])
    0x5954cc: (0x59557c, 6),    # cmp eax, edi with edi = 5
    # CRT memcpy (0x61c980) and its second copy (0x61f900): byte tails with negative indexes,
    # read from their true starts (sub ecx, 4 / jb: -4..-1; neg ecx: -7..0), as in td6-native
    0x61c9d4: (0x61cac8, 4),
    0x61cb42: (0x61cbf4, 8),
    0x61f954: (0x61fa48, 4),
    0x61fac2: (0x61fb74, 8),
}

# Jumps into the middle of another function (exception handling, longjmp).
NONLOCAL_JUMPS = []

# Imports that never return.
NORETURN_IMPORTS = ['ExitProcess', 'ExitThread', 'TerminateProcess']

TABLES = []
# Functions the runtime may hook (see 1nsane-native / td6-native): hook_<name>, run at entry.
HOOKS = {
    # an image of a cluster being made ready (ECX = the image record): game.c loads the pixels of
    # on-demand 16/32-bit images that are drawn as surfaces, which the game would copy from NULL
    0x5b9ec0: 'image_ready',
    # the player's profile opened at start-up (0x5275c0: a new one sets [EFFECTS] control = 0,
    # keyboard only): padgame.c makes a new profile's device the controller when one is connected
    0x5275c0: 'profile_open',
    # the GRAPHICS screen (menu.c: the port's options as its second page): constructor, OnCommand
    # (this = screen+0x40) and draw (this = screen+0x40)
    0x515460: 'graphics_ctor',
    0x515ac0: 'graphics_command',
    0x5162e0: 'graphics_draw',
}
CONSTS = {}
INLINE = {}
# Game data the runtime reads: emitted as GAME_<NAME> in gen/image.h.
DATA = {}
