/* expr.c -- Expression tree parser and evaluator for ffind.
 *
 * Ported from GNU findutils:
 *   - tree.c:   get_expr(), scan_rest(), build_expression_tree()
 *   - pred.c:   pred_name, pred_type, pred_and, pred_or, pred_negate, etc.
 *   - parser.c: parse_name, parse_type, parse_table[], find_parser()
 *   - util.c:   apply_predicate()
 *
 * Copyright (C) 1990-2026 Free Software Foundation, Inc.
 * Modifications for ffind: 2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include "../headers/expr.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define DAYSECS 86400

/* ================================================================
 * Internal helpers
 * ================================================================ */

/* Ensure stat buffer is populated.  We use st_ino == 0 as the
 * "not yet stated" sentinel (inode 0 is unused on all real filesystems).
 *
 * This implements the "lazy stat" optimisation from findutils:
 * apply_predicate() only calls stat when the predicate needs it.
 */
static bool ensure_stat(const char *path, struct stat *st)
{
    if (st->st_ino != 0)
        return true;   /* already stated */
    if (lstat(path, st) != 0)
        return false;  /* stat failed */
    /* If st_ino happened to be 0 after a real stat (theoretically
     * possible on some FS), set it to 1 so we don't re-stat. */
    if (st->st_ino == 0)
        st->st_ino = 1;
    return true;
}

/* Map dirent d_type to S_IFMT mode bits.
 * From findutils ftsfind.c digest_mode() / defs.h.
 */
static mode_t dtype_to_mode(unsigned char d_type)
{
    switch (d_type) {
    case DT_REG:  return S_IFREG;
    case DT_DIR:  return S_IFDIR;
    case DT_LNK:  return S_IFLNK;
    case DT_CHR:  return S_IFCHR;
    case DT_BLK:  return S_IFBLK;
    case DT_FIFO: return S_IFIFO;
    case DT_SOCK: return S_IFSOCK;
    default:      return 0;  /* DT_UNKNOWN */
    }
}

/* Parse a comparison prefix: +N means GT, -N means LT, N means EQ.
 * Advances *str past the prefix character if present.
 * From findutils parser.c get_comp_type().
 */
static enum comparison_type parse_comp_type(const char **str)
{
    if (**str == '+') {
        (*str)++;
        return COMP_GT;
    } else if (**str == '-') {
        (*str)++;
        return COMP_LT;
    }
    return COMP_EQ;
}

/* ================================================================
 * Node allocation helpers
 * (mirrors findutils get_new_pred / insert_primary / make_binop)
 * ================================================================ */

static pred_node_t *make_node(enum node_type type)
{
    pred_node_t *n = calloc(1, sizeof(*n));
    if (!n) {
        fprintf(stderr, "ffind: out of memory\n");
        exit(1);
    }
    n->type = type;
    return n;
}

static pred_node_t *make_primary(pred_func_t fn, void *arg, bool needs_stat)
{
    pred_node_t *n = make_node(NODE_PRIMARY);
    n->func = fn;
    n->arg = arg;
    n->needs_stat = needs_stat;
    return n;
}

static pred_node_t *make_binop(enum node_type type, pred_node_t *l, pred_node_t *r)
{
    pred_node_t *n = make_node(type);
    n->left = l;
    n->right = r;
    return n;
}

/* ================================================================
 * Leaf predicates
 * Each mirrors its findutils counterpart from pred.c.
 * ================================================================ */

/* -name PATTERN
 * From findutils pred_name() / pred_name_common().
 * Uses fnmatch on the basename only, no stat needed.
 */
static bool fn_pred_name(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)path; (void)d_type; (void)st;
    return fnmatch((const char *)arg, base, 0) == 0;
}

/* -iname PATTERN
 * From findutils pred_iname() — case-insensitive variant.
 */
static bool fn_pred_iname(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)path; (void)d_type; (void)st;
    return fnmatch((const char *)arg, base, FNM_CASEFOLD) == 0;
}

/* -path PATTERN  (also -wholename)
 * From findutils pred_path().
 * fnmatch on the full path, no stat needed.
 */
