/*
 *  vbl-worker — VBL compute engine (standalone REPL)
 *
 *  Usage:  ./vbl-worker [script.vbl]
 */

#define GFXCORE_IMPLEMENTATION
#define GFXFONT_IMPLEMENTATION
#define GFXGRID_IMPLEMENTATION
#define GFXUI_IMPLEMENTATION
#define NIFTI_BASE_IMPLEMENTATION
#define NIFTI_ZNZ_IMPLEMENTATION
#define NIFTI_HEADER_IMPLEMENTATION
#define NIFTI_IMAGE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "parser.h"
#include "types.h"
#include "ops.h"
#include "graph.h"
#include "env.h"
#include "pool.h"

#define MAX_LINE 4096
#define HIST_MAX 100

static char history[HIST_MAX][MAX_LINE];
static int hist_count = 0;
static int hist_pos = 0;

static void hist_add(const char *line) {
    if (hist_count < HIST_MAX) {
        strncpy(history[hist_count], line, MAX_LINE - 1);
        hist_count++;
    }
    hist_pos = hist_count;
}

static void set_raw(struct termios *orig) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, orig);
    raw = *orig;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void restore_term(struct termios *orig) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
}

/* Read one line with arrow key editing. Returns 0 on EOF. */
static int read_line(char *buf, int max, int tty, FILE *fp) {
    if (!tty) return fgets(buf, max, fp) != NULL;

    struct termios orig;
    set_raw(&orig);

    write(STDOUT_FILENO, "> ", 2);

    int pos = 0, len = 0;
    memset(buf, 0, max);

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) { restore_term(&orig); return 0; }

        if (c == '\x1b') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) break;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) break;
            if (seq[0] == '[') {
                if (seq[1] == 'D' && pos > 0) {       /* Left */
                    write(STDOUT_FILENO, "\b", 1);
                    pos--;
                } else if (seq[1] == 'C' && pos < len) { /* Right */
                    write(STDOUT_FILENO, &buf[pos], 1);
                    pos++;
                } else if (seq[1] == 'A') {             /* Up */
                    if (hist_pos > 0) {
                        hist_pos--;
                        /* Clear line */
                        while (pos > 0) { write(STDOUT_FILENO, "\b \b", 3); pos--; len--; }
                        write(STDOUT_FILENO, "\r> ", 3);
                        strncpy(buf, history[hist_pos], max - 1);
                        len = strlen(buf);
                        pos = len;
                        write(STDOUT_FILENO, buf, len);
                    }
                } else if (seq[1] == 'B') {             /* Down */
                    if (hist_pos < hist_count) {
                        hist_pos++;
                        while (pos > 0) { write(STDOUT_FILENO, "\b \b", 3); pos--; len--; }
                        write(STDOUT_FILENO, "\r> ", 3);
                        if (hist_pos < hist_count) {
                            strncpy(buf, history[hist_pos], max - 1);
                        } else {
                            buf[0] = '\0';
                        }
                        len = strlen(buf);
                        pos = len;
                        write(STDOUT_FILENO, buf, len);
                    }
                }
            }
        } else if (c == 127 || c == '\b') {  /* Backspace */
            if (pos > 0) {
                memmove(&buf[pos - 1], &buf[pos], len - pos);
                len--; pos--;
                buf[len] = '\0';
                write(STDOUT_FILENO, "\b", 1);
                for (int i = pos; i < len; i++) write(STDOUT_FILENO, &buf[i], 1);
                write(STDOUT_FILENO, " ", 1);
                for (int i = len; i >= pos; i--) write(STDOUT_FILENO, "\b", 1);
            }
        } else if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        } else if (c >= 32 && c < 127 && len < max - 1) {
            memmove(&buf[pos + 1], &buf[pos], len - pos);
            buf[pos] = c;
            len++; pos++;
            buf[len] = '\0';
            write(STDOUT_FILENO, &buf[pos - 1], len - pos + 1);
            for (int i = 0; i < len - pos; i++) write(STDOUT_FILENO, "\b", 1);
        }
    }

    restore_term(&orig);
    if (len > 0) hist_add(buf);
    return 1;
}

