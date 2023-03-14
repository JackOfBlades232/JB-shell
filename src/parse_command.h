/* Toy-Shell/src/parse_command.h */
#ifndef PARSE_COMMAND_SENTRY
#define PARSE_COMMAND_SENTRY

#include "word_list.h"
#include "command.h"

struct command_chain *parse_tokens_to_cmd_chain(
        struct word_list *tokens,
        struct command_res *res
        );

#endif
