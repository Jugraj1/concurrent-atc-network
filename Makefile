# Set the C compiler to `gcc` (GNU Compiler Collection)
CC=gcc

# Settings to use for the compilation
CFLAGS=-Wall -Wconversion -g -ggdb3 -fsanitize=thread

PROGS = controller
OBJS = $(addsuffix .o, $(PROGS))

all: $(PROGS)

ifdef LOG
CFLAGS += -DENABLE_LOG -fsanitize=thread
endif

ifdef RELEASE
CFLAGS += -O3
endif

controller: src/controller.o src/network_utils.o src/airport.o 
	"$(CC)" $(CFLAGS) -o $@ $^

src/%.o : src/%.c
	"$(CC)" $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm src/*.o $(PROGS) >/dev/null 2>/dev/null || true