static bool fn_pred_path(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)st;
    return fnmatch((const char *)arg, path, 0) == 0;
}

/* -ipath PATTERN  (also -iwholename)
 * From findutils pred_ipath().
 */
static bool fn_pred_ipath(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)st;
    return fnmatch((const char *)arg, path, FNM_CASEFOLD) == 0;
}

/* -type TYPE_CHAR
 * From findutils pred_type().
 *
 * The arg is a pointer to a mode_t holding the S_IFMT value for
 * the requested type (e.g. S_IFREG for 'f', S_IFDIR for 'd').
 *
 * Uses d_type when available to avoid stat; falls back to lstat
 * when d_type is DT_UNKNOWN (same logic as findutils NeedsType).
 */
static bool fn_pred_type(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base;
    mode_t want = *(mode_t *)arg;
    mode_t have;

    if (d_type != DT_UNKNOWN) {
        have = dtype_to_mode(d_type);
    } else {
        /* d_type unknown — need stat.  From findutils: when d_type
         * is DT_UNKNOWN, we fall back to stat to get the type. */
        if (!ensure_stat(path, st))
            return false;
        have = st->st_mode & S_IFMT;
    }
    return have == want;
}

/* -empty
 * From findutils pred_empty().
 * For regular files: true if size == 0.
 * For directories: true if directory contains only . and ..
 */
static bool fn_pred_empty(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)arg;

    /* Determine file type */
    mode_t mode;
    if (d_type != DT_UNKNOWN) {
        mode = dtype_to_mode(d_type);
    } else {
        if (!ensure_stat(path, st))
            return false;
        mode = st->st_mode & S_IFMT;
    }

    if (mode == S_IFDIR) {
        DIR *d = opendir(path);
        if (!d)
            return false;
        struct dirent *ent;
        bool empty = true;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] != '.'
                || (ent->d_name[1] != '\0'
                    && (ent->d_name[1] != '.' || ent->d_name[2] != '\0'))) {
                empty = false;
                break;
            }
        }
        closedir(d);
        return empty;
    } else if (mode == S_IFREG) {
        if (!ensure_stat(path, st))
            return false;
        return st->st_size == 0;
    }
    return false;
}

/* -size [+|-]N[bcwkMG]
 * From findutils pred_size().
 */
static bool fn_pred_size(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    size_arg_t *sa = (size_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    unsigned long long f_val = ((unsigned long long)st->st_size / sa->blocksize)
        + (st->st_size % sa->blocksize != 0 ? 1 : 0);

    switch (sa->kind) {
    case COMP_GT: return f_val > sa->size;
    case COMP_LT: return f_val < sa->size;
    case COMP_EQ: return f_val == sa->size;
    }
    return false;
}

/* -mtime [+|-]N
 * From findutils pred_mtime() / pred_timewindow().
 *
 * N means: file was modified exactly N*24h ago (within a 24h window)
 * +N means: file was modified more than N*24h ago
 * -N means: file was modified less than N*24h ago
 */
static bool fn_pred_mtime(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    /* Age of file in days (rounded up, like findutils) */
    time_t file_age = ta->origin - st->st_mtime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * DAYSECS;
    case COMP_LT: return file_age < ta->days * DAYSECS;
    case COMP_EQ:
        /* File is exactly N days old: age is in [N*86400, (N+1)*86400) */
        return file_age >= ta->days * DAYSECS && file_age < (ta->days + 1) * DAYSECS;
    }
    return false;
}

/* -atime [+|-]N
 * Same as -mtime but uses st_atime.
 */
static bool fn_pred_atime(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    time_t file_age = ta->origin - st->st_atime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * DAYSECS;
    case COMP_LT: return file_age < ta->days * DAYSECS;
    case COMP_EQ:
        return file_age >= ta->days * DAYSECS && file_age < (ta->days + 1) * DAYSECS;
    }
    return false;
}

/* -ctime [+|-]N
 * Same as -mtime but uses st_ctime.
 */
