CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 $(shell pkg-config --cflags sdl2 libcurl libarchive)
LIBS = $(shell pkg-config --libs sdl2 libcurl libarchive) -lm

SRC_DIR = src
OBJ_DIR = obj
TARGET = retro_setup_gui

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)
	@echo "Build successful! Run ./${TARGET} to launch the GUI."

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
