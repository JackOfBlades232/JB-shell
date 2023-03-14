/* Toy-Shell/src/cmd_res.h */
#ifndef CMD_RES_SENTRY
#define CMD_RES_SENTRY

enum command_res_type { exited, killed, failed, noproc, not_implemented };
struct command_res {
  enum command_res_type type;
  int code;
};

#endif