static bool fn_pred_ctime(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    time_t file_age = ta->origin - st->st_ctime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * DAYSECS;
    case COMP_LT: return file_age < ta->days * DAYSECS;
    case COMP_EQ:
        return file_age >= ta->days * DAYSECS && file_age < (ta->days + 1) * DAYSECS;
    }
    return false;
}

/* -mmin [+|-]N
 * From findutils pred_mmin(). Like -mtime but in minutes.
 */
static bool fn_pred_mmin(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    time_t file_age = ta->origin - st->st_mtime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * 60;
    case COMP_LT: return file_age < ta->days * 60;
    case COMP_EQ:
        return file_age >= ta->days * 60 && file_age < (ta->days + 1) * 60;
    }
    return false;
}

/* -amin [+|-]N */
static bool fn_pred_amin(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    time_t file_age = ta->origin - st->st_atime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * 60;
    case COMP_LT: return file_age < ta->days * 60;
    case COMP_EQ:
        return file_age >= ta->days * 60 && file_age < (ta->days + 1) * 60;
    }
    return false;
}

/* -cmin [+|-]N */
static bool fn_pred_cmin(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    time_t file_age = ta->origin - st->st_ctime;

    switch (ta->kind) {
    case COMP_GT: return file_age > ta->days * 60;
    case COMP_LT: return file_age < ta->days * 60;
    case COMP_EQ:
        return file_age >= ta->days * 60 && file_age < (ta->days + 1) * 60;
    }
    return false;
}

/* -newer FILE
 * From findutils pred_newer().
 * True if the file being tested was modified more recently than FILE.
 */
static bool fn_pred_newer(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_t ref_mtime = *(time_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    return st->st_mtime > ref_mtime;
}

/* -perm [/-]MODE
 * From findutils pred_perm().
 */
static bool fn_pred_perm(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    perm_arg_t *pa = (perm_arg_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    mode_t file_mode = st->st_mode & 07777;  /* permission bits only */

    if (pa->any) {
        /* -perm /mode: any of the permission bits match */
        if (pa->mode == 0)
            return true;  /* -perm /0 is always true (findutils Savannah bug 14748) */
        return (file_mode & pa->mode) != 0;
    }

    switch (pa->kind) {
    case COMP_EQ:
        /* -perm mode: exact match */
        return file_mode == pa->mode;
    case COMP_GT:
        /* -perm -mode: all specified bits must be set */
        return (file_mode & pa->mode) == pa->mode;
    default:
        return false;
    }
}

/* -links [+|-]N
 * From findutils pred_links().
 */
static bool fn_pred_links(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    time_arg_t *ta = (time_arg_t *)arg;  /* reusing time_arg_t for the numeric comparison */

    if (!ensure_stat(path, st))
        return false;

    nlink_t nlinks = st->st_nlink;
    unsigned long long val = (unsigned long long)ta->days;  /* stored in 'days' field */

    switch (ta->kind) {
    case COMP_GT: return nlinks > val;
    case COMP_LT: return nlinks < val;
    case COMP_EQ: return nlinks == val;
    }
    return false;
}

/* -user NAME_OR_UID
 * From findutils pred_user() / pred_uid().
 * arg is a pointer to uid_t.
 */
static bool fn_pred_user(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    uid_t want = *(uid_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    return st->st_uid == want;
}

/* -group NAME_OR_GID
 * From findutils pred_group() / pred_gid().
 * arg is a pointer to gid_t.
 */
static bool fn_pred_group(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type;
    gid_t want = *(gid_t *)arg;

    if (!ensure_stat(path, st))
        return false;

    return st->st_gid == want;
}

/* -nouser
 * From findutils pred_nouser().
 */
static bool fn_pred_nouser(const char *path, const char *base,
                           unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)arg;
    if (!ensure_stat(path, st))
        return false;
    return getpwuid(st->st_uid) == NULL;
}

/* -nogroup
 * From findutils pred_nogroup().
 */
static bool fn_pred_nogroup(const char *path, const char *base,
                            unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)arg;
    if (!ensure_stat(path, st))
        return false;
    return getgrgid(st->st_gid) == NULL;
}

/* -readable
 * From findutils pred_readable().
 */
static bool fn_pred_readable(const char *path, const char *base,
                             unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)st; (void)arg;
    return access(path, R_OK) == 0;
}

