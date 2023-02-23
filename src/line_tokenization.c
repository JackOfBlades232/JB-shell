/* Toy-Shell/src/line_tokenization.c */
#include "line_tokenization.h"

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
    return c == '\n' || c == '\r' || c == EOF;
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

static void process_spec_char(struct line_traverse_state *state)
{
    if (state->cur_c == '"') 
        switch_traverse_mode(state);
    else if (state->cur_c == '\\')
        state->ignore_spec = 1;
}


int tokenize_input_line_to_word_list(FILE *f, 
        struct word_list **out_words, int *eol_char)
{
    int status = 0;
    struct line_traverse_state state;

    *out_words = word_list_create();

    init_state(&state);

    while (!char_is_eol((state.cur_c = getc(f)))) {
        if (cur_char_is_special(&state))
            process_spec_char(&state);
        else {
            if (!state.in_word && cur_char_is_in_word(&state))
                 word_list_add_item(*out_words);

            state.in_word = cur_char_is_in_word(&state);

            if (state.in_word)
                word_list_add_letter_to_last(*out_words, state.cur_c);

            state.ignore_spec = 0;
        }
    }

    if (state.mode != regular)
        status = 1;

    if (status == 0)
        *eol_char = state.cur_c;
    else
        word_list_free(*out_words);

    return status;
}
