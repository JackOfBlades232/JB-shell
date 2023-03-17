/* Toy-Shell/src/line_tokenization.c */
#include "line_tokenization.h"
#include "input.h"

#include <stdlib.h>

enum line_traverse_mode { regular, in_quotes };

struct line_traverse_state {
    int cur_c;
    enum line_traverse_mode mode;
    int in_word, ignore_spec;
};

static void init_state(struct line_traverse_state *state)
{
    state->mode = regular;
    state->in_word = 0;
    state->ignore_spec = 0;
}

static int char_is_eol(int c)
{
    return c == '\n' || c == '\r' || c == '\0';
}

static int cur_char_is_special(const struct line_traverse_state *state)
{
    return !state->ignore_spec && 
        (state->cur_c == '"' || state->cur_c == '\\');
}

static int cur_char_is_in_word(const struct line_traverse_state *state)
{
    return state->mode == in_quotes ||
        (state->cur_c != ' ' && state->cur_c != '\t');
}

static void switch_traverse_mode(struct line_traverse_state *state)
{
    state->mode = state->mode == regular ? in_quotes : regular;
}

static void process_spec_char(struct line_traverse_state *state,
        struct word_list *words)
{
    if (state->cur_c == '"') {
        switch_traverse_mode(state);

        if (!state->in_word && state->mode == in_quotes) {
            word_list_add_item(words, regular_wrd);
            state->in_word = 1;
        }
    } else if (state->cur_c == '\\')
        state->ignore_spec = 1;
}

static int char_is_first_in_split_pattern(int c)
{
    return c == '<' || c == '>' || c == '&' || c == '|' || 
        c == ';' || c == '(' || c == ')';
}

static int two_chars_are_split_pattern(int c1, int c2)
{
    return (c1 == '>' && c2 == '>') || 
        (c1 == '|' && c2 == '|') ||
        (c1 == '&' && c2 == '&');
}

static int try_extract_split_pattern(char **line, char *l_base,
        struct line_traverse_state *state, struct word_list *words)
{ 
    int second_c;

    if (state->mode == in_quotes || 
            !char_is_first_in_split_pattern(state->cur_c))
        return 0;

    word_list_add_item(words, separator);
    word_list_add_letter_to_last(words, state->cur_c);

    second_c = lgetc(line);
    if (two_chars_are_split_pattern(state->cur_c, second_c)) {
        state->cur_c = second_c;
        word_list_add_letter_to_last(words, state->cur_c);
    } else
        lungetc(line, l_base, second_c);

    state->in_word = 0;
    state->ignore_spec = 0;
    return 1;
}

int tokenize_input_line_to_word_list(char *line, struct word_list **out_words)
{
    int status = 0;
    char *linep = line;
    struct line_traverse_state state;

    *out_words = word_list_create();

    init_state(&state);

    while (!char_is_eol((state.cur_c = lgetc(&linep)))) {
        if (try_extract_split_pattern(&linep, line, &state, *out_words))
            continue;
        else if (cur_char_is_special(&state))
            process_spec_char(&state, *out_words);
        else {
            if (!state.in_word && cur_char_is_in_word(&state))
                 word_list_add_item(*out_words, regular_wrd);

            state.in_word = cur_char_is_in_word(&state);

            if (state.in_word)
                word_list_add_letter_to_last(*out_words, state.cur_c);

            state.ignore_spec = 0;
        }
    }

    if (state.mode != regular || state.ignore_spec)
        status = 1;

    if (status != 0)
        word_list_free(*out_words);

    free(line);

    return status;
}
