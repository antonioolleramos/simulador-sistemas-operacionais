CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS := -Iinclude
TARGET := simulador
SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

debug: CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O0 -g3 \
	-fsanitize=address,undefined
debug: clean $(TARGET)

clean:
	$(RM) $(OBJECTS) $(TARGET)
