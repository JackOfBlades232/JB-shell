/* Toy-Shell/src/line_tokenization.h */
#ifndef LINE_TOKENIZATION_SENTRY
#define LINE_TOKENIZATION_SENTRY

#include "word_list.h"

#include <stdio.h>

int tokenize_input_line_to_word_list(FILE *f, 
        struct word_list *out_words, int *eol_char);

#endif
