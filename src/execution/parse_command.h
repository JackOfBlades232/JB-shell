/* Toy-Shell/src/execution/parse_command.h */
#ifndef PARSE_COMMAND_SENTRY
#define PARSE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "command.h"

enum pipe_sequence_rule { none, always, if_success, if_failed, to_bg };

struct command_pipe *parse_tokens_to_cmd_pipe(
        struct word_list *tokens,
        enum pipe_sequence_rule *rule_out
        );

#endif
