/* expr.h -- Expression tree for ffind filtering.
 *
 * Ported from GNU findutils (tree.c, pred.c, parser.c, defs.h).
 * Copyright (C) 1990-2026 Free Software Foundation, Inc.
 * Modifications for ffind: 2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EXPR_H
#define EXPR_H

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* ============================================================
 * Node types — mirrors findutils predicate_type / tree structure
 * ============================================================ */

enum node_type {
    NODE_PRIMARY,   /* Leaf predicate: -name, -type, etc. */
    NODE_AND,       /* Binary AND (-a, implicit) */
    NODE_OR,        /* Binary OR  (-o) */
    NODE_NOT        /* Unary NOT  (!, -not) */
};

/* ============================================================
 * Predicate function signature
 *
 * path     — full path of the file
 * basename — just the filename component
 * d_type   — dirent d_type (DT_REG, DT_DIR, etc.), may be DT_UNKNOWN
 * st       — stat buffer; may be zeroed if stat hasn't been called yet.
 *            The predicate should call lstat() itself if it needs stat
 *            info and st->st_ino == 0 (our "not yet stated" sentinel).
 * arg      — predicate-specific opaque data (pattern string, etc.)
 * ============================================================ */

typedef bool (*pred_func_t)(const char *path, const char *basename,
                            unsigned char d_type, struct stat *st, void *arg);

/* ============================================================
 * Expression tree node
 * ============================================================ */

typedef struct pred_node {
    enum node_type  type;
    pred_func_t     func;        /* Only for NODE_PRIMARY */
    void           *arg;         /* Predicate-specific data */
    bool            needs_stat;  /* Does this predicate require stat()? */
    struct pred_node *left;      /* Left child (or NULL) */
    struct pred_node *right;     /* Right child (or operand for NOT) */
} pred_node_t;

/* ============================================================
 * Comparison type for numeric predicates (-size, -mtime, etc.)
 * Mirrors findutils enum comparison_type.
 * ============================================================ */

enum comparison_type {
    COMP_EQ,   /* exact match (no prefix) */
    COMP_GT,   /* greater than ('+' prefix) */
    COMP_LT    /* less than   ('-' prefix) */
};

/* Argument struct for -size */
typedef struct {
    enum comparison_type kind;
    int      blocksize;   /* bytes per unit: 1 for 'c', 512 for default, etc. */
    unsigned long long size;
} size_arg_t;

/* Argument struct for -mtime / -atime / -ctime */
typedef struct {
    enum comparison_type kind;
    long long days;       /* number of 24h periods */
    time_t    origin;     /* reference time (program start - days*86400) */
} time_arg_t;

/* Argument struct for -perm */
typedef struct {
    enum comparison_type kind;   /* COMP_EQ = exact, COMP_GT = -perm -xxx (at least), COMP_LT unused */
    mode_t mode;
    bool any;                    /* true for -perm /mode (any bit match) */
} perm_arg_t;

/* ============================================================
 * Public API
 * ============================================================ */

/*
 * build_expression_tree
 *
 * Parse argv[expr_start .. argc-1] into a binary expression tree.
 * Returns the root of the tree, or NULL if no expression was given
 * (caller should treat NULL as "match everything").
 *
 * On parse error, prints a message to stderr and returns NULL.
 *
 * This mirrors findutils build_expression_tree() in tree.c, but
 * simplified: no optimization passes, no -print wrapping (ffind
 * always prints matches).
 */
pred_node_t *build_expression_tree(int argc, char **argv, int expr_start);

/*
 * evaluate
 *
 * Evaluate the expression tree against a single file.
 * Returns true if the file matches the expression.
 *
 * This mirrors findutils apply_predicate() + pred_and/pred_or/pred_negate.
 *
 * Lazy stat: stat is only called when a predicate actually needs it.
 * The stat buffer is shared across the whole evaluation so we stat
 * at most once per file.
 */
bool evaluate(pred_node_t *node, const char *path, const char *basename,
              unsigned char d_type, struct stat *st);

/*
 * free_expression_tree
 *
 * Recursively free all nodes and their arg data.
 */
void free_expression_tree(pred_node_t *node);

#endif /* EXPR_H */
