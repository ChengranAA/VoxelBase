#ifndef VBL_GRAPH_H
#define VBL_GRAPH_H

#include "ops.h"
#include "parser.h"
#include "types.h"

typedef enum { GN_LITERAL, GN_LOAD, GN_SAVE, GN_CALL, GN_REF } GraphNodeType;

typedef struct GraphNode {
  GraphNodeType type;
  OpDef *op;
  Value *literal;
  char *ref_name;
  char *path;
  int arg_count;
  struct GraphNode **args;
  Value *result;
  int evaluated;
} GraphNode;

GraphNode *graph_build(AstNode *ast);
int graph_eval(GraphNode *gn);
void graph_free(GraphNode *gn);

/* Bridge hook: set a callback to intercept unknown ops */
void graph_set_bridge_hook(GraphNode *(*hook)(const char *op_name,
                                              AstNode *ast));

#endif