/* -writable
 * From findutils pred_writable().
 */
static bool fn_pred_writable(const char *path, const char *base,
                             unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)st; (void)arg;
    return access(path, W_OK) == 0;
}

/* -executable
 * From findutils pred_executable().
 */
static bool fn_pred_executable(const char *path, const char *base,
                               unsigned char d_type, struct stat *st, void *arg)
{
    (void)base; (void)d_type; (void)st; (void)arg;
    return access(path, X_OK) == 0;
}

/* -true
 * From findutils pred_true().
 */
static bool fn_pred_true(const char *path, const char *base,
                         unsigned char d_type, struct stat *st, void *arg)
{
    (void)path; (void)base; (void)d_type; (void)st; (void)arg;
    return true;
}

/* -false
 * From findutils pred_false().
 */
static bool fn_pred_false(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)path; (void)base; (void)d_type; (void)st; (void)arg;
    return false;
}

/* -prune
 * From findutils pred_prune().
 * In ffind, -prune always evaluates to true (like POSIX requires).
 * The actual pruning logic (skipping directory traversal) would need
 * integration with the traversal engine — for now it's a no-op test
 * that returns true.
 */
static bool fn_pred_prune(const char *path, const char *base,
                          unsigned char d_type, struct stat *st, void *arg)
{
    (void)path; (void)base; (void)d_type; (void)st; (void)arg;
    return true;
}

/* ================================================================
 * Type character → S_IFMT mapping
 * From findutils parser.c insert_type() / pred.c pred_type().
 * ================================================================ */

static bool type_char_to_mode(char c, mode_t *out)
{
    switch (c) {
    case 'f': *out = S_IFREG;  return true;
    case 'd': *out = S_IFDIR;  return true;
    case 'l': *out = S_IFLNK;  return true;
    case 'c': *out = S_IFCHR;  return true;
    case 'b': *out = S_IFBLK;  return true;
    case 'p': *out = S_IFIFO;  return true;
    case 's': *out = S_IFSOCK; return true;
    default:  return false;
    }
}

/* ================================================================
 * Parser
 *
 * Recursive descent with operator precedence, ported from findutils
 * tree.c get_expr()/scan_rest() and simplified.
 *
 * Grammar:
 *   expr     := and_expr ( '-o' and_expr )*
 *   and_expr := primary  ( ['-a'] primary )*     ← implicit AND
 *   primary  := '(' expr ')'
 *             | '!' primary | '-not' primary
 *             | TEST [ARG]
 *
 * Precedence (highest binds tightest):
 *   NOT (!)      — unary, highest
 *   AND (-a)     — binary, implicit between adjacent primaries
 *   OR  (-o)     — binary, lowest
 *
 * This matches the findutils precedence table exactly.
 * ================================================================ */

/* Forward declarations */
static pred_node_t *parse_expr(int argc, char **argv, int *i);
static pred_node_t *parse_and_expr(int argc, char **argv, int *i);
static pred_node_t *parse_primary(int argc, char **argv, int *i);

/* Check if a token is an operator or punctuation that ends a primary sequence.
 * Used to detect the boundary of an AND group.
 */
static bool is_expr_terminator(const char *tok)
{
    return strcmp(tok, "-o") == 0
        || strcmp(tok, "-or") == 0
        || strcmp(tok, ")") == 0;
}

/* Check if a token looks like the start of a primary (test or punctuation).
 * From findutils util.c looks_like_expression().
 */
static bool is_primary_start(const char *tok)
{
    if (tok[0] == '-' && tok[1] != '\0') return true;
    if ((tok[0] == '!' || tok[0] == '(') && tok[1] == '\0') return true;
    return false;
}

/* Parse a single primary: a test with optional argument, NOT, or parenthesized expr.
 *
 * This corresponds to findutils get_expr()'s PRIMARY_TYPE, UNI_OP,
 * and OPEN_PAREN cases.
 */
