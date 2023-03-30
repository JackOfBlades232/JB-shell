/* Toy-Shell/src/execution/parse_command.h */
#ifndef PARSE_COMMAND_SENTRY
#define PARSE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "command.h"
#include "cmd_res.h"

struct command_chain *parse_tokens_to_cmd_chain(
        struct word_list *tokens,
        struct command_res *res
        );

#endif
