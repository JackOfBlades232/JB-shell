/* Toy-Shell/src/tokeniz/line_tokenization.h */
#ifndef LINE_TOKENIZATION_SENTRY
#define LINE_TOKENIZATION_SENTRY

#include "word_list.h"

int tokenize_input_line_to_word_list(char *line, struct word_list **out_words);

#endif
