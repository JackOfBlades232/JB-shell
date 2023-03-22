SRCMODULES = src/word.c src/word_list.c src/line_tokenization.c src/int_set.c \
			 src/command.c src/parse_command.c src/execute_command.c \
			 src/input.c src/prompt.c src/lookup.c
OBJMODULES = $(SRCMODULES:.c=.o)
CC = gcc
CFLAGS = -g -Wall

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

prog: main.c $(OBJMODULES)
	$(CC) $(CFLAGS) $^ -o $@

test: tests.c $(OBJMODULES)
	$(CC) $(CFLAGS) $^ -o $@

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

deps.mk: $(SRCMODULES)
	$(CC) -MM $^ > $@

clean:
	rm -f src/*.o *.o prog test
