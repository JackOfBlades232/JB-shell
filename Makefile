SRCMODULES = $(shell find ./ -type f -name '*.c')
OBJMODULES = $(SRCMODULES:.c=.o)
CC = gcc
CFLAGS = -g -Wall

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

prog: $(OBJMODULES)
	$(CC) $(CFLAGS) $^ -o shell

ifneq (clean, core_clean, $(MAKECMDGOALS))
-include deps.mk
endif

deps.mk: $(SRCMODULES)
	$(CC) -MM $^ > $@

clean:
	rm -f $(OBJMODULES) *.o shell

core_clean:
	rm -f core.*
