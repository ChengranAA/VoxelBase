#include "graph.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static GraphNode *gn_alloc(GraphNodeType t) { GraphNode *g = calloc(1, sizeof(GraphNode)); g->type = t; return g; }

static void gn_add_arg(GraphNode *p, GraphNode *c) {
    p->args = realloc(p->args, (p->arg_count + 1) * sizeof(GraphNode *));
    p->args[p->arg_count++] = c;
}

/* Map VBL names to internal op names */
static const char *op_alias(const char *name) {
    if (strcmp(name, "+") == 0) return "add";
    if (strcmp(name, "-") == 0) return "sub";
    if (strcmp(name, "*") == 0) return "mul";
    if (strcmp(name, "/") == 0) return "div";
    return name;
}

/* Bridge hook: called for unknown ops. Returns graph node or NULL. */
static GraphNode *(*g_bridge_hook)(const char *op_name, AstNode *ast) = NULL;

void graph_set_bridge_hook(GraphNode *(*hook)(const char *op_name, AstNode *ast)) {
    g_bridge_hook = hook;
}

GraphNode *graph_build(AstNode *ast) {
    if (!ast) return NULL;
    switch (ast->type) {
        case AST_INT: {
            GraphNode *g = gn_alloc(GN_LITERAL);
            g->literal = val_new_volume3d(1, 1, 1, 1.0, 1.0, 1.0);
            g->literal->data[0] = (float)ast->ival;
            return g;
        }
        case AST_FLOAT: {
            GraphNode *g = gn_alloc(GN_LITERAL);
            g->literal = val_new_volume3d(1, 1, 1, 1.0, 1.0, 1.0);
            g->literal->data[0] = (float)ast->fval;
            return g;
        }
        case AST_STRING: {
            GraphNode *g = gn_alloc(GN_LITERAL);
            g->literal = val_new_volume3d(1, 1, 1, 1.0, 1.0, 1.0);
            g->path = strdup(ast->string);
            return g;
        }
        case AST_SYMBOL: {
            GraphNode *g = gn_alloc(GN_REF);
            g->ref_name = strdup(ast->symbol);
            return g;
        }
        case AST_LIST: {
            if (ast->list.count == 0) return gn_alloc(GN_LITERAL);
            AstNode *head = ast->list.items[0];
            if (head->type != AST_SYMBOL) return graph_build(head);
            const char *op_name = head->symbol;

            /* (load "path") */
            if (strcmp(op_name, "load") == 0 && ast->list.count >= 2 && ast->list.items[1]->type == AST_STRING) {
                GraphNode *g = gn_alloc(GN_LOAD);
                g->path = strdup(ast->list.items[1]->string);
                return g;
            }

            /* (print expr) */
            if (strcmp(op_name, "print") == 0 && ast->list.count >= 2) {
                return graph_build(ast->list.items[1]);
            }

            /* (save expr "path") */
            if (strcmp(op_name, "save") == 0 && ast->list.count >= 3 && ast->list.items[2]->type == AST_STRING) {
                GraphNode *g = gn_alloc(GN_SAVE);
                gn_add_arg(g, graph_build(ast->list.items[1]));
                g->path = strdup(ast->list.items[2]->string);
                return g;
            }

            /* (def name expr) — build + eval immediately, bind, return nil */
            if (strcmp(op_name, "def") == 0 && ast->list.count >= 3 && ast->list.items[1]->type == AST_SYMBOL) {
                GraphNode *g = graph_build(ast->list.items[2]);
                graph_eval(g);
                env_bind(ast->list.items[1]->symbol, g->result);
                g->result = NULL; /* env owns it now */
                GraphNode *nil = gn_alloc(GN_LITERAL);
                nil->literal = val_new_nil();
                graph_free(g);
                return nil;
            }

            /* Generic op call */
            OpDef *od = op_find(op_alias(op_name));
            if (od) {
                GraphNode *g = gn_alloc(GN_CALL);
                g->op = od;
                for (int i = 1; i < ast->list.count; i++)
                    gn_add_arg(g, graph_build(ast->list.items[i]));
                return g;
            }

            /* Check bridge hook before failing */
            if (g_bridge_hook) {
                GraphNode *g = g_bridge_hook(op_name, ast);
                if (g) return g;
            }

            fprintf(stderr, "ERROR: unknown operation '%s'\n", op_name);
            return gn_alloc(GN_LITERAL);
        }
    }
    return NULL;
}

int graph_eval(GraphNode *gn) {
    if (!gn || gn->evaluated) return 0;
    gn->evaluated = 1;

    switch (gn->type) {
        case GN_LITERAL: gn->result = gn->literal; return 0;

        case GN_REF: {
            Value *v = env_lookup(gn->ref_name);
            if (!v) { fprintf(stderr, "ERROR: undefined '%s'\n", gn->ref_name); return -1; }
            gn->result = v;
            return 0;
        }

        case GN_LOAD: {
            gn->result = val_load_nifti(gn->path);
            return gn->result ? 0 : -1;
        }

        case GN_SAVE: {
            if (graph_eval(gn->args[0]) != 0) return -1;
            val_save_nifti(gn->args[0]->result, gn->path);
            gn->result = val_new_nil();
            return 0;
        }

        case GN_CALL: {
            /* Validate arg count */
            if (gn->arg_count < gn->op->min_args ||
                (gn->op->max_args >= 0 && gn->arg_count > gn->op->max_args)) {
                fprintf(stderr, "ERROR: %s expects ", gn->op->name);
                if (gn->op->min_args == gn->op->max_args)
                    fprintf(stderr, "%d arg(s)\n", gn->op->min_args);
                else
                    fprintf(stderr, "%d..%d args\n", gn->op->min_args, gn->op->max_args);
                gn->result = val_new_nil();
                return 0;
            }
            Value **argv = calloc(gn->arg_count, sizeof(Value *));
            for (int i = 0; i < gn->arg_count; i++) {
                if (graph_eval(gn->args[i]) != 0) { free(argv); return -1; }
                argv[i] = gn->args[i]->result;
            }
            gn->result = gn->op->fn(gn->arg_count, argv);
            free(argv);
            return 0;
        }
    }
    return 0;
}

void graph_free(GraphNode *gn) {
    if (!gn) return;
    /* Don't free result if it's shared with literal or env ref */
    if (gn->type != GN_REF && gn->type != GN_LITERAL) val_free(gn->result);
    val_free(gn->literal);
    free(gn->ref_name);
    free(gn->path);
    for (int i = 0; i < gn->arg_count; i++) graph_free(gn->args[i]);
    free(gn->args);
    free(gn);
}
