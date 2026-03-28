CC         = clang
CFLAGS     = -Wall -Wextra -O3 -std=c11

FRAMEWORKS = -framework AudioToolbox -framework CoreFoundation -framework Accelerate

# Homebrew ncurses 6.x (wide-char, supports 256 colors and UTF-8 block chars)
# On Apple Silicon: /opt/homebrew; on Intel: /usr/local
BREW_PREFIX    := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
NCURSES_PREFIX  = $(BREW_PREFIX)/opt/ncurses
NCURSES_CFLAGS  = -I$(NCURSES_PREFIX)/include
NCURSES_LIBS    = -L$(NCURSES_PREFIX)/lib -lncursesw

LIBS = $(NCURSES_LIBS) -lm

TARGET = hzview
SRC    = main.c audio.c viz.c config.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(NCURSES_CFLAGS) $(SRC) -o $(TARGET) $(FRAMEWORKS) $(LIBS)

install: $(TARGET)
	install -m755 $(TARGET) /usr/local/bin/

clean:
	rm -f $(TARGET)

.PHONY: all install clean
