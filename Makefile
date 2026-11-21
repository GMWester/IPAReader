CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
override CFLAGS += -Iinclude -fPIC -fvisibility=hidden -DIPR_BUILD_DLL
LDLIBS  = -lm
PREFIX  ?= /usr/local

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
  SHLIB = libipareader.dylib
  SHFLAGS = -dynamiclib -install_name @rpath/$(SHLIB)
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  SHLIB = ipareader.dll
  SHFLAGS = -shared
else
  SHLIB = libipareader.so
  SHFLAGS = -shared -Wl,-soname,$(SHLIB)
endif

SRC = src/parse.c src/plan.c src/synth.c src/api.c src/tune.c
OBJ = $(SRC:src/%.c=build/%.o)

all: build/$(SHLIB) build/libipareader.a build/ipareader

build/%.o: src/%.c src/internal.h src/tune.h include/ipareader.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/$(SHLIB): $(OBJ)
	$(CC) $(SHFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/libipareader.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

build/ipareader: cli/main.c build/libipareader.a
	$(CC) $(CFLAGS) -o $@ cli/main.c build/libipareader.a $(LDLIBS)

build/test_api: tests/test_api.c build/libipareader.a
	$(CC) $(CFLAGS) -o $@ tests/test_api.c build/libipareader.a $(LDLIBS)

test: build/test_api build/ipareader build/$(SHLIB)
	./build/test_api
	python3 tests/test_python.py

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/share/ipareader/voices
	install -m 755 build/ipareader $(DESTDIR)$(PREFIX)/bin/
	install -m 644 build/$(SHLIB) build/libipareader.a $(DESTDIR)$(PREFIX)/lib/
	install -m 644 include/ipareader.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 voices/*.voice $(DESTDIR)$(PREFIX)/share/ipareader/voices/

clean:
	rm -rf build

.PHONY: all test install clean
