#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Lexer */
typedef enum { TK_LPAREN, TK_RPAREN, TK_INT, TK_FLOAT, TK_STRING, TK_SYMBOL, TK_EOF } TokenType;

typedef struct { TokenType type; char *text; long ival; double fval; } Token;
typedef struct { const char *s; int pos; } Lexer;

static void skip_ws(Lexer *l) {
    while (l->s[l->pos] == ' ' || l->s[l->pos] == '\t' || l->s[l->pos] == '\n' || l->s[l->pos] == '\r') l->pos++;
}

static void skip_block_comment(Lexer *l) {
    l->pos += 2;
    while (l->s[l->pos]) {
        if (l->s[l->pos] == '|' && l->s[l->pos + 1] == '#') { l->pos += 2; return; }
        l->pos++;
    }
}

static Token next_token(Lexer *l) {
    Token t = {0};
    skip_ws(l);
    if (l->s[l->pos] == '#' && l->s[l->pos + 1] == '|') { skip_block_comment(l); skip_ws(l); }
    if (l->s[l->pos] == ';') { while (l->s[l->pos] && l->s[l->pos] != '\n') l->pos++; if (l->s[l->pos] == '\n') l->pos++; skip_ws(l); }
    if (l->s[l->pos] == '\0') { t.type = TK_EOF; return t; }
    if (l->s[l->pos] == '(') { t.type = TK_LPAREN; t.text = strdup("("); l->pos++; return t; }
    if (l->s[l->pos] == ')') { t.type = TK_RPAREN; t.text = strdup(")"); l->pos++; return t; }
    if (l->s[l->pos] == '"') {
        l->pos++; int start = l->pos;
        while (l->s[l->pos] && l->s[l->pos] != '"') l->pos++;
        int len = l->pos - start;
        t.text = strndup(l->s + start, len); t.type = TK_STRING;
        if (l->s[l->pos] == '"') l->pos++;
        return t;
    }
    int start = l->pos;
    while (l->s[l->pos] && !isspace(l->s[l->pos]) && l->s[l->pos] != '(' && l->s[l->pos] != ')' && l->s[l->pos] != '"') l->pos++;
    int len = l->pos - start;
    t.text = strndup(l->s + start, len);
    char *end;
    t.ival = strtol(t.text, &end, 10);
    if (*end == '\0') { t.type = TK_INT; return t; }
    t.fval = strtod(t.text, &end);
    if (*end == '\0') { t.type = TK_FLOAT; return t; }
    t.type = TK_SYMBOL; return t;
}

/* Parser */
static Lexer g_lex; static Token g_tok;

static void advance(void) { free(g_tok.text); g_tok = next_token(&g_lex); }

static AstNode *parse_atom(void) {
    switch (g_tok.type) {
        case TK_INT: { AstNode *n = calloc(1, sizeof(AstNode)); n->type = AST_INT; n->ival = g_tok.ival; advance(); return n; }
        case TK_FLOAT: { AstNode *n = calloc(1, sizeof(AstNode)); n->type = AST_FLOAT; n->fval = g_tok.fval; advance(); return n; }
        case TK_STRING: { AstNode *n = calloc(1, sizeof(AstNode)); n->type = AST_STRING; n->string = g_tok.text; g_tok.text = NULL; advance(); return n; }
        case TK_SYMBOL: { AstNode *n = calloc(1, sizeof(AstNode)); n->type = AST_SYMBOL; n->symbol = g_tok.text; g_tok.text = NULL; advance(); return n; }
        default: return NULL;
    }
}

static void ast_list_push(AstList *list, AstNode *item) {
    if (list->count >= list->cap) { list->cap = list->cap ? list->cap * 2 : 4; list->items = realloc(list->items, list->cap * sizeof(AstNode *)); }
    list->items[list->count++] = item;
}

static AstNode *parse_list(void) {
    advance();
    AstNode *n = calloc(1, sizeof(AstNode)); n->type = AST_LIST;
    while (g_tok.type != TK_RPAREN && g_tok.type != TK_EOF) {
        AstNode *child = (g_tok.type == TK_LPAREN) ? parse_list() : parse_atom();
        if (child) ast_list_push(&n->list, child);
    }
    if (g_tok.type == TK_RPAREN) advance();
    return n;
}

AstNode *parse_expr(const char *s) {
    g_lex.s = s; g_lex.pos = 0;
    advance();
    if (g_tok.type == TK_EOF) return NULL;  /* empty string */
    AstNode *n = (g_tok.type == TK_LPAREN) ? parse_list() : parse_atom();
    if (g_tok.type != TK_EOF) {
        ast_free(n);
        return NULL;  /* trailing junk */
    }
    return n;
}

void ast_free(AstNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_STRING: free(node->string); break;
        case AST_SYMBOL: free(node->symbol); break;
        case AST_LIST:
            for (int i = 0; i < node->list.count; i++) ast_free(node->list.items[i]);
            free(node->list.items);
            break;
        default: break;
    }
    free(node);
}

#ifdef PARSER_TEST
int main(void) {
    const char *tests[] = {
        "42", "3.14", "\"hello\"", "vol1",
        "(load \"test.nii\")",
        "(correlate vol1 (seed vol1 32 45 18))",
        "  ( def  x  42 ) ; comment\n",
        NULL
    };
    for (int i = 0; tests[i]; i++) {
        printf("IN:  %s\nOUT: ", tests[i]);
        AstNode *ast = parse_expr(tests[i]);
        if (!ast) printf("PARSE ERROR\n");
        else if (ast->type == AST_INT) printf("%ld\n", ast->ival);
        else if (ast->type == AST_FLOAT) printf("%f\n", ast->fval);
        else if (ast->type == AST_STRING) printf("\"%s\"\n", ast->string);
        else if (ast->type == AST_SYMBOL) printf("%s\n", ast->symbol);
        else if (ast->type == AST_LIST) printf("(list %d items)\n", ast->list.count);
        ast_free(ast);
    }
    return 0;
}
#endif
