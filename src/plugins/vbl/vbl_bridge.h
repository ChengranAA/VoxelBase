/*
 *  vbl_bridge.h — VBL language bridge into VoxelBase REPL
 *
 *  Usage:
 *    vbl_bridge_init(app);      // once at startup
 *    vbl_bridge_eval(input);    // evaluate S-expression, writes to
 * app->repl_output vbl_bridge_shutdown();     // once at exit
 *
 *  The bridge wraps the VBL worker engine (parser, graph, ops, env, pool).
 *  It adds one bridge op: (slot n) — copies BareMRI slot n into a VBL Value.
 *  All VBL values are independent copies; slot mutations don't affect them.
 *
 *  Non-S-expression input (cmap, zoom, cross, load, help) is still handled
 *  by the existing REPL in panel.c.  The bridge only processes lines that
 *  start with '(' or ';'.
 */
#ifndef VBL_BRIDGE_H
#define VBL_BRIDGE_H

void vbl_bridge_init(void *app);
void vbl_bridge_shutdown(void);
int vbl_bridge_eval(void *app, const char *input);

#endif
