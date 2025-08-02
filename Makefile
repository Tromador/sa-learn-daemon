# Simple Makefile for sa-learn-daemon

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pipe
LDFLAGS :=

TARGET  := sa_learn_daemon
SRC     := src/sa_learn_daemon.c
INC     := -Iinclude

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
        $(CC) $(CFLAGS) $(INC) -o $@ $^ $(LDFLAGS)

clean:
        rm -f $(TARGET)
