/* Toy-Shell/src/interpreter/parse_command.h */
#ifndef PARSE_COMMAND_SENTRY
#define PARSE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "../cmd_data/pipe_seq.h"

struct pipe_sequence *parse_tokens_to_pipe_seq(struct word_list *tokens);

#endif
