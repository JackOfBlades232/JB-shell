/* Toy-Shell/src/input.h */
#ifndef INPUT_SENTRY
#define INPUT_SENTRY

#include <stddef.h>

struct positional_buffer {
    char *buf, *bufpos, *bufend;
    size_t bufsize;
};

char *read_input_line();
char lgetc(char **line);
void lungetc(char **line, char *l_base, char c);

int char_is_separator(int c);
void redraw_full_buf(struct positional_buffer *pbuf);
void add_word(struct positional_buffer *pbuf, const char *word);

#endif