static pred_node_t *parse_primary(int argc, char **argv, int *i)
{
    if (*i >= argc)
        return NULL;

    const char *tok = argv[*i];

    /* ---- Parenthesized expression ---- */
    /* From findutils tree.c get_expr() case OPEN_PAREN */
    if (strcmp(tok, "(") == 0) {
        (*i)++;
        pred_node_t *expr = parse_expr(argc, argv, i);
        if (*i < argc && strcmp(argv[*i], ")") == 0) {
            (*i)++;
        } else {
            fprintf(stderr, "ffind: missing closing ')'\n");
        }
        return expr;
    }

    /* ---- NOT operator ---- */
    /* From findutils tree.c get_expr() case UNI_OP → pred_negate */
    if (strcmp(tok, "!") == 0 || strcmp(tok, "-not") == 0) {
        (*i)++;
        pred_node_t *operand = parse_primary(argc, argv, i);
        if (!operand) {
            fprintf(stderr, "ffind: expected expression after '%s'\n", tok);
            return NULL;
        }
        pred_node_t *n = make_node(NODE_NOT);
        n->right = operand;
        return n;
    }

    /* ---- -name PATTERN ---- */
    /* From findutils parser.c parse_name() → pred_name */
    if (strcmp(tok, "-name") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        return make_primary(fn_pred_name, (void *)argv[*i - 1], false);
    }

    /* ---- -iname PATTERN ---- */
    /* From findutils parser.c parse_iname() → pred_iname */
    if (strcmp(tok, "-iname") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        return make_primary(fn_pred_iname, (void *)argv[*i - 1], false);
    }

    /* ---- -path / -wholename PATTERN ---- */
    /* From findutils parser.c parse_path() / parse_wholename() → pred_path */
    if (strcmp(tok, "-path") == 0 || strcmp(tok, "-wholename") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        return make_primary(fn_pred_path, (void *)argv[*i - 1], false);
    }

    /* ---- -ipath / -iwholename PATTERN ---- */
    /* From findutils parser.c parse_ipath() / parse_iwholename() → pred_ipath */
    if (strcmp(tok, "-ipath") == 0 || strcmp(tok, "-iwholename") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        return make_primary(fn_pred_ipath, (void *)argv[*i - 1], false);
    }

    /* ---- -type TYPE_CHAR ---- */
    /* From findutils parser.c parse_type() / insert_type() → pred_type */
    if (strcmp(tok, "-type") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *type_str = argv[*i - 1];
        if (strlen(type_str) != 1) {
            fprintf(stderr, "ffind: invalid argument '%s' to '-type'\n", type_str);
            return NULL;
        }
        mode_t *m = malloc(sizeof(mode_t));
        if (!m) { perror("malloc"); exit(1); }
        if (!type_char_to_mode(type_str[0], m)) {
            fprintf(stderr, "ffind: unknown type '%c' for '-type'\n", type_str[0]);
            free(m);
            return NULL;
        }
        return make_primary(fn_pred_type, m, false);
    }

    /* ---- -empty ---- */
    /* From findutils parser.c parse_empty() → pred_empty */
    if (strcmp(tok, "-empty") == 0) {
        (*i)++;
        return make_primary(fn_pred_empty, NULL, false);
    }

    /* ---- -size [+|-]N[bcwkMG] ---- */
    /* From findutils parser.c parse_size() → pred_size */
    if (strcmp(tok, "-size") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        size_arg_t *sa = malloc(sizeof(size_arg_t));
        if (!sa) { perror("malloc"); exit(1); }

        sa->kind = parse_comp_type(&arg_str);

        char *endp;
        sa->size = strtoull(arg_str, &endp, 10);

        /* Parse optional suffix — from findutils parser.c parse_size() */
        switch (*endp) {
        case 'c': sa->blocksize = 1;         break;
        case 'w': sa->blocksize = 2;         break;
        case 'k': sa->blocksize = 1024;      break;
        case 'M': sa->blocksize = 1048576;   break;
        case 'G': sa->blocksize = 1073741824; break;
        case 'b': /* fallthrough */
        case '\0': sa->blocksize = 512;      break;
        default:
            fprintf(stderr, "ffind: invalid size suffix '%c'\n", *endp);
            free(sa);
            return NULL;
        }
        return make_primary(fn_pred_size, sa, true);
    }

    /* ---- -mtime [+|-]N ---- */
    /* From findutils parser.c parse_time() → pred_mtime */
    if (strcmp(tok, "-mtime") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);
        ta->origin = time(NULL);

        return make_primary(fn_pred_mtime, ta, true);
    }

    /* ---- -atime [+|-]N ---- */
    if (strcmp(tok, "-atime") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);
        ta->origin = time(NULL);

        return make_primary(fn_pred_atime, ta, true);
    }

    /* ---- -ctime [+|-]N ---- */
    if (strcmp(tok, "-ctime") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);
        ta->origin = time(NULL);

        return make_primary(fn_pred_ctime, ta, true);
    }

    /* ---- -mmin [+|-]N ---- */
    /* From findutils parser.c parse_mmin() → pred_mmin */
    if (strcmp(tok, "-mmin") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);  /* minutes, stored in 'days' field */
        ta->origin = time(NULL);

        return make_primary(fn_pred_mmin, ta, true);
    }

    /* ---- -amin [+|-]N ---- */
    if (strcmp(tok, "-amin") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);
        ta->origin = time(NULL);

        return make_primary(fn_pred_amin, ta, true);
    }

    /* ---- -cmin [+|-]N ---- */
    if (strcmp(tok, "-cmin") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);
        ta->origin = time(NULL);

        return make_primary(fn_pred_cmin, ta, true);
    }

    /* ---- -newer FILE ---- */
    /* From findutils parser.c parse_newer() → pred_newer */
    if (strcmp(tok, "-newer") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *ref_file = argv[*i - 1];

        struct stat ref_st;
        if (stat(ref_file, &ref_st) != 0) {
            fprintf(stderr, "ffind: cannot stat '%s': %s\n", ref_file, strerror(errno));
            return NULL;
        }
        time_t *ref_mtime = malloc(sizeof(time_t));
        if (!ref_mtime) { perror("malloc"); exit(1); }
        *ref_mtime = ref_st.st_mtime;

        return make_primary(fn_pred_newer, ref_mtime, true);
    }

    /* ---- -perm [/-]MODE ---- */
    /* From findutils parser.c parse_perm() → pred_perm */
    if (strcmp(tok, "-perm") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        perm_arg_t *pa = malloc(sizeof(perm_arg_t));
        if (!pa) { perror("malloc"); exit(1); }
        pa->any = false;

        if (arg_str[0] == '/') {
            pa->any = true;
            arg_str++;
            pa->kind = COMP_EQ;  /* not used when any=true */
        } else if (arg_str[0] == '-') {
            pa->kind = COMP_GT;  /* at-least semantics */
            arg_str++;
        } else {
            pa->kind = COMP_EQ;  /* exact match */
        }

        pa->mode = (mode_t)strtol(arg_str, NULL, 8);
        return make_primary(fn_pred_perm, pa, true);
    }

    /* ---- -links [+|-]N ---- */
    /* From findutils parser.c parse_links() → pred_links */
    if (strcmp(tok, "-links") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        time_arg_t *ta = malloc(sizeof(time_arg_t));
        if (!ta) { perror("malloc"); exit(1); }

        ta->kind = parse_comp_type(&arg_str);
        ta->days = strtoll(arg_str, NULL, 10);  /* reusing 'days' for the count */
        ta->origin = 0;

        return make_primary(fn_pred_links, ta, true);
    }

    /* ---- -user NAME_OR_UID ---- */
    /* From findutils parser.c parse_user() → pred_user */
    if (strcmp(tok, "-user") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        uid_t *uid = malloc(sizeof(uid_t));
        if (!uid) { perror("malloc"); exit(1); }

        /* Try as numeric UID first */
        char *endp;
        unsigned long val = strtoul(arg_str, &endp, 10);
        if (*endp == '\0') {
            *uid = (uid_t)val;
        } else {
            /* Try as username */
            struct passwd *pw = getpwnam(arg_str);
            if (!pw) {
                fprintf(stderr, "ffind: unknown user '%s'\n", arg_str);
                free(uid);
                return NULL;
            }
            *uid = pw->pw_uid;
        }
        return make_primary(fn_pred_user, uid, true);
    }

    /* ---- -group NAME_OR_GID ---- */
    /* From findutils parser.c parse_group() → pred_group */
    if (strcmp(tok, "-group") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "ffind: missing argument to '%s'\n", tok);
            return NULL;
        }
        (*i) += 2;
        const char *arg_str = argv[*i - 1];

        gid_t *gid = malloc(sizeof(gid_t));
        if (!gid) { perror("malloc"); exit(1); }

        char *endp;
        unsigned long val = strtoul(arg_str, &endp, 10);
        if (*endp == '\0') {
            *gid = (gid_t)val;
        } else {
            struct group *gr = getgrnam(arg_str);
            if (!gr) {
                fprintf(stderr, "ffind: unknown group '%s'\n", arg_str);
                free(gid);
                return NULL;
            }
            *gid = gr->gr_gid;
        }
        return make_primary(fn_pred_group, gid, true);
    }

    /* ---- -nouser ---- */
    if (strcmp(tok, "-nouser") == 0) {
        (*i)++;
        return make_primary(fn_pred_nouser, NULL, true);
    }

    /* ---- -nogroup ---- */
    if (strcmp(tok, "-nogroup") == 0) {
        (*i)++;
        return make_primary(fn_pred_nogroup, NULL, true);
    }

    /* ---- -readable ---- */
    if (strcmp(tok, "-readable") == 0) {
        (*i)++;
        return make_primary(fn_pred_readable, NULL, false);
    }

    /* ---- -writable ---- */
    if (strcmp(tok, "-writable") == 0) {
        (*i)++;
        return make_primary(fn_pred_writable, NULL, false);
    }

    /* ---- -executable ---- */
    if (strcmp(tok, "-executable") == 0) {
        (*i)++;
        return make_primary(fn_pred_executable, NULL, false);
    }

    /* ---- -true ---- */
    if (strcmp(tok, "-true") == 0) {
        (*i)++;
        return make_primary(fn_pred_true, NULL, false);
    }

    /* ---- -false ---- */
    if (strcmp(tok, "-false") == 0) {
        (*i)++;
        return make_primary(fn_pred_false, NULL, false);
    }

    /* ---- -prune ---- */
    if (strcmp(tok, "-prune") == 0) {
        (*i)++;
        return make_primary(fn_pred_prune, NULL, false);
    }

    /* ---- Unknown predicate ---- */
    fprintf(stderr, "ffind: unknown predicate '%s'\n", tok);
    return NULL;
}

