COMPILER ?= $(GCC_PATH)gcc

FLAGS ?= -O2 -Wall -Wno-variadic-macros -pedantic -g -fopenmp $(GCC_SUPPFLAGS) -DDEBUG_MNGR -DDEBUG_LOADER

LDFLAGS ?= -g -fopenmp
LDLIBS = #-lm

EXECUTABLE = schedule_processes 

SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:src/%.c=obj/%.o)

all: release

release: $(OBJS)
	$(COMPILER) $(LDFLAGS) -o $(EXECUTABLE) $(OBJS) $(LDLIBS) 

obj/%.o: src/%.c | obj
	$(COMPILER) $(FLAGS) -o $@ -c $<
obj:
	mkdir -p $@

clean:
	rm -f obj/*.o
	rm -f *.log
	rm ${EXECUTABLE} 
