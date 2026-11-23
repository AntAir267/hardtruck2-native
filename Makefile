# Hard Truck 2 native: build the runtime and the C generated from the user's own king.exe.
#   make GAME=<folder with king.exe>     translate + build build/ht2
# The translator and most of the runtime come from the AntiStatic toolkit, pinned as a git
# submodule (third_party/antistatic, a release tag); runtime/ holds what is Hard Truck 2's own:
# port.h, its game hooks, menus, widescreen, movies and music stand-ins, native functions.
# Python with Capstone (and Unicorn, for the function check): the repository's .venv if there is one
PY ?= $(firstword $(wildcard .venv/bin/python) python3)
GAME ?= $(HOME)/.local/share/hardtruck2-native/game
BUILD_CFG ?= builds/ht2_gog13.py
TK ?= third_party/antistatic
# Parallel compiles: each takes ~300 MB; keep it modest so the machine stays usable.
JOBS ?= 8
MAKEFLAGS += -j$(JOBS)
CC = gcc
SDL3 ?= third_party/sdl3
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libswscale libswresample libavutil 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavformat libavcodec libswscale libswresample libavutil 2>/dev/null)
RT_INC = -Iruntime -I$(TK)/runtime/cpu -I$(TK)/runtime/win32 -I$(TK)/runtime/d3d7
CFLAGS_COMMON = -std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer $(RT_INC) -Igen -I$(SDL3)/include
CFLAGS_GEN = $(CFLAGS_COMMON) -O2 -w -DCPU_FAST_MEM
CFLAGS_RT = $(CFLAGS_COMMON) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-missing-field-initializers $(FFMPEG_CFLAGS)
LDFLAGS = -rdynamic $(SDL3)/lib/libSDL3.a $(FFMPEG_LIBS) -pthread -lm -ldl -lz -lvulkan

# the runtime: the toolkit's CPU, Windows and DirectX 7 folders, then the port's own files
TK_SRC = $(TK)/runtime/cpu/fpu.c $(wildcard $(TK)/runtime/win32/*.c) $(wildcard $(TK)/runtime/d3d7/*.c)
PORT_SRC = $(wildcard runtime/*.c)
RT_OBJ = $(patsubst %.c,build/obj/tk_%.o,$(notdir $(TK_SRC))) $(patsubst runtime/%.c,build/obj/rt_%.o,$(PORT_SRC))
RT_H = $(wildcard runtime/*.h $(TK)/runtime/*/*.h)
vpath %.c $(TK)/runtime/cpu $(TK)/runtime/win32 $(TK)/runtime/d3d7
# The code files recomp.py wrote, listed in gen/files.mk once it has run (a fresh tree has none
# yet: make writes the list after translating, then reads it again).
-include gen/files.mk
GEN_SRC = $(GEN_CODE) gen/table.c gen/stubs.c
GEN_OBJ = $(patsubst gen/%.c,build/obj/gen_%.o,$(GEN_SRC))

all: build/ht2

gen/funcs.h: $(wildcard $(TK)/translator/*.py) $(BUILD_CFG)
	$(PY) $(TK)/translator/recomp.py $(GAME)/king.exe $(BUILD_CFG) gen
gen/table.c gen/stubs.c gen/image.h: gen/funcs.h ;
gen/files.mk: gen/funcs.h
	@echo 'GEN_CODE = $(sort $(wildcard gen/code_*.c))' > $@

build/obj/gen_%.o: gen/%.c gen/funcs.h $(wildcard $(TK)/runtime/cpu/*.h) runtime/port.h | build/obj   # (cpu.h reads port.h)
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/obj/tk_%.o: %.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

build/obj/rt_%.o: runtime/%.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

build/ht2: $(GEN_OBJ) $(RT_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/obj:
	mkdir -p $@

# make install: the game in ~/.local/opt/hardtruck2-native with an applications-menu entry and the
# game's own icon (taken from the user's king.exe)
PREFIX ?= $(HOME)/.local/opt/hardtruck2-native
APPS ?= $(HOME)/.local/share/applications
install: all
	mkdir -p $(PREFIX) $(APPS)
	cp build/ht2 $(PREFIX)/ht2.new && mv -f $(PREFIX)/ht2.new $(PREFIX)/ht2
	-PYTHONPATH=$(TK)/translator $(PY) tools/icon.py $(GAME)/king.exe $(PREFIX)/ht2.png
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Hard Truck 2: King of the Road' \
		'Comment=Hard Truck 2 recompiled for Linux, on Vulkan, with controller support' \
		'Exec=$(PREFIX)/ht2' 'Icon=$(PREFIX)/ht2.png' 'Categories=Game;' \
		'StartupWMClass=hardtruck2-native' 'Terminal=false' > $(APPS)/hardtruck2-native.desktop
	@echo "installed in $(PREFIX); the applications menu has Hard Truck 2"


# Function-level check against the original code in Unicorn (tests/fncheck.c)
UNICORN ?= $(firstword $(wildcard .venv/lib/python3*/site-packages/unicorn))
build/obj/tk_main_nomain.o: $(TK)/runtime/win32/main.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -DRUNTIME_NO_MAIN -c $< -o $@
build/obj/gen_x87_special.o: gen/x87_special.c | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/fncheck: tests/fncheck.c $(filter-out build/obj/tk_main.o,$(RT_OBJ)) build/obj/tk_main_nomain.o $(GEN_OBJ) build/obj/gen_x87_special.o
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

# Reference build: the original code in Unicorn on the same runtime (ref/ref_cpu.c)
build/obj/ref_cpu.o: ref/ref_cpu.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -c $< -o $@

build/ht2_ref: $(RT_OBJ) $(GEN_OBJ) build/obj/gen_x87_special.o build/obj/ref_cpu.o
	$(CC) -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

check: build/fncheck
	HT2_NATIVE=0 build/fncheck --game $(GAME)   # (the hand-written functions off, as the baseline)

clean:
	rm -rf build gen

.PHONY: install all clean check
