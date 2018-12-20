CC=gcc
CFLAGS=-g -Wall
# LD flags are the library name.
LDFLAGS=
SOURCES=$(wildcard *.c)
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=main

.PHONY: all clean

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
    $(CC) $(LDFLAGS) $(OBJECTS) -o $@

%.o: %.cpp
    $(CC) $(CFLAGS) $< -o $@

clean:
    rm -f $(EXECUTABLE) $(OBJECTS)
    echo "clean done"