# RICKROLL: CE-native Rickroll player

NAME = RICKROLL
ICON = icon.png
DESCRIPTION = "Rickroll 30FPS player"
COMPRESSED = YES
COMPRESSED_MODE = zx7

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

ifeq ($(DEBUG),1)
CFLAGS += -DRICKROLL_DEBUG
CXXFLAGS += -DRICKROLL_DEBUG
endif

include $(shell cedev-config --makefile)
