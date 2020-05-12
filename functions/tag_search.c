/*
  Copyright(C) 2016 Naoya Murakami <naoya@ipnexus.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <string.h>
#include <stdlib.h>
#include <groonga/plugin.h>

#ifdef __GNUC__
# define GNUC_UNUSED __attribute__((__unused__))
#else
# define GNUC_UNUSED
#endif

/* copy from lib/grn_rset.h */

#define GRN_RSET_UTIL_BIT (0x80000000)

typedef struct {
  double score;
  int n_subrecs;
  int subrecs[1];
} grn_rset_recinfo;

typedef struct {
  grn_id rid;
  uint32_t sid;
  uint32_t pos;
} grn_rset_posinfo;
/* lib/grn_rset.h */

#define GRN_CTX_TEMPORARY_DISABLE_II_RESOLVE_SEL_AND (0x40)

static void
grn_ii_resolve_sel_and_(grn_ctx *ctx, grn_hash *s, grn_operator op)
{
  if (op == GRN_OP_AND
      && !(ctx->flags & GRN_CTX_TEMPORARY_DISABLE_II_RESOLVE_SEL_AND)) {
    grn_id eid;
    grn_rset_recinfo *ri;
    grn_hash_cursor *c = grn_hash_cursor_open(ctx, s, NULL, 0, NULL, 0,
                                              0, -1, 0);
    if (c) {
      while ((eid = grn_hash_cursor_next(ctx, c))) {
        grn_hash_cursor_get_value(ctx, c, (void **) &ri);
        if ((ri->n_subrecs & GRN_RSET_UTIL_BIT)) {
          ri->n_subrecs &= ~GRN_RSET_UTIL_BIT;
        } else {
          grn_hash_delete_by_id(ctx, s, eid, NULL);
        }
      }
      grn_hash_cursor_close(ctx, c);
    }
  }
}

static grn_rc
selector_tag_search(grn_ctx *ctx, GNUC_UNUSED grn_obj *table, GNUC_UNUSED grn_obj *index,
                    GNUC_UNUSED int nargs, grn_obj **args,
                    grn_obj *res, GNUC_UNUSED grn_operator op)
{
  grn_obj *accessor;
  int n_args = nargs - 1;
  grn_obj **values;
  int n_values = n_args - 1;
  unsigned int i;
  grn_operator internal_op = GRN_OP_OR;
  grn_operator mode = GRN_OP_EXACT;
  unsigned int threshold = 1000;
  unsigned int n_hits;
  grn_rc rc = GRN_SUCCESS;

#define N_REQUIRED_ARGS 2

  if (n_args < N_REQUIRED_ARGS) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "tag_search(): wrong number of arguments (%d for 2..)",
                     n_args);
    return GRN_INVALID_ARGUMENT;
  }
  accessor = args[1];

  if (args[n_args]->header.type == GRN_TABLE_HASH_KEY) {
    grn_obj *options = args[n_args];
    grn_hash_cursor *cursor;
    void *key;
    grn_obj *value;
    unsigned int key_size;
    cursor = grn_hash_cursor_open(ctx, (grn_hash *)options,
                                  NULL, 0, NULL, 0,
                                  0, -1, 0);
    if (!cursor) {
      GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                       "tag_search(): couldn't open cursor");
      goto exit;
    }
    while (grn_hash_cursor_next(ctx, cursor) != GRN_ID_NIL) {
      grn_bool is_valid = GRN_TRUE;
      grn_hash_cursor_get_key_value(ctx, cursor, &key, &key_size,
                                    (void **)&value);

      if (key_size == 2 && !memcmp(key, "op", 2)) {
        if (GRN_TEXT_LEN(value) == 2 && !memcmp(GRN_TEXT_VALUE(value), "OR", 2)) {
          internal_op = GRN_OP_OR;
        } else if (GRN_TEXT_LEN(value) == 3 && !memcmp(GRN_TEXT_VALUE(value), "AND", 3)) {
          internal_op = GRN_OP_AND;
        } else if (GRN_TEXT_LEN(value) == 3 && !memcmp(GRN_TEXT_VALUE(value), "NOT", 3)) {
          internal_op = GRN_OP_AND_NOT;
        } else {
          is_valid = GRN_FALSE;
        }
      } else if (key_size == 4 && !memcmp(key, "mode", 4)) {
        if (GRN_TEXT_LEN(value) == 5 && !memcmp(GRN_TEXT_VALUE(value), "EXACT", 5)) {
          mode = GRN_OP_EXACT;
        } else if (GRN_TEXT_LEN(value) == 6 && !memcmp(GRN_TEXT_VALUE(value), "PREFIX", 6)) {
          mode = GRN_OP_PREFIX;
        } else {
          is_valid = GRN_FALSE;
        }
      } else if (key_size == 9 && !memcmp(key, "threshold", 9)) {
        threshold = GRN_INT32_VALUE(value);
      } else {
        is_valid = GRN_FALSE;
      }
      if (!is_valid) {
        GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                         "invalid option name: <%.*s>",
                         key_size, (char *)key);
        grn_hash_cursor_close(ctx, cursor);
        goto exit;
      }
    }
    grn_hash_cursor_close(ctx, cursor);
    n_values--;
  }

  if (n_values == 0) {
    return rc;
  }