/* parse_and_expr: handle implicit AND and explicit -a.
 *
 * From findutils tree.c scan_rest() — processes a chain of
 * primaries connected by AND (implicit or explicit -a).
 *
 * AND has higher precedence than OR, so we recurse into parse_primary
 * and consume -a tokens between them.
 */
static pred_node_t *parse_and_expr(int argc, char **argv, int *i)
{
    pred_node_t *left = parse_primary(argc, argv, i);
    if (!left)
        return NULL;

    while (*i < argc) {
        /* Stop at OR or close-paren — they belong to the caller. */
        if (is_expr_terminator(argv[*i]))
            break;

        /* Consume explicit -a / -and if present */
        if (strcmp(argv[*i], "-a") == 0 || strcmp(argv[*i], "-and") == 0)
            (*i)++;

        /* Implicit AND: if next token starts a primary, parse it and AND them.
         * From findutils tree.c: get_new_pred_chk_op() auto-inserts AND
         * when two primaries are adjacent. */
        pred_node_t *right = parse_primary(argc, argv, i);
        if (!right)
            break;

        left = make_binop(NODE_AND, left, right);
    }
    return left;
}

/* parse_expr: handle OR (-o).
 *
 * From findutils tree.c get_expr()/scan_rest() for BI_OP with OR_PREC.
 * OR has the lowest precedence among binary operators.
 */
