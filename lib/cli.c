#include "cli.h"

#include <string.h>

static void out(const cli_t *c, const char *s)
{
    if (c->write != NULL) {
        c->write(s, c->ctx);
    }
}

void cli_init(cli_t *c, const cli_cmd_t *cmds, size_t ncmds, cli_write_fn write, void *ctx,
              const char *prompt, bool echo)
{
    memset(c, 0, sizeof(*c));
    c->cmds   = cmds;
    c->ncmds  = ncmds;
    c->write  = write;
    c->ctx    = ctx;
    c->prompt = prompt;
    c->echo   = echo;
}

int cli_tokenize(char *line, char **argv, int max)
{
    int   argc = 0;
    char *p    = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max) {
            return argc; /* extra tokens are ignored; handlers validate argc */
        }
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"') {
                p++;
            }
            if (*p != '"') {
                return -1;
            }
            *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p != '\0') {
                *p++ = '\0';
            }
        }
    }
    return argc;
}

static void print_help(const cli_t *c)
{
    out(c, "commands:\r\n");
    for (size_t i = 0; i < c->ncmds; i++) {
        out(c, "  ");
        out(c, c->cmds[i].name);
        if (c->cmds[i].usage != NULL && c->cmds[i].usage[0] != '\0') {
            out(c, " ");
            out(c, c->cmds[i].usage);
        }
        out(c, "\r\n      ");
        out(c, c->cmds[i].help != NULL ? c->cmds[i].help : "");
        out(c, "\r\n");
    }
}

int cli_execute(cli_t *c, char *line)
{
    char *argv[CLI_ARGV_MAX];
    int   argc = cli_tokenize(line, argv, (int)CLI_ARGV_MAX);

    if (argc < 0) {
        out(c, "error: unterminated quote\r\n");
        return CLI_ERR_ARGS;
    }
    if (argc == 0) {
        return CLI_EMPTY;
    }
    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        print_help(c);
        return CLI_OK;
    }
    for (size_t i = 0; i < c->ncmds; i++) {
        if (strcmp(argv[0], c->cmds[i].name) == 0) {
            const int rc = c->cmds[i].fn(argc, argv, c->ctx);
            if (rc == CLI_ERR_ARGS) {
                out(c, "usage: ");
                out(c, c->cmds[i].name);
                out(c, " ");
                out(c, c->cmds[i].usage != NULL ? c->cmds[i].usage : "");
                out(c, "\r\n");
            }
            return rc;
        }
    }
    out(c, "unknown command '");
    out(c, argv[0]);
    out(c, "' (try 'help')\r\n");
    return CLI_ERR_UNKNOWN;
}

static void prompt(const cli_t *c)
{
    if (c->prompt != NULL) {
        out(c, c->prompt);
    }
}

void cli_feed(cli_t *c, char ch)
{
    /* Treat CR, LF and CRLF as one line ending. */
    if (ch == '\n' && c->last_was_cr) {
        c->last_was_cr = false;
        return;
    }
    c->last_was_cr = (ch == '\r');

    if (ch == '\r' || ch == '\n') {
        if (c->echo) {
            out(c, "\r\n");
        }
        if (c->overflow) {
            out(c, "error: line too long\r\n");
        } else {
            c->line[c->len] = '\0';
            (void)cli_execute(c, c->line);
        }
        c->len      = 0u;
        c->overflow = false;
        prompt(c);
        return;
    }

    if (ch == '\b' || ch == 0x7F) {
        if (c->len > 0u && !c->overflow) {
            c->len--;
            if (c->echo) {
                out(c, "\b \b");
            }
        }
        return;
    }

    if ((unsigned char)ch < 0x20u || (unsigned char)ch > 0x7Eu) {
        return; /* ignore other control and non-ASCII bytes */
    }

    if (c->len >= CLI_LINE_MAX - 1u) {
        c->overflow = true;
        return;
    }
    c->line[c->len++] = ch;
    if (c->echo) {
        const char s[2] = {ch, '\0'};
        out(c, s);
    }
}