/*
  TODO: should implement sequential search
  if (selector_tag_search_sequential_search(ctx, table, index,
                                            n_values, values,
                                            res, op)) {
    return ctx->rc;
  }
*/

  values = args + 2;

/*
  n_hits = grn_table_size(ctx, res);
  if (n_hits == 0) {
    grn_obj *v, *cond = NULL;

    GRN_EXPR_CREATE_FOR_QUERY(ctx, table, cond, v);
    grn_expr_parse(ctx, cond,
                   "all_records()",
                   strlen("all_records()"),
                   NULL,
                   GRN_OP_MATCH,
                   GRN_OP_AND,
                   GRN_EXPR_SYNTAX_SCRIPT);
    if (ctx->rc != GRN_SUCCESS) {
      goto exit;
    }
    grn_table_select(ctx, table, cond, res, GRN_OP_OR);
    if (ctx->rc != GRN_SUCCESS) {
      goto exit;
    }
    if (cond) {
      grn_obj_unlink(ctx, cond);
    }
  }
*/

  int original_flags = ctx->flags;
  if (internal_op == GRN_OP_OR) {
   ctx->flags |= GRN_CTX_TEMPORARY_DISABLE_II_RESOLVE_SEL_AND;
  } else {
    op = internal_op;
  }

  grn_obj buf;
  GRN_TEXT_INIT(&buf, 0);
  for (i = 0; i < n_values; i++) {
    grn_obj *value = values[i];
    grn_operator mode_ = mode;

    {
      const char *ch = GRN_TEXT_VALUE(values[i]);
      unsigned int value_length = 0;
      unsigned int char_length;
      unsigned int rest_length = GRN_TEXT_LEN(values[i]);
      grn_encoding encoding = ctx->encoding;

      while ((char_length = grn_plugin_charlen(ctx, ch, rest_length,
                                               encoding))) {
        if ((rest_length - char_length == 0) &&
            (char_length == 1 && !memcmp(ch, "*", 1))) {
          mode_ = GRN_OP_PREFIX;
          break;
        }
        ch += char_length;
        value_length += char_length;
        rest_length -= char_length;
      }
      if (value_length < GRN_TEXT_LEN(values[i])) {
        GRN_BULK_REWIND(&buf);
        GRN_TEXT_SET(ctx, &buf, GRN_TEXT_VALUE(values[i]), value_length);
        value = &buf;
      }
    }

    grn_search_optarg search_options;
    memset(&search_options, 0, sizeof(grn_search_optarg));
    search_options.mode = mode_;
    search_options.similarity_threshold = 0;
    search_options.max_interval = 0;
    search_options.weight_vector = NULL;
    search_options.vector_size = 0;
    search_options.proc = NULL;
    search_options.max_size = 0;
    search_options.scorer = NULL;
    if (i == n_values - 1 && internal_op == GRN_OP_OR) {
      ctx->flags = original_flags;
    }
    rc = grn_obj_search(ctx, index, value, res, op, &search_options);

    if (rc != GRN_SUCCESS) {
      break;
    }
  }
  GRN_OBJ_FIN(ctx, &buf);

exit :

#undef N_REQUIRED_ARGS
  
  return rc;
}

static grn_obj *
func_tag_search(grn_ctx *ctx, int nargs, grn_obj **args, grn_user_data *user_data)
{
  grn_obj *found;
  grn_obj *target_value;
  int i;

  found = grn_plugin_proc_alloc(ctx, user_data, GRN_DB_BOOL, 0);
  if (!found) {
    return NULL;
  }
  GRN_BOOL_SET(ctx, found, GRN_FALSE);

  if (nargs < 1) {
    ERR(GRN_INVALID_ARGUMENT,
        "tag_search(): wrong number of arguments (%d for 1..)", nargs);
    return found;
  }

  target_value = args[0];
  for (i = 1; i < nargs; i++) {
    grn_obj *value = args[i];
    grn_bool result;

    result = grn_operator_exec_equal(ctx, target_value, value);
    if (ctx->rc) {
      break;
    }

    if (result) {
      GRN_BOOL_SET(ctx, found, GRN_TRUE);
      break;
    }
  }

  return found;
}

grn_rc
GRN_PLUGIN_INIT(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}

grn_rc
GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  {
    grn_obj *selector_proc;

    selector_proc = grn_proc_create(ctx, "tag_search", -1, GRN_PROC_FUNCTION,
                                    func_tag_search, NULL, NULL, 0, NULL);
    grn_proc_set_selector(ctx, selector_proc, selector_tag_search);
    grn_proc_set_selector_operator(ctx, selector_proc, GRN_OP_MATCH);
  }

  return ctx->rc;
}

grn_rc
GRN_PLUGIN_FIN(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}