static pred_node_t *parse_expr(int argc, char **argv, int *i)
{
    pred_node_t *left = parse_and_expr(argc, argv, i);
    if (!left)
        return NULL;

    while (*i < argc && (strcmp(argv[*i], "-o") == 0 || strcmp(argv[*i], "-or") == 0)) {
        (*i)++;  /* consume -o */
        pred_node_t *right = parse_and_expr(argc, argv, i);
        if (!right) {
            fprintf(stderr, "ffind: expected expression after '-o'\n");
            break;
        }
        left = make_binop(NODE_OR, left, right);
    }
    return left;
}

/* ================================================================
 * Public API: build_expression_tree
 *
 * Mirrors findutils tree.c build_expression_tree() but simplified:
 * - No optimization passes (no cost estimation, no arm swaps)
 * - No -print wrapping (ffind always prints matches from the caller)
 * - No flat linked list intermediate form; we build the tree directly
 *   via recursive descent
 * ================================================================ */

pred_node_t *build_expression_tree(int argc, char **argv, int expr_start)
{
    if (expr_start >= argc) {
        /* No expression given — match everything (like GNU find). */
        return NULL;
    }

    int i = expr_start;
    pred_node_t *tree = parse_expr(argc, argv, &i);

    /* Check for unconsumed tokens */
    if (i < argc) {
        fprintf(stderr, "ffind: unexpected argument '%s'\n", argv[i]);
        free_expression_tree(tree);
        return NULL;
    }

    return tree;
}

