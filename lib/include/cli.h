/**
 * @file    cli.h
 * @brief   Line-oriented command shell, independent of the transport.
 *
 * Characters are fed one at a time (from a UART RX stream). The shell
 * handles CR/LF/CRLF line endings, backspace/DEL editing, overlong-line
 * rejection, double-quoted arguments and a built-in `help`. Output goes
 * through a caller-supplied write function, so the same code runs on the
 * target and in host unit tests.
 */
#ifndef LIB_CLI_H
#define LIB_CLI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_LINE_MAX 96u
#define CLI_ARGV_MAX 8u

enum {
    CLI_OK          = 0,
    CLI_EMPTY       = 1,
    CLI_ERR_UNKNOWN = -1,
    CLI_ERR_ARGS    = -2,
    CLI_ERR_TOOLONG = -3,
    CLI_ERR_FAILED  = -4,
};

typedef void (*cli_write_fn)(const char *s, void *ctx);
typedef int (*cli_handler_fn)(int argc, char **argv, void *ctx);

typedef struct {
    const char    *name;
    const char    *usage; /* arguments, e.g. "<off|csv|bin> [period_ms]" */
    const char    *help;  /* one-line description */
    cli_handler_fn fn;
} cli_cmd_t;

typedef struct {
    const cli_cmd_t *cmds;
    size_t           ncmds;
    cli_write_fn     write;
    void            *ctx;
    const char      *prompt;
    bool             echo;
    bool             overflow;
    bool             last_was_cr;
    size_t           len;
    char             line[CLI_LINE_MAX];
} cli_t;

void cli_init(cli_t *c, const cli_cmd_t *cmds, size_t ncmds, cli_write_fn write, void *ctx,
              const char *prompt, bool echo);

/** Feed one received character; runs the command on end-of-line. */
void cli_feed(cli_t *c, char ch);

/** Execute one complete line (modified in place). Returns a CLI_* code or the handler's result. */
int cli_execute(cli_t *c, char *line);

/** Split @p line in place on spaces/tabs; supports "quoted args". Returns argc, or -1 on an unterminated quote. */
int cli_tokenize(char *line, char **argv, int max);

#ifdef __cplusplus
}
#endif

#endif /* LIB_CLI_H */
