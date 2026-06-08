#ifndef VBL_PARSER_H
#define VBL_PARSER_H

typedef enum {
  AST_INT,
  AST_FLOAT,
  AST_STRING,
  AST_SYMBOL,
  AST_LIST,
} AstType;

typedef struct {
  int count;
  int cap;
  struct AstNode **items;
} AstList;

typedef struct AstNode {
  AstType type;
  union {
    long ival;
    double fval;
    char *string;
    char *symbol;
    AstList list;
  };
} AstNode;

AstNode *parse_expr(const char *s);
void ast_free(AstNode *node);

#endif
