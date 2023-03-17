/* Toy-Shell/src/input.h */
#ifndef INPUT_SENTRY
#define INPUT_SENTRY

char *read_input_line();
char lgetc(char **line);
void lungetc(char **line, char *l_base, char c);

#endif
