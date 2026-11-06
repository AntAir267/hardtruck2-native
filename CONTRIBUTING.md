# Contributing

Thanks for helping. Bug reports, testing on other hardware and pull requests are all welcome.

## Rules

1. **No game files, ever.** Nothing from the game in the repository, an issue or a pull request:
   not king.exe or any other file of the install, nothing extracted from them (textures, sounds,
   text, data tables), and no generated C (gen/ is the translation of your own copy and stays on
   your computer). Hand-written code is fine, including code that rewrites a game function by
   hand. Never link to copies of the game.
2. **The checks must pass.** A change to the translator or the runtime keeps the game's behaviour
   exact: the function tests (`make check`) show no new failures against
   tests/fncheck_baseline.txt, and the lockstep run against the original code (tools/lockstep.py)
   still matches. DESIGN.md says how to run them. Optimisations never change what the game does,
   including its timing.
3. **Settings live in the game's own menus.** Anything a change adds that a player might want to
   switch goes on the MORE OPTIONS page (runtime/menu.c), drawn like the game's own, never only
   in a file or an environment variable. The original look must stay reachable.
4. **Every controller works.** Any controller, plugged in at any time, alongside the keyboard;
   never only the first one found.
5. Enhancements are optional and can be turned off. They come from rendering, not from
   upscaled or replaced game art.

## Pull requests

Keep each one to a single change, say what it fixes and how you checked it. The maintainer reads
every change before building it.

## Reporting bugs

Use the bug report form. Include your system (Linux distribution or Windows version, graphics
card), what you were doing, and the log: last-run.log in the settings folder above (or setup.log
if setting up failed).