/* ================================================================
 * Public API: evaluate
 *
 * Mirrors findutils util.c apply_predicate() + pred.c tree operators
 * (pred_and, pred_or, pred_negate).
 *
 * Implements short-circuit evaluation:
 *   AND: returns false at the first failure
 *   OR:  returns true at the first success
 *
 * Lazy stat: only calls lstat() when a leaf predicate actually
 * needs it (by checking needs_stat flag and st_ino sentinel).
 * ================================================================ */

bool evaluate(pred_node_t *node, const char *path, const char *base,
              unsigned char d_type, struct stat *st)
{
    if (!node)
        return true;  /* NULL tree = match everything */

    switch (node->type) {
    case NODE_PRIMARY:
        return node->func(path, base, d_type, st, node->arg);

    case NODE_AND:
        /* From findutils pred.c pred_and():
         * Short-circuit: if left fails, skip right. */
        return evaluate(node->left, path, base, d_type, st)
            && evaluate(node->right, path, base, d_type, st);

    case NODE_OR:
        /* From findutils pred.c pred_or():
         * Short-circuit: if left succeeds, skip right. */
        return evaluate(node->left, path, base, d_type, st)
            || evaluate(node->right, path, base, d_type, st);

    case NODE_NOT:
        /* From findutils pred.c pred_negate():
         * Operand is in pred_right. */
        return !evaluate(node->right, path, base, d_type, st);
    }

    return false;
}

/* ================================================================
 * Public API: free_expression_tree
 * ================================================================ */

void free_expression_tree(pred_node_t *node)
{
    if (!node)
        return;

    free_expression_tree(node->left);
    free_expression_tree(node->right);

    /* Free dynamically allocated arg data for certain predicates */
    if (node->type == NODE_PRIMARY && node->arg) {
        if (node->func == fn_pred_type
            || node->func == fn_pred_size
            || node->func == fn_pred_mtime
            || node->func == fn_pred_atime
            || node->func == fn_pred_ctime
            || node->func == fn_pred_mmin
            || node->func == fn_pred_amin
            || node->func == fn_pred_cmin
            || node->func == fn_pred_newer
            || node->func == fn_pred_perm
            || node->func == fn_pred_links
            || node->func == fn_pred_user
            || node->func == fn_pred_group) {
            free(node->arg);
        }
        /* String args (name, path patterns) point into argv — don't free */
    }

    free(node);
}
