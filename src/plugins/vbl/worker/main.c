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
                if (strcmp(head->symbol, "if") == 0) {
                    /* (if cond then [else]) */
                    if (ast->list.count >= 3) {
                        GraphNode *cg = graph_build(ast->list.items[1]);
                        int truthy = 0;
                        if (cg && graph_eval(cg) == 0 && cg->result) {
                            Value *cv = cg->result;
                            truthy = cv->is_int ? (cv->idata && cv->idata[0] != 0)
                                                : (cv->data  && cv->data[0]  != 0.0f);
                        }
                        graph_free(cg);
                        AstNode *branch = truthy ? ast->list.items[2]
                                     : (ast->list.count >= 4 ? ast->list.items[3] : NULL);
                        if (branch) {
                            if (branch->type == AST_LIST && branch->list.count > 0 &&
                                branch->list.items[0]->type == AST_SYMBOL &&
                                strcmp(branch->list.items[0]->symbol, "print") == 0 &&
                                branch->list.count >= 2) {
                                GraphNode *pg = graph_build(branch->list.items[1]);
                                if (pg && graph_eval(pg) == 0) val_print(pg->result);
                                graph_free(pg);
                            } else {
                                GraphNode *bg = graph_build(branch);
                                if (bg && graph_eval(bg) == 0) val_print(bg->result);
                                graph_free(bg);
                            }
                        }
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "exit") == 0) {
                    done = 1;
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "def") == 0) {
                    if (ast->list.count >= 3 && ast->list.items[1]->type == AST_SYMBOL) {
                        const char *name = ast->list.items[1]->symbol;
                        /* reject reserved words */
                        static const char *reserved[] = {
                            "def", "print", "show", "save", "vars", "exit",
                            "load-script", "save-script", "if", NULL
                        };
                        int blocked = 0;
                        for (int ri = 0; reserved[ri]; ri++)
                            if (strcmp(name, reserved[ri]) == 0) { blocked = 1; break; }
                        if (!blocked && op_find(name)) blocked = 1;
                        if (blocked) {
                            fprintf(stderr, "ERROR: '%s' is reserved\n", name);
                            ast_free(ast);
                            continue;
                        }
                        GraphNode *gn = graph_build(ast->list.items[2]);
                        if (!graph_eval(gn)) {
                            env_bind(name, gn->result);
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
                                            const char *nm = sa->list.items[1]->symbol;
                                            static const char *resv[] = {"def","print","show","save","vars","exit","load-script","save-script","if",NULL};
                                            int blk = 0;
                                            for (int r = 0; resv[r]; r++) if (strcmp(nm,resv[r])==0) {blk=1;break;}
                                            if (!blk && op_find(nm)) blk = 1;
                                            if (blk) { fprintf(stderr,"ERROR: '%s' is reserved\n",nm); }
                                            else {
                                                GraphNode *gn = graph_build(sa->list.items[2]);
                                                if (!graph_eval(gn)) {
                                                    env_bind(nm, gn->result);
                                                    gn->result = NULL; gn->literal = NULL;
                                                }
                                                graph_free(gn);
                                            }
                                        }
                                    } else if (sh->type == AST_SYMBOL && strcmp(sh->symbol, "if") == 0) {
                                        if (sa->list.count >= 3) {
                                            GraphNode *cg = graph_build(sa->list.items[1]);
                                            int truthy = 0;
                                            if (cg && graph_eval(cg) == 0 && cg->result) {
                                                Value *cv = cg->result;
                                                truthy = cv->is_int ? (cv->idata && cv->idata[0] != 0)
                                                                    : (cv->data  && cv->data[0]  != 0.0f);
                                            }
                                            graph_free(cg);
                                            AstNode *branch = truthy ? sa->list.items[2]
                                                         : (sa->list.count >= 4 ? sa->list.items[3] : NULL);
                                            if (branch) {
                                                /* if branch is (print ...), handle directly */
                                                if (branch->type == AST_LIST && branch->list.count > 0 &&
                                                    branch->list.items[0]->type == AST_SYMBOL &&
                                                    strcmp(branch->list.items[0]->symbol, "print") == 0 &&
                                                    branch->list.count >= 2) {
                                                    GraphNode *pg = graph_build(branch->list.items[1]);
                                                    if (pg && graph_eval(pg) == 0) val_print(pg->result);
                                                    graph_free(pg);
                                                } else {
                                                    GraphNode *bg = graph_build(branch);
                                                    if (bg && graph_eval(bg) == 0) val_print(bg->result);
                                                    graph_free(bg);
                                                }
                                            }
                                        }
                                    } else if (sh->type == AST_SYMBOL && strcmp(sh->symbol, "vars") == 0) {
                                        int n = env_count();
                                        if (n == 0) { printf("  (no variables defined)\n"); }
                                        else for (int vi = 0; vi < n; vi++) {
                                            const char *vname; Value *vv;
                                            env_get(vi, &vname, &vv);
                                            const char *tn = "?";
                                            if (vv) switch (vv->type) {
                                                case TYPE_VOLUME3D: tn="vol3d"; break;
                                                case TYPE_VOLUME4D: tn="vol4d"; break;
                                                case TYPE_TIMESERIES: tn="ts"; break;
                                                case TYPE_CORRMAP: tn="corrmap"; break;
                                                case TYPE_MASK: tn="mask"; break;
                                                case TYPE_NIL: tn="nil"; break;
                                                case TYPE_AFFINE: tn="affine"; break;
                                            }
                                            if (vv && vv->type==TYPE_VOLUME3D && vv->nx==1 && vv->ny==1 && vv->nz==1)
                                                printf("  %-20s %s = %.6g\n", vname, tn, vv->data[0]);
                                            else if (vv)
                                                printf("  %-20s %s %dx%dx%d\n", vname, tn, vv->nx, vv->ny, vv->nz);
                                            else printf("  %-20s (null)\n", vname);
                                        }
                                    } else if (sh->type == AST_SYMBOL && strcmp(sh->symbol, "print") == 0) {
                                        if (sa->list.count >= 2) {
                                            GraphNode *pg = graph_build(sa->list.items[1]);
                                            if (pg && graph_eval(pg) == 0) val_print(pg->result);
                                            graph_free(pg);
                                        }
                                    } else if (sh->type == AST_SYMBOL && strcmp(sh->symbol, "save") == 0) {
                                        if (sa->list.count >= 3 && sa->list.items[2]->type == AST_STRING) {
                                            GraphNode *sg = graph_build(sa->list.items[1]);
                                            if (sg && graph_eval(sg) == 0) {
                                                val_save_nifti(sg->result, sa->list.items[2]->string);
                                                printf("OK -> %s\n", sa->list.items[2]->string);
                                            }
                                            graph_free(sg);
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
                if (strcmp(head->symbol, "vars") == 0) {
                    int n = env_count();
                    if (n == 0) {
                        printf("  (no variables defined)\n");
                    } else {
                        for (int i = 0; i < n; i++) {
                            const char *name; Value *val;
                            env_get(i, &name, &val);
                            const char *tname = "?";
                            if (!val) tname = "(null)";
                            else switch (val->type) {
                                case TYPE_VOLUME3D:  tname = "vol3d";   break;
                                case TYPE_VOLUME4D:  tname = "vol4d";   break;
                                case TYPE_TIMESERIES:tname = "ts";      break;
                                case TYPE_CORRMAP:   tname = "corrmap"; break;
                                case TYPE_MASK:      tname = "mask";    break;
                                case TYPE_NIL:       tname = "nil";     break;
                                case TYPE_AFFINE:    tname = "affine";  break;
                            }
                            if (val && val->type == TYPE_VOLUME3D && val->nx == 1 && val->ny == 1 && val->nz == 1)
                                printf("  %-20s %s = %.6g\n", name, tname, val->data[0]);
                            else if (val)
                                printf("  %-20s %s %dx%dx%d\n", name, tname, val->nx, val->ny, val->nz);
                            else
                                printf("  %-20s (null)\n", name);
                        }
                    }
                    ast_free(ast);
                    continue;
                }
                if (strcmp(head->symbol, "if") == 0) {
                    /* (if cond then [else]) */
                    if (ast->list.count >= 3) {
                        GraphNode *cg = graph_build(ast->list.items[1]);
                        int truthy = 0;
                        if (cg && graph_eval(cg) == 0 && cg->result) {
                            Value *cv = cg->result;
                            truthy = cv->is_int ? (cv->idata && cv->idata[0] != 0)
                                                : (cv->data  && cv->data[0]  != 0.0f);
                        }
                        graph_free(cg);
                        AstNode *branch = truthy ? ast->list.items[2]
                                     : (ast->list.count >= 4 ? ast->list.items[3] : NULL);
                        if (branch) {
                            if (branch->type == AST_LIST && branch->list.count > 0 &&
                                branch->list.items[0]->type == AST_SYMBOL &&
                                strcmp(branch->list.items[0]->symbol, "print") == 0 &&
                                branch->list.count >= 2) {
                                GraphNode *pg = graph_build(branch->list.items[1]);
                                if (pg && graph_eval(pg) == 0) val_print(pg->result);
                                graph_free(pg);
                            } else {
                                GraphNode *bg = graph_build(branch);
                                if (bg && graph_eval(bg) == 0) val_print(bg->result);
                                graph_free(bg);
                            }
                        }
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
