SRCMODULES = src/word.c src/word_list.c src/line_tokenization.c
OBJMODULES = $(SRCMODULES:.c=.o)
CC = gcc
CFLAGS = -g -Wall

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

prog: main.c $(OBJMODULES)
	$(CC) $(CFLAGS) $^ -o $@

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

deps.mk: $(SRCMODULES)
	$(CC) -MM $^ > $@

clean:
	rm -f *.o prog