int main(int argc, char **argv) {
    FILE *input = stdin;
    int interactive = 1;
    if (argc >= 2) {
        input = fopen(argv[1], "r");
        if (!input) { fprintf(stderr, "ERROR: cannot open '%s'\n", argv[1]); return 1; }
        interactive = 0;
    }

    env_init();
    ThreadPool *pool = pool_create(0); /* auto-detect cores */
    g_pool = pool;

    int tty = isatty(STDIN_FILENO) && (argc < 2);
    if (tty) printf("VBL Worker — type 'exit' to quit\n");

    char line[MAX_LINE];
    char acc[MAX_LINE * 8];  /* accumulated multi-line buffer */
    int acc_len = 0;
    int done = 0;

    while (!done && read_line(line, sizeof(line), tty, input)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;

        /* Skip empty and comments */
        if (*s == '\0' || *s == '\n' || *s == ';') continue;

        /* Accumulate */
        int len = strlen(s);
        if (acc_len + len + 2 >= (int)sizeof(acc)) {
            fprintf(stderr, "ERROR: expression too long\n");
            acc_len = 0;
            continue;
        }
        if (acc_len > 0) acc[acc_len++] = ' ';
        memcpy(acc + acc_len, s, len);
        acc_len += len;
        acc[acc_len] = '\0';

        /* Check paren balance */
        int depth = 0;
        for (int i = 0; i < acc_len; i++) {
            if (acc[i] == '(') depth++;
            else if (acc[i] == ')') depth--;
        }

        if (depth > 0) continue;   /* incomplete — read more */
        if (depth < 0) {
            fprintf(stderr, "ERROR: unbalanced parentheses\n");
            acc_len = 0;
            continue;
        }

        /* Complete expression — parse and evaluate */
        s = acc;
        acc_len = 0;

        AstNode *ast = parse_expr(s);
        if (!ast) { fprintf(stderr, "ERROR: parse failed\n"); continue; }

        /* Handle bare exit */
        if (ast->type == AST_SYMBOL && strcmp(ast->symbol, "exit") == 0) {
            ast_free(ast);
            done = 1;
            continue;
        }

        if (ast->type == AST_LIST && ast->list.count > 0) {
            AstNode *head = ast->list.items[0];
            if (head->type == AST_SYMBOL) {
                if (strcmp(head->symbol, "exit") == 0) {
                    done = 1;
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "def") == 0) {
                    if (ast->list.count >= 3 && ast->list.items[1]->type == AST_SYMBOL) {
                        GraphNode *gn = graph_build(ast->list.items[2]);
                        if (!graph_eval(gn)) {
                            env_bind(ast->list.items[1]->symbol, gn->result);
                            gn->result = NULL;
                            gn->literal = NULL;
                        }
                        graph_free(gn);
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "print") == 0) {
                    if (ast->list.count >= 2) {
                        GraphNode *gn = graph_build(ast->list.items[1]);
                        if (!graph_eval(gn)) val_print(gn->result);
                        gn->result = NULL;
                        graph_free(gn);
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "show") == 0) {
                    if (ast->list.count >= 2) {
                        GraphNode *gn = graph_build(ast->list.items[1]);
                        if (!graph_eval(gn)) {
                            val_print(gn->result);
                            printf("  [show: slot push not yet connected (Phase 3)]\n");
                        }
                        gn->result = NULL;
                        graph_free(gn);
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "save") == 0) {
                    if (ast->list.count >= 3 && ast->list.items[2]->type == AST_STRING) {
                        GraphNode *gn = graph_build(ast->list.items[1]);
                        if (!graph_eval(gn)) {
                            val_save_nifti(gn->result, ast->list.items[2]->string);
                            printf("OK -> %s\n", ast->list.items[2]->string);
                        }
                        gn->result = NULL;
                        graph_free(gn);
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "load-script") == 0) {
                    if (ast->list.count >= 2 && ast->list.items[1]->type == AST_STRING) {
                        FILE *sf = fopen(ast->list.items[1]->string, "r");
                        if (!sf) {
                            fprintf(stderr, "ERROR: cannot open script '%s'\n", ast->list.items[1]->string);
                        } else {
                            char sline[MAX_LINE];
                            while (fgets(sline, sizeof(sline), sf)) {
                                char *ss = sline;
                                while (*ss == ' ' || *ss == '\t') ss++;
                                if (*ss == '\0' || *ss == '\n' || *ss == ';') continue;
                                AstNode *sa = parse_expr(ss);
                                if (!sa) { fprintf(stderr, "ERROR: parse failed in script\n"); continue; }
                                if (sa->type == AST_LIST && sa->list.count > 0) {
                                    AstNode *sh = sa->list.items[0];
                                    if (sh->type == AST_SYMBOL && strcmp(sh->symbol, "def") == 0) {
                                        if (sa->list.count >= 3 && sa->list.items[1]->type == AST_SYMBOL) {
                                            GraphNode *gn = graph_build(sa->list.items[2]);
                                            if (!graph_eval(gn)) {
                                                env_bind(sa->list.items[1]->symbol, gn->result);
                                                gn->result = NULL; gn->literal = NULL;
                                            }
                                            graph_free(gn);
                                        }
                                    } else {
                                        GraphNode *gn = graph_build(sa);
                                        if (gn && !graph_eval(gn)) val_print(gn->result);
                                        graph_free(gn);
                                    }
                                }
                                ast_free(sa);
                            }
                            fclose(sf);
                            printf("  [loaded script: %s]\n", ast->list.items[1]->string);
                        }
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "save-script") == 0) {
                    if (ast->list.count >= 2 && ast->list.items[1]->type == AST_STRING) {
                        printf("  [save-script: not yet implemented]\n");
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "exit") == 0) {
                    done = 1;
                    ast_free(ast);
                    continue;
                }
            }
        }

        /* Generic expression */
        GraphNode *gn = graph_build(ast);
        if (gn) {
            if (!graph_eval(gn)) val_print(gn->result);
            graph_free(gn);
        }
        ast_free(ast);
    }

    pool_destroy(pool);
    env_shutdown();
    if (input != stdin) fclose(input);
    return 0;
}
