/* Toy-Shell/src/execution/parse_command.h */
#ifndef PARSE_COMMAND_SENTRY
#define PARSE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "command.h"

enum chain_sequence_rule { none, always, if_success, if_failed, to_bg };

struct command_chain *parse_tokens_to_cmd_chain(
        struct word_list *tokens,
        enum chain_sequence_rule *rule_out
        );

#endif
