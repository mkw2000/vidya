CC := cc
PKG_CONFIG := /opt/homebrew/bin/pkg-config

RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)

CFLAGS := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Ilibs/raygui -Isrc $(RAYLIB_CFLAGS)
LDFLAGS := $(RAYLIB_LIBS) -lpthread

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    CFLAGS += -mmacosx-version-min=12.0
    LDFLAGS += -mmacosx-version-min=12.0 -framework Cocoa -framework IOKit -framework CoreVideo
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos
    YTDLP_NAME := yt-dlp
else ifeq ($(UNAME_S),Linux)
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux
    YTDLP_NAME := yt-dlp
else
    YTDLP_URL := https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe
    YTDLP_NAME := yt-dlp.exe
endif

SRC := src/main.c src/downloader.c
OBJ := $(SRC:.c=.o)
TARGET := ytdlp-gui

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

bundle-ytdlp: $(TARGET)
	curl -fsSL -o $(YTDLP_NAME) $(YTDLP_URL)
	chmod +x $(YTDLP_NAME) || true

macos-app:
	./scripts/build-macos-app.sh

%.o: %.c src/downloader.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET) $(YTDLP_NAME)
	rm -rf dist

.PHONY: all clean bundle-ytdlp macos-app

test:
	sh tests/run.sh

test-media:
	uv run --no-project python tests/media.py

.PHONY: test test-media
