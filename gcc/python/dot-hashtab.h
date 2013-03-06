/* This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>. */

#ifndef __DOT_HASHTAB_H__
#define __DOT_HASHTAB_H__

typedef unsigned long gpy_hashval_t;
typedef struct gpy_hash_entry {
  gpy_hashval_t hash;
  void * data;
} gpy_hash_entry_t ;

typedef struct GTY(()) gpy_hash_table_t {
  signed long size, length;
  gpy_hash_entry_t * array;
} gpy_hash_tab_t ;

typedef struct gpy_symbol_t {
  tree offset, field, type;
} gpy_symbol_t;

typedef gpy_hash_tab_t * dot_context;
DEF_VEC_P (dot_context);
DEF_VEC_ALLOC_P (dot_context, gc);
typedef VEC(dot_context,gc) * dot_context_t;

extern gpy_hashval_t gpy_dd_hash_string (const char *);
extern gpy_hash_entry_t * gpy_dd_hash_lookup_table (gpy_hash_tab_t *, gpy_hashval_t);

extern void ** gpy_dd_hash_insert (gpy_hashval_t, void *, gpy_hash_tab_t *);
extern void gpy_dd_hash_grow_table (gpy_hash_tab_t *);
extern void gpy_dd_hash_init_table (gpy_hash_tab_t *);

#endif /*__DOT_HASHTAB_H__*/
