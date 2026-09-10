# BAD30: CE-native Bad Apple player

NAME = BAD30
ICON = icon.png
DESCRIPTION = "Bad Apple 30FPS player"
COMPRESSED = YES
COMPRESSED_MODE = zx7

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

ifeq ($(DEBUG),1)
CFLAGS += -DBAD30_DEBUG
CXXFLAGS += -DBAD30_DEBUG
endif

include $(shell cedev-config --makefile)
