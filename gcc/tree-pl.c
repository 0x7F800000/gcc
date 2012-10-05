#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tm_p.h"
#include "basic-block.h"
#include "flags.h"
#include "function.h"
#include "tree-inline.h"
#include "gimple.h"
#include "target.h"
#include "tree-iterator.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "hashtab.h"
#include "diagnostic.h"
#include "demangle.h"
#include "langhooks.h"
#include "ggc.h"
#include "cgraph.h"
#include "gimple.h"
#include "tree-pl.h"
#include "rtl.h"
#include "expr.h"
#include "tree-pretty-print.h"

typedef void (*assign_handler)(tree, tree, void *);

static unsigned int pl_execute (void);
static bool pl_gate (void);

static int pl_may_complete_phi_bounds (void **slot, void *res);
static bool pl_may_finish_incomplete_bounds (void);
static int pl_find_valid_phi_bounds (void **slot, void *res);
static int pl_recompute_phi_bounds (void **slot, void *res);
static int pl_mark_invalid_bounds_walker (void **slot, void *res);
static void pl_mark_completed_bounds (tree bounds);
static bool pl_completed_bounds (tree bounds);
static void pl_finish_incomplete_bounds (void);
static void pl_erase_completed_bounds (void);
static void pl_erase_incomplete_bounds (void);
static tree pl_get_tmp_var (void);
static void pl_fix_function_decl (tree decl, bool make_ssa_names);
static void pl_fix_function_decls (void);
static void pl_init (void);
static void pl_fini (void);
static tree pl_build_addr_expr (tree t);
static void pl_register_bounds (tree ptr, tree bnd);
static void pl_register_addr_bounds (tree ptr, tree bnd);
static tree pl_get_registered_addr_bounds (tree ptr);
static void pl_register_incomplete_bounds (tree bounds, tree ptr);
static bool pl_incomplete_bounds (tree bounds);
static basic_block pl_get_entry_block (void);
static tree pl_get_zero_bounds (void);
static tree pl_get_none_bounds (void);
static tree pl_get_invalid_op_bounds (void);
static tree pl_get_nonpointer_load_bounds (void);
static void pl_mark_invalid_bounds (tree bounds);
static bool pl_valid_bounds (tree bounds);
static void pl_transform_function (void);
static tree pl_get_bound_for_parm (tree parm);
static tree pl_force_gimple_call_op (tree op, gimple_seq *seq);
static tree pl_build_bndldx (tree addr, tree ptr, gimple_stmt_iterator *gsi);
static void pl_build_bndstx (tree addr, tree ptr, tree bounds,
			     gimple_stmt_iterator *gsi);
static tree pl_build_returned_bound (gimple call);
static tree pl_build_component_ref (tree obj, tree field);
static tree pl_build_array_ref (tree arr, tree etype, tree esize,
				unsigned HOST_WIDE_INT idx);
static void pl_copy_bounds_for_assign (tree lhs, tree rhs, void *arg);
static void pl_add_modification_to_statements_list (tree lhs, tree rhs, void *arg);
static void pl_walk_pointer_assignments (tree lhs, tree rhs, void *arg,
					 assign_handler handler);
static tree pl_compute_bounds_for_assignment (tree node, gimple assign);
static tree pl_make_bounds (tree lb, tree size, gimple_stmt_iterator *iter, bool after);
static tree pl_make_addressed_object_bounds (tree obj,
					     gimple_stmt_iterator *iter,
					     bool always_narrow_fields);
static tree pl_generate_extern_var_bounds (tree var);
static tree pl_get_bounds_for_decl (tree decl);
static tree pl_get_bounds_for_decl_addr (tree decl);
static tree pl_get_bounds_for_string_cst (tree cst);
static tree pl_get_bounds_by_definition (tree node, gimple def_stmt,
					 gimple_stmt_iterator *iter);
static tree pl_find_bounds_1 (tree ptr, tree ptr_src,
			      gimple_stmt_iterator *iter,
			      bool always_narrow_fields);
static tree pl_find_bounds (tree ptr, gimple_stmt_iterator *iter);
static tree pl_find_bounds_loaded (tree ptr, tree ptr_src,
				   gimple_stmt_iterator *iter);
static tree pl_find_bounds_narrowed (tree ptr, gimple_stmt_iterator *iter);
static void pl_check_mem_access (tree first, tree last, tree bounds,
				 gimple_stmt_iterator iter,
				 location_t location, tree dirflag);
static tree pl_intersect_bounds (tree bounds1, tree bounds2,
				 gimple_stmt_iterator *iter);
static bool pl_may_narrow_to_field (tree field);
static bool pl_narrow_bounds_for_field (tree field, bool always_narrow);
static tree pl_narrow_bounds_to_field (tree bounds, tree component,
				       gimple_stmt_iterator *iter);
static void pl_parse_array_and_component_ref (tree node, tree *ptr,
					      tree *elt, bool *safe,
					      bool *bitfield,
					      tree *bounds,
					      gimple_stmt_iterator *iter,
					      bool innermost_bounds,
					      bool always_narrow);
static void pl_add_bounds_to_call_stmt (gimple_stmt_iterator *gsi);
static void pl_add_bounds_to_ret_stmt (gimple_stmt_iterator *gsi);
static void pl_process_stmt (gimple_stmt_iterator *iter, tree node,
			     location_t loc, tree dirflag,
			     tree access_offs, tree access_size,
			     bool safe);
static void pl_mark_stmt (gimple s);
static bool pl_marked_stmt (gimple s);
static void pl_find_bound_slots (tree type, bool *have_bound,
				 HOST_WIDE_INT offs,
				 HOST_WIDE_INT ptr_size);

static GTY (()) tree pl_bndldx_fndecl;
static GTY (()) tree pl_bndstx_fndecl;
static GTY (()) tree pl_checkl_fndecl;
static GTY (()) tree pl_checku_fndecl;
static GTY (()) tree pl_bndmk_fndecl;
static GTY (()) tree pl_ret_bnd_fndecl;
static GTY (()) tree pl_intersect_fndecl;
static GTY (()) tree pl_user_intersect_fndecl;
static GTY (()) tree pl_bind_intersect_fndecl;
static GTY (()) tree pl_arg_bnd_fndecl;

static GTY (()) tree pl_bound_type;
static GTY (()) tree pl_uintptr_type;

static basic_block entry_block;
static tree zero_bounds;
static tree none_bounds;
static tree tmp_var;
static tree incomplete_bounds;

static GTY ((param_is (union tree_node))) htab_t pl_marked_stmts;
static GTY ((param_is (union tree_node))) htab_t pl_invalid_bounds;
static GTY ((param_is (union tree_node))) htab_t pl_completed_bounds_map;
static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map)))
     htab_t pl_reg_bounds;
static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map)))
     htab_t pl_reg_addr_bounds;
static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map)))
     htab_t pl_incomplete_bounds_map;

static const char *BOUND_TMP_NAME = "__bound_tmp";

static VEC(tree,heap) *var_inits = NULL;
const char *PLSI_IDENTIFIER = "__pl_initialize_static_bounds";

static void
pl_mark_stmt (gimple s)
{
  void **slot;

  slot = htab_find_slot (pl_marked_stmts, s, INSERT);
  *slot = s;
}

static bool
pl_marked_stmt (gimple s)
{
  return htab_find (pl_marked_stmts, s) != NULL;
}

static tree
pl_get_tmp_var (void)
{
  if (!tmp_var)
    {
      tmp_var = create_tmp_reg (pl_bound_type, BOUND_TMP_NAME);
      add_referenced_var (tmp_var);
    }

  return tmp_var;
}

void
pl_split_returned_reg (rtx return_reg, rtx *return_reg_val,
		       rtx *return_reg_bnd)
{
  int i;
  int val_num = 0;
  int bnd_num = 0;
  rtx *val_tmps;
  rtx *bnd_tmps;

  *return_reg_bnd = 0;

  if (!return_reg
      || GET_CODE (return_reg) != PARALLEL)
    {
      *return_reg_val = return_reg;
      return;
    }

  val_tmps = XALLOCAVEC (rtx, XVECLEN (return_reg, 0));
  bnd_tmps = XALLOCAVEC (rtx, XVECLEN (return_reg, 0));

  for (i = 0; i < XVECLEN (return_reg, 0); i++)
    {
      rtx reg = XEXP (XVECEXP (return_reg, 0, i), 0);

      if (!reg)
	continue;

      if (BOUND_MODE_P (GET_MODE (reg)) || CONST_INT_P (reg))
	bnd_tmps[bnd_num++] = XVECEXP (return_reg, 0, i);
      else
	val_tmps[val_num++] = XVECEXP (return_reg, 0, i);
    }

  gcc_assert (val_num);

  if (!bnd_num)
    {
      *return_reg_val = return_reg;
      return;
    }

  if (val_num == 1)
    {
      *return_reg_val = XEXP (val_tmps[0], 0);
      if (bnd_num == 1 && XEXP (bnd_tmps[0], 1) == const0_rtx)
	*return_reg_bnd = XEXP (bnd_tmps[0], 0);
      else
	*return_reg_bnd
	  = gen_rtx_PARALLEL (VOIDmode,
			      gen_rtvec_v (bnd_num, bnd_tmps));
    }
  else
    {
      gcc_assert  (bnd_num > 0);
      *return_reg_val = gen_rtx_PARALLEL (GET_MODE (return_reg),
					  gen_rtvec_v (val_num, val_tmps));
      *return_reg_bnd = gen_rtx_PARALLEL (VOIDmode,
					  gen_rtvec_v (bnd_num, bnd_tmps));
    }
}

rtx
pl_join_splitted_reg (rtx val, rtx bnd)
{
  rtx res;

  if (!bnd)
    return val;

  if (REG_P (bnd) || CONST_INT_P (bnd))
    {
      gcc_assert (GET_CODE (val) != PARALLEL);

      bnd = gen_rtx_EXPR_LIST (VOIDmode, bnd, const0_rtx);
      val = gen_rtx_EXPR_LIST (VOIDmode, val, const0_rtx);

      res = gen_rtx_PARALLEL (GET_MODE (val), rtvec_alloc (2));

      XVECEXP (res, 0, 0) = bnd;
      XVECEXP (res, 0, 1) = val;
    }
  else
    {
      bool valpar = (GET_CODE (val) == PARALLEL);
      int n, i;

      gcc_assert (GET_CODE (bnd) == PARALLEL);

      n = (valpar ? XVECLEN (val, 0) : 1) + XVECLEN (bnd, 0);
      res = gen_rtx_PARALLEL (GET_MODE (val), rtvec_alloc (n));

      n = 0;
      for (i = 0; i < XVECLEN (bnd, 0); i++)
	XVECEXP (res, 0, n++) = XVECEXP (bnd, 0, i);
      if (valpar)
	for (i = 0; i < XVECLEN (val, 0); i++)
	  XVECEXP (res, 0, n++) = XVECEXP (val, 0, i);
      else
	XVECEXP (res, 0, n++) = gen_rtx_EXPR_LIST (VOIDmode,
						   val,
						   const0_rtx);
    }

  return res;
}

tree
pl_make_bounds_for_struct_addr (tree ptr)
{
  tree type = TREE_TYPE (ptr);
  tree size;

  gcc_assert (POINTER_TYPE_P (type));

  size = TYPE_SIZE (TREE_TYPE (type));

  gcc_assert (size);

  return build_call_nary (pl_bound_type,
			  build_fold_addr_expr (pl_bndmk_fndecl),
			  2, ptr, size);
}

void
pl_emit_bounds_store (rtx bounds, rtx value, rtx mem)
{
  gcc_assert (MEM_P (mem));

  if (REG_P (bounds) || CONST_INT_P (bounds))
    {
      rtx ptr;

      if (REG_P (value))
	ptr = value;
      else
	{
	  rtx slot = adjust_address (value, Pmode, 0);
	  ptr = gen_reg_rtx (Pmode);
	  emit_move_insn (ptr, slot);
	}

      if (CONST_INT_P (bounds))
	bounds = targetm.calls.load_bounds_for_arg (value, ptr, bounds);

      targetm.calls.store_bounds_for_arg (ptr, mem,
					  bounds, NULL);
    }
  else
    {
      int i;

      gcc_assert (GET_CODE (bounds) == PARALLEL);
      gcc_assert (GET_CODE (value) == PARALLEL || MEM_P (value));

      for (i = 0; i < XVECLEN (bounds, 0); i++)
	{
	  rtx reg = XEXP (XVECEXP (bounds, 0, i), 0);
	  rtx offs = XEXP (XVECEXP (bounds, 0, i), 1);
	  rtx slot = adjust_address (mem, Pmode, INTVAL (offs));
	  rtx ptr;

	  if (GET_CODE (value) == PARALLEL)
	    ptr = pl_get_value_with_offs (value, offs);
	  else
	    {
	      rtx tmp = adjust_address (value, Pmode, INTVAL (offs));
	      ptr = gen_reg_rtx (Pmode);
	      emit_move_insn (ptr, tmp);
	    }

	  targetm.calls.store_bounds_for_arg (ptr, slot, reg, NULL);
	}
    }
}

static int
pl_may_complete_phi_bounds (void **slot, void *res)
{
  struct tree_map *map = (struct tree_map *)*slot;
  tree bounds = map->base.from;
  gimple phi;
  unsigned i;

  gcc_assert (TREE_CODE (bounds) == SSA_NAME);

  phi = SSA_NAME_DEF_STMT (bounds);

  gcc_assert (phi && gimple_code (phi) == GIMPLE_PHI);

  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      tree phi_arg = gimple_phi_arg_def (phi, i);
      if (!phi_arg)
	{
	  *((bool *)res) = false;
	  /* Do not need to traverse further.  */
	  return 0;
	}
    }

  return 1;
}

static bool
pl_may_finish_incomplete_bounds (void)
{
  bool res = true;

  htab_traverse (pl_incomplete_bounds_map,
		 pl_may_complete_phi_bounds,
		 &res);

  return res;
}

static int
pl_find_valid_phi_bounds (void **slot, void *res)
{
  struct tree_map *map = (struct tree_map *)*slot;
  tree bounds = map->base.from;
  gimple phi;
  unsigned i;

  gcc_assert (TREE_CODE (bounds) == SSA_NAME);

  if (pl_completed_bounds (bounds))
    return 1;

  phi = SSA_NAME_DEF_STMT (bounds);

  gcc_assert (phi && gimple_code (phi) == GIMPLE_PHI);

  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      tree phi_arg = gimple_phi_arg_def (phi, i);

      gcc_assert (phi_arg);

      if (pl_valid_bounds (phi_arg) && !pl_incomplete_bounds (phi_arg))
	{
	  *((bool *)res) = true;
	  pl_mark_completed_bounds (bounds);
	  pl_recompute_phi_bounds (slot, NULL);
	  return 1;
	}
    }

  return 1;
}

static int
pl_recompute_phi_bounds (void **slot, void *res ATTRIBUTE_UNUSED)
{
  struct tree_map *map = (struct tree_map *)*slot;
  tree bounds = map->base.from;
  tree ptr = map->to;
  gimple bounds_phi;
  gimple ptr_phi;
  unsigned i;

  gcc_assert (TREE_CODE (bounds) == SSA_NAME);
  gcc_assert (TREE_CODE (ptr) == SSA_NAME);

  bounds_phi = SSA_NAME_DEF_STMT (bounds);
  ptr_phi = SSA_NAME_DEF_STMT (ptr);

  gcc_assert (bounds_phi && gimple_code (bounds_phi) == GIMPLE_PHI);
  gcc_assert (ptr_phi && gimple_code (ptr_phi) == GIMPLE_PHI);

  for (i = 0; i < gimple_phi_num_args (bounds_phi); i++)
    {
      tree ptr_arg = gimple_phi_arg_def (ptr_phi, i);
      tree bound_arg = pl_find_bounds (ptr_arg, NULL);

      add_phi_arg (bounds_phi, bound_arg,
		   gimple_phi_arg_edge (ptr_phi, i),
		   UNKNOWN_LOCATION);
    }

  return 1;
}

static int
pl_mark_invalid_bounds_walker (void **slot, void *res ATTRIBUTE_UNUSED)
{
  struct tree_map *map = (struct tree_map *)*slot;
  tree bounds = map->base.from;

  if (!pl_completed_bounds (bounds))
    {
      pl_mark_invalid_bounds (bounds);
      pl_mark_completed_bounds (bounds);
    }
  return 1;
}

static void
pl_finish_incomplete_bounds (void)
{
  bool found_valid;

  while (found_valid)
    {
      found_valid = false;

      htab_traverse (pl_incomplete_bounds_map,
		     pl_find_valid_phi_bounds,
		     &found_valid);

      if (found_valid)
	htab_traverse (pl_incomplete_bounds_map,
		       pl_recompute_phi_bounds,
		       NULL);
    }

  htab_traverse (pl_incomplete_bounds_map,
		 pl_mark_invalid_bounds_walker,
		 NULL);
  htab_traverse (pl_incomplete_bounds_map,
		 pl_recompute_phi_bounds,
		 &found_valid);

  pl_erase_completed_bounds ();
  pl_erase_incomplete_bounds ();
}

bool
pl_type_has_pointer (tree type)
{
  bool res = false;

  if (BOUNDED_TYPE_P (type))
    res = true;
  else if (RECORD_OR_UNION_TYPE_P (type))
    {
      tree field;

      for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL)
	  res = res || pl_type_has_pointer (TREE_TYPE (field));
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    res = pl_type_has_pointer (TREE_TYPE (type));

  return res;
}

extern bool
pl_register_var_initializer (tree var)
{
  if (!flag_pl)
    return false;

  gcc_assert (TREE_CODE (var) == VAR_DECL);
  gcc_assert (DECL_INITIAL (var)
	      && DECL_INITIAL (var) != error_mark_node);

  if (TREE_STATIC (var)
      && pl_type_has_pointer (TREE_TYPE (var)))
    {
      VEC_safe_push (tree, heap, var_inits, var);
      return true;
    }

  return false;
}

static void
pl_add_modification_to_statements_list (tree lhs,
					tree rhs,
					void *arg)
{
  tree *stmts = (tree *)arg;
  tree modify;

  if (!useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (rhs)))
    rhs = build1 (CONVERT_EXPR, TREE_TYPE (lhs), rhs);

  modify = build2 (MODIFY_EXPR, TREE_TYPE (lhs), lhs, rhs);
  append_to_statement_list (modify, stmts);
}

void
pl_finish_file (void)
{
  tree stmts = NULL_TREE;
  int i;
  tree var;

  if (!var_inits)
    return;

  if (seen_error ())
    return;

  FOR_EACH_VEC_ELT (tree, var_inits, i, var)
    /* !!! We must check that var is actually emitted and we need
       and may initialize its bounds.  Currently asm_written flag and
       rtl are checked.  Probably some other fields should be checked.  */
    if (DECL_RTL (var) && MEM_P (DECL_RTL (var)) && TREE_ASM_WRITTEN (var))
      pl_walk_pointer_assignments (var, DECL_INITIAL (var), &stmts,
				   pl_add_modification_to_statements_list);

  if (stmts)
    cgraph_build_static_cdtor ('P', stmts, MAX_RESERVED_INIT_PRIORITY-1);

  VEC_free (tree, heap, var_inits);
  var_inits = NULL;
}

static void
pl_transform_function (void)
{
  basic_block bb, next;
  gimple_stmt_iterator i;
  int saved_last_basic_block = last_basic_block;
  enum gimple_rhs_class grhs_class;
  bool safe = DECL_PL_STATIC_INIT (cfun->decl);

  bb = ENTRY_BLOCK_PTR ->next_bb;
  do
    {
      next = bb->next_bb;
      for (i = gsi_start_bb (bb); !gsi_end_p (i); )
        {
          gimple s = gsi_stmt (i);

	  if (pl_marked_stmt (s))
	    {
	      gsi_next (&i);
	      continue;
	    }

          switch (gimple_code (s))
            {
            case GIMPLE_ASSIGN:
	      pl_process_stmt (&i, gimple_assign_lhs (s),
			       gimple_location (s), integer_one_node,
			       NULL_TREE, NULL_TREE, safe);
	      pl_process_stmt (&i, gimple_assign_rhs1 (s),
			       gimple_location (s), integer_zero_node,
			       NULL_TREE, NULL_TREE, safe);
	      grhs_class = get_gimple_rhs_class (gimple_assign_rhs_code (s));
	      if (grhs_class == GIMPLE_BINARY_RHS)
		pl_process_stmt (&i, gimple_assign_rhs2 (s),
				 gimple_location (s), integer_zero_node,
				 NULL_TREE, NULL_TREE, safe);
              break;

            case GIMPLE_RETURN:
              if (gimple_return_retval (s) != NULL_TREE)
                {
                  pl_process_stmt (&i, gimple_return_retval (s),
				   gimple_location (s),
				   integer_zero_node,
				   NULL_TREE, NULL_TREE, safe);

		  /* Additionall we need to add bounds
		     to return statement.  */
		  pl_add_bounds_to_ret_stmt (&i);
                }
              break;

	    case GIMPLE_CALL:
	      pl_add_bounds_to_call_stmt (&i);
	      break;

            default:
              ;
            }

	  gsi_next (&i);

	  /* We do not need any statements in PL static initializer except
	     created in PL pass.  */
	  if (DECL_PL_STATIC_INIT (cfun->decl)
	      && gimple_code (s) == GIMPLE_ASSIGN)
	    {
	      gimple_stmt_iterator del_iter = gsi_for_stmt (s);
	      gsi_remove (&del_iter, true);
	      unlink_stmt_vdef (s);
	      release_defs(s);
	    }
        }
      bb = next;
    }
  while (bb && bb->index <= saved_last_basic_block);
}

/* Add bound retvals to return statement pointed by GSI.  */

static void
pl_add_bounds_to_ret_stmt (gimple_stmt_iterator *gsi)
{
  gimple ret = gsi_stmt (*gsi);
  tree retval = gimple_return_retval (ret);
  tree ret_decl = DECL_RESULT (cfun->decl);
  tree bounds;

  if (!retval)
    return;

  if (BOUNDED_P (ret_decl))
    {
      bounds = pl_find_bounds (retval, gsi);
      pl_register_bounds (ret_decl, bounds);
      gimple_return_set_retval2 (ret, bounds);
    }

  /* TODO: Add support for structures which may
     be returned on registers.  */

  update_stmt (ret);
}

/* Add bound arguments to call statement pointed by GSI.  */

static void
pl_add_bounds_to_call_stmt (gimple_stmt_iterator *gsi)
{
  gimple call = gsi_stmt (*gsi);
  unsigned arg_no = 0;
  unsigned new_arg_no = 0;
  unsigned bnd_arg_cnt = 0;
  unsigned arg_cnt = 0;
  tree fndecl = gimple_call_fndecl (call);
  tree fntype = TREE_TYPE (TREE_TYPE (gimple_call_fn (call)));
  tree first_formal_arg;
  tree arg;
  gimple new_call;
  ssa_op_iter iter;
  tree op;

  /* Do nothing if back-end builtin is called.  */
  if (fndecl && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_MD
      && fndecl != pl_user_intersect_fndecl)
    return;

  first_formal_arg = TYPE_ARG_TYPES (fntype);

  /* Get number of arguments and bound arguments.  */
  for (arg = first_formal_arg;
       arg && TREE_VALUE (arg) != void_type_node
	 && (arg_cnt - bnd_arg_cnt)< gimple_call_num_args (call);
       arg = TREE_CHAIN (arg))
    {
      if (BOUNDED_TYPE_P (TREE_VALUE (arg)))
	{
	  bnd_arg_cnt++;
	  arg_cnt++;
	}

      arg_cnt++;
    }

  if (fndecl == pl_user_intersect_fndecl)
    {
      fndecl = pl_bind_intersect_fndecl;
      arg_cnt = 4;
      bnd_arg_cnt = 1;
    }

  /* Now add number of additional bound arguments for
     stdarg functions.  */
  for (arg_no = arg_cnt - bnd_arg_cnt;
       arg_no < gimple_call_num_args (call);
       arg_no++)
    {
      if (BOUNDED_P (gimple_call_arg (call, arg_no)))
	{
	  arg_cnt++;
	  bnd_arg_cnt++;
	}
      arg_cnt++;
    }

  new_call = gimple_alloc (GIMPLE_CALL, arg_cnt + 3);
  memcpy (new_call, call, sizeof (struct gimple_statement_call));
  gimple_set_num_ops (new_call, arg_cnt + 3);
  gimple_set_op (new_call, 0, gimple_op (call, 0));
  if (fndecl == pl_bind_intersect_fndecl)
    {
      gimple_set_op (new_call, 1, pl_build_addr_expr (fndecl));
      gimple_call_set_fntype (new_call, TREE_TYPE (fndecl));
    }
  else
    gimple_set_op (new_call, 1, gimple_op (call, 1));
  gimple_set_op (new_call, 2, gimple_op (call, 2));

  arg_no = 0;
  for (arg = first_formal_arg;
       arg && TREE_VALUE (arg) != void_type_node
	 && arg_no < gimple_call_num_args (call);
       arg = TREE_CHAIN (arg))
    {
      tree call_arg = gimple_call_arg (call, arg_no++);
      gimple_call_set_arg (new_call, new_arg_no++, call_arg);

      if (BOUNDED_TYPE_P (TREE_VALUE (arg)) && bnd_arg_cnt)
	{
	  tree bounds = pl_find_bounds (call_arg, gsi);
	  gimple_call_set_arg (new_call, new_arg_no++, bounds);
	  bnd_arg_cnt--;
	}
    }

  for ( ; arg_no < gimple_call_num_args (call); arg_no++)
    {
      tree call_arg = gimple_call_arg (call, arg_no);
      gimple_call_set_arg (new_call, new_arg_no++, call_arg);
      if (BOUNDED_P (call_arg) && bnd_arg_cnt)
	{
	  tree bounds = pl_find_bounds (call_arg, gsi);
	  gimple_call_set_arg (new_call, new_arg_no++, bounds);
	  bnd_arg_cnt--;
	}
    }

  FOR_EACH_SSA_TREE_OPERAND (op, call, iter, SSA_OP_ALL_DEFS)
    {
      SSA_NAME_DEF_STMT (op) = new_call;
    }

  gsi_replace (gsi, new_call, true);
}

static void
pl_check_mem_access (tree first, tree last, tree bounds,
		     gimple_stmt_iterator iter,
		     location_t location, tree dirflag)
{
  gimple_seq seq;
  gimple checkl, checku;
  tree node;

  if (bounds == pl_get_zero_bounds ())
    return;

  seq = gimple_seq_alloc ();

  node = pl_force_gimple_call_op (first, &seq);

  checkl = gimple_build_call (pl_checkl_fndecl, 2, bounds, node);
  pl_mark_stmt (checkl);
  gimple_seq_add_stmt (&seq, checkl);

  node = pl_force_gimple_call_op (last, &seq);

  checku = gimple_build_call (pl_checku_fndecl, 2, bounds, node);
  pl_mark_stmt (checku);
  gimple_seq_add_stmt (&seq, checku);

  gsi_insert_seq_before (&iter, seq, GSI_SAME_STMT);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      gimple before = gsi_stmt (iter);
      fprintf (dump_file, "Generated bound checks for statement ");
      print_gimple_stmt (dump_file, before, 0, TDF_VOPS|TDF_MEMSYMS);
      fprintf (dump_file, "  ");
      print_gimple_stmt (dump_file, checkl, 0, TDF_VOPS|TDF_MEMSYMS);
      fprintf (dump_file, "  ");
      print_gimple_stmt (dump_file, checku, 0, TDF_VOPS|TDF_MEMSYMS);
    }
}

static tree
pl_build_addr_expr (tree t)
{
  return TREE_CODE (t) == TARGET_MEM_REF
    ? tree_mem_ref_addr (ptr_type_node, t)
    : build_fold_addr_expr (t);
}

static void
pl_register_bounds (tree ptr, tree bnd)
{
  struct tree_map **slot, *map;

  if (bnd == incomplete_bounds)
    return;

  map = ggc_alloc_tree_map ();
  map->hash = htab_hash_pointer (ptr);
  map->base.from = ptr;
  map->to = bnd;

  slot = (struct tree_map **)
    htab_find_slot_with_hash (pl_reg_bounds, map, map->hash, INSERT);
  *slot = map;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Regsitered bound ");
      print_generic_expr (dump_file, bnd, 0);
      fprintf (dump_file, " for pointer ");
      print_generic_expr (dump_file, ptr, 0);
      fprintf (dump_file, "\n");
    }
}

tree
pl_get_registered_bounds (tree ptr)
{
  struct tree_map *res, in;
  in.base.from = ptr;
  in.hash = htab_hash_pointer (ptr);

  res = (struct tree_map *) htab_find_with_hash (pl_reg_bounds,
						 &in, in.hash);

  return res ? res->to : NULL_TREE;
}

tree
pl_get_arg_bounds (tree arg)
{
  tree bounds = pl_find_bounds (arg, NULL);

  if (!bounds)
    {
      if (arg == integer_zero_node)
	bounds = pl_get_none_bounds ();
      else
	bounds = pl_get_invalid_op_bounds ();
    }

  return bounds;
}

rtx
pl_get_value_with_offs (rtx par, rtx offs)
{
  int n;

  gcc_assert (GET_CODE (par) == PARALLEL);

  for (n = 0; n < XVECLEN (par, 0); n++)
    {
      rtx par_offs = XEXP (XVECEXP (par, 0, n), 1);
      if (INTVAL (offs) == INTVAL (par_offs))
	return XEXP (XVECEXP (par, 0, n), 0);
    }

  return NULL;
}

static void
pl_find_bound_slots (tree type, bool *have_bound,
		     HOST_WIDE_INT offs,
		     HOST_WIDE_INT ptr_size)
{
  if (BOUNDED_TYPE_P (type))
    have_bound[offs / ptr_size] = true;
  else if (RECORD_OR_UNION_TYPE_P (type))
    {
      tree field;

      for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL)
	  {
	    HOST_WIDE_INT field_offs
	      = TREE_INT_CST_LOW (DECL_FIELD_BIT_OFFSET (field));
	    if (DECL_FIELD_OFFSET (field))
	      field_offs += TREE_INT_CST_LOW (DECL_FIELD_OFFSET (field)) * 8;
	    pl_find_bound_slots (TREE_TYPE (field), have_bound,
				 offs + field_offs, ptr_size);
	  }
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree maxval = TYPE_MAX_VALUE (TYPE_DOMAIN (type));
      tree etype = TREE_TYPE (type);
      HOST_WIDE_INT esize = TREE_INT_CST_LOW (TYPE_SIZE (etype));
      HOST_WIDE_INT cur;

      for (cur = 0; cur <= TREE_INT_CST_LOW (maxval); cur++)
	pl_find_bound_slots (etype, have_bound, offs + cur * esize, ptr_size);
    }
}

void
pl_copy_bounds_for_stack_parm (rtx slot, rtx value, tree type)
{
  HOST_WIDE_INT ptr_size = TREE_INT_CST_LOW (TYPE_SIZE (pl_uintptr_type));
  HOST_WIDE_INT max_bounds = TREE_INT_CST_LOW (TYPE_SIZE (type)) / ptr_size;
  bool *have_bound = (bool *)xmalloc (sizeof (bool) * max_bounds);
  HOST_WIDE_INT i;
  rtx tmp = NULL, bnd;

  memset (have_bound, 0, sizeof (bool) * max_bounds);

  gcc_assert (TYPE_SIZE (type));
  gcc_assert (MEM_P (value));
  gcc_assert (MEM_P (slot));
  gcc_assert (RECORD_OR_UNION_TYPE_P (type));

  pl_find_bound_slots (type, have_bound, 0, ptr_size);

  for (i = 0; i < max_bounds; i++)
    if (have_bound[i])
      {
	rtx ptr = adjust_address (value, Pmode, i * ptr_size / 8);
	rtx to = adjust_address (slot, Pmode, i * ptr_size / 8);

	if (!tmp)
	  tmp = gen_reg_rtx (Pmode);

	emit_move_insn (tmp, ptr);
	bnd = targetm.calls.load_bounds_for_arg (ptr, tmp, NULL);
	targetm.calls.store_bounds_for_arg (tmp, to, bnd, NULL);
      }

  free (have_bound);
}

static void
pl_register_addr_bounds (tree ptr, tree bnd)
{
  struct tree_map **slot, *map;

  if (bnd == incomplete_bounds)
    return;

  map = ggc_alloc_tree_map ();
  map->hash = htab_hash_pointer (ptr);
  map->base.from = ptr;
  map->to = bnd;

  slot = (struct tree_map **)
    htab_find_slot_with_hash (pl_reg_addr_bounds, map, map->hash, INSERT);
  *slot = map;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Regsitered bound ");
      print_generic_expr (dump_file, bnd, 0);
      fprintf (dump_file, " for address of ");
      print_generic_expr (dump_file, ptr, 0);
      fprintf (dump_file, "\n");
    }
}

static tree
pl_get_registered_addr_bounds (tree ptr)
{
  struct tree_map *res, in;
  in.base.from = ptr;
  in.hash = htab_hash_pointer (ptr);

  res = (struct tree_map *) htab_find_with_hash (pl_reg_addr_bounds,
						 &in, in.hash);

  return res ? res->to : NULL_TREE;
}

static void
pl_register_incomplete_bounds (tree bounds, tree ptr)
{
  struct tree_map **slot, *map;

  map = ggc_alloc_tree_map ();
  map->hash = htab_hash_pointer (bounds);
  map->base.from = bounds;
  map->to = ptr;

  slot = (struct tree_map **)
    htab_find_slot_with_hash (pl_incomplete_bounds_map, map, map->hash, INSERT);
  *slot = map;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Regsitered incomplete bounds ");
      print_generic_expr (dump_file, bounds, 0);
      fprintf (dump_file, " for ");
      print_generic_expr (dump_file, ptr, 0);
      fprintf (dump_file, "\n");
    }
}

static bool
pl_incomplete_bounds (tree bounds)
{
  struct tree_map *res, in;

  if (bounds == incomplete_bounds)
    return true;

  if (pl_completed_bounds (bounds))
    return false;

  in.base.from = bounds;
  in.hash = htab_hash_pointer (bounds);

  res = (struct tree_map *) htab_find_with_hash (pl_incomplete_bounds_map,
						 &in, in.hash);

  return res != NULL;
}

static basic_block
pl_get_entry_block (void)
{
  if (!entry_block)
    {
      basic_block prev_entry = ENTRY_BLOCK_PTR->next_bb;
      edge e = find_edge (ENTRY_BLOCK_PTR, prev_entry);
      entry_block = split_edge (e);
    }

  return entry_block;
}

static tree
pl_get_zero_bounds (void)
{
  if (zero_bounds)
    return zero_bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Creating zero bounds...");

  zero_bounds = pl_make_bounds (integer_zero_node,
				integer_zero_node,
				NULL,
				false);

  return zero_bounds;
}

static tree
pl_get_none_bounds (void)
{
  if (none_bounds)
    return none_bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Creating none bounds...");

  none_bounds = pl_make_bounds (integer_minus_one_node,
				build_int_cst (size_type_node, 2),
				NULL,
				false);

  return none_bounds;
}

static tree
pl_get_invalid_op_bounds (void)
{
  return pl_get_zero_bounds ();
}

static tree
pl_get_nonpointer_load_bounds (void)
{
  return pl_get_zero_bounds ();
}

static tree
pl_build_returned_bound (gimple call)
{
  gimple_stmt_iterator gsi;
  tree bounds;
  gimple stmt;
  tree fndecl = gimple_call_fndecl (call);

  /* Currently we handle alloca separately.  May also fix
     alloca expanding to obtain bounds on b0.  */
  if (fndecl
      && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL
      && (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_ALLOCA
	  || DECL_FUNCTION_CODE (fndecl) == BUILT_IN_ALLOCA_WITH_ALIGN))
    {
      tree size = gimple_call_arg (call, 0);
      tree lb = gimple_call_lhs (call);
      gimple_stmt_iterator iter = gsi_for_stmt (call);
      bounds = pl_make_bounds (lb, size, &iter, true);
    }
  else if (fndecl
      && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL
      && DECL_FUNCTION_CODE (fndecl) == BUILT_IN_NEXT_ARG)
    {
      tree size = targetm.fn_abi_va_list_bounds_size (cfun->decl);
      if (size == integer_zero_node)
	bounds = pl_get_zero_bounds ();
      else
	{
	  tree lb = gimple_call_lhs (call);
	  gimple_stmt_iterator iter = gsi_for_stmt (call);
	  bounds = pl_make_bounds (lb, size, &iter, true);
	}
    }
  else
    {
      stmt = gimple_build_call (pl_ret_bnd_fndecl, 0);
      pl_mark_stmt (stmt);

      /* If call may throw then we have to insert new
	 statement on the fallthru edge.  Otherwise insert
	 it right after call.  */
      if (stmt_can_throw_internal (call))
	{
	  basic_block bb = gimple_bb (call);

	  gcc_assert (EDGE_COUNT (bb->succs) == 2);

	  gsi_insert_on_edge (FALLTHRU_EDGE (bb), stmt);
	  gsi_commit_edge_inserts ();
	}
      else
	{
	  gsi = gsi_for_stmt (call);
	  gsi_insert_after (&gsi, stmt, GSI_SAME_STMT);
	}

      bounds = make_ssa_name (pl_get_tmp_var (), stmt);
      gimple_call_set_lhs (stmt, bounds);

      update_stmt (stmt);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Built returned bounds (");
      print_generic_expr (dump_file, bounds, 0);
      fprintf (dump_file, ") for call: ");
      print_gimple_stmt (dump_file, call, 0, TDF_VOPS|TDF_MEMSYMS);
    }

  pl_register_bounds (gimple_call_lhs (call), bounds);

  return bounds;
}

static tree
pl_get_bound_for_parm (tree parm)
{
  tree decl = SSA_NAME_VAR (parm);
  tree bounds;

  bounds = pl_get_registered_bounds (parm);

  if (!bounds)
    bounds = pl_get_registered_bounds (decl);

  /* NULL bounds mean parm is not a pointer and
     zero bounds should be returned.  */
  if (!bounds)
    {
      /* For static chain param we return zero bounds
	 because currently we do not check dereferences
	 of this pointer.  */
      /* !!! FIXME: there is probably a more correct way to
	 identify such parm.  */
      if (cfun->decl && DECL_STATIC_CHAIN (cfun->decl)
	  && DECL_ARTIFICIAL (decl))
	bounds = pl_get_zero_bounds ();
      else if (BOUNDED_P (parm))
	{
	    gimple_stmt_iterator gsi;
	    gimple stmt;

	    stmt = gimple_build_call (pl_arg_bnd_fndecl, 1, parm);
	    pl_mark_stmt (stmt);

	    gsi = gsi_start_bb (pl_get_entry_block ());
	    gsi_insert_before (&gsi, stmt, GSI_SAME_STMT);

	    bounds = make_ssa_name (pl_get_tmp_var (), stmt);
	    gimple_call_set_lhs (stmt, bounds);

	    update_stmt (stmt);
	    pl_register_bounds (decl, bounds);

	    if (dump_file && (dump_flags & TDF_DETAILS))
	      {
		fprintf (dump_file, "Built arg bounds (");
		print_generic_expr (dump_file, bounds, 0);
		fprintf (dump_file, ") for arg: ");
		print_node (dump_file, "", decl, 0);
	      }
	}
      else
	bounds = pl_get_zero_bounds ();
    }

  if (!pl_get_registered_bounds (parm))
    pl_register_bounds (parm, bounds);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Using bounds ");
      print_generic_expr (dump_file, bounds, 0);
      fprintf (dump_file, " for parm ");
      print_generic_expr (dump_file, parm, 0);
      fprintf (dump_file, " of type ");
      print_generic_expr (dump_file, TREE_TYPE (parm), 0);
      fprintf (dump_file, ".\n");
    }

  return bounds;
}

static tree
pl_force_gimple_call_op (tree op, gimple_seq *seq)
{
  gimple_seq stmts;
  gimple_seq_node node;

  op = force_gimple_operand (unshare_expr (op), &stmts, true, NULL_TREE);

  for (node = gimple_seq_first (stmts); node; node = node->next)
    pl_mark_stmt (node->stmt);

  gimple_seq_add_seq (seq, stmts);

  return op;
}

static tree
pl_build_bndldx (tree addr, tree ptr, gimple_stmt_iterator *gsi)
{
  gimple_seq seq;
  gimple stmt;
  tree bounds;

  seq = gimple_seq_alloc ();

  addr = pl_force_gimple_call_op (addr, &seq);
  ptr = pl_force_gimple_call_op (ptr, &seq);

  stmt = gimple_build_call (pl_bndldx_fndecl, 2, addr, ptr);
  pl_mark_stmt (stmt);
  bounds = make_ssa_name (pl_get_tmp_var (), stmt);
  gimple_call_set_lhs (stmt, bounds);

  gimple_seq_add_stmt (&seq, stmt);

  gsi_insert_seq_after (gsi, seq, GSI_CONTINUE_LINKING);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Generated bndldx for pointer ");
      print_generic_expr (dump_file, ptr, 0);
      fprintf (dump_file, ": ");
      print_gimple_stmt (dump_file, stmt, 0, TDF_VOPS|TDF_MEMSYMS);
    }

  return bounds;
}

static void
pl_build_bndstx (tree addr, tree ptr, tree bounds,
		 gimple_stmt_iterator *gsi)
{
  gimple_seq seq;
  gimple stmt;

  /*
  if (bounds == pl_get_zero_bounds ())
    return;
  */

  seq = gimple_seq_alloc ();

  addr = pl_force_gimple_call_op (addr, &seq);
  ptr = pl_force_gimple_call_op (ptr, &seq);

  stmt = gimple_build_call (pl_bndstx_fndecl, 3, addr, ptr, bounds);
  pl_mark_stmt (stmt);

  gimple_seq_add_stmt (&seq, stmt);

  gsi_insert_seq_after (gsi, seq, GSI_CONTINUE_LINKING);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Generated bndstx for pointer store ");
      print_gimple_stmt (dump_file, gsi_stmt (*gsi), 0, TDF_VOPS|TDF_MEMSYMS);
      print_gimple_stmt (dump_file, stmt, 2, TDF_VOPS|TDF_MEMSYMS);
    }
}

static void
pl_mark_invalid_bounds (tree bounds)
{
  void **slot;

  slot = htab_find_slot (pl_invalid_bounds, bounds, INSERT);
  *slot = bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Marked bounds ");
      print_generic_expr (dump_file, bounds, 0);
      fprintf (dump_file, " as invalid\n");
    }
}

static bool
pl_valid_bounds (tree bounds)
{
  if (bounds == zero_bounds || bounds == none_bounds)
    return false;

  if (htab_find (pl_invalid_bounds, bounds) != NULL)
    return false;

  return true;
}

static void
pl_mark_completed_bounds (tree bounds)
{
  void **slot;

  slot = htab_find_slot (pl_completed_bounds_map, bounds, INSERT);
  *slot = bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Marked bounds ");
      print_generic_expr (dump_file, bounds, 0);
      fprintf (dump_file, " as completed\n");
    }
}

static bool
pl_completed_bounds (tree bounds)
{
  return htab_find (pl_completed_bounds_map, bounds) != NULL;
}

static void
pl_erase_completed_bounds (void)
{
  htab_empty (pl_completed_bounds_map);
}

static void
pl_erase_incomplete_bounds (void)
{
  htab_empty (pl_incomplete_bounds_map);
}

static tree
pl_compute_bounds_for_assignment (tree node, gimple assign)
{
  enum tree_code rhs_code = gimple_assign_rhs_code (assign);
  tree rhs1 = gimple_assign_rhs1 (assign);
  tree bounds = NULL_TREE;
  gimple_stmt_iterator iter = gsi_for_stmt (assign);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Computing bounds for assignment: ");
      print_gimple_stmt (dump_file, assign, 0, TDF_VOPS|TDF_MEMSYMS);
    }

  switch (rhs_code)
    {
    case MEM_REF:
    case ARRAY_REF:
    case COMPONENT_REF:
    case TARGET_MEM_REF:
      bounds = pl_find_bounds_loaded (node, rhs1, &iter);
      break;

    case VAR_DECL:
    case SSA_NAME:
    case ADDR_EXPR:
    case POINTER_PLUS_EXPR:
    case NOP_EXPR:
    case CONVERT_EXPR:
    case INTEGER_CST:
      bounds = pl_find_bounds (rhs1, &iter);
      break;

    case PARM_DECL:
      gcc_assert (TREE_ADDRESSABLE (rhs1));
      bounds = pl_build_bndldx (pl_build_addr_expr (rhs1),
				node, &iter);
      break;

    case MINUS_EXPR:
    case PLUS_EXPR:
    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
      {
	tree rhs2 = gimple_assign_rhs2 (assign);
	tree bnd1 = pl_find_bounds (rhs1, &iter);
	tree bnd2 = pl_find_bounds (rhs2, &iter);

	/* First we try to check types of operands.  If it
	   does not help then look at bound values.  */
	if (BOUNDED_P (rhs1)
	    && !BOUNDED_P (rhs2))
	  bounds = bnd1;
	else if (BOUNDED_P (rhs2)
		 && !BOUNDED_P (rhs1)
		 && rhs_code != MINUS_EXPR)
	  bounds = bnd2;
	else if (pl_incomplete_bounds (bnd1))
	  if (pl_valid_bounds (bnd2) && rhs_code != MINUS_EXPR
	      && !pl_incomplete_bounds (bnd2))
	    bounds = bnd2;
	  else
	    bounds = incomplete_bounds;
	else if (pl_incomplete_bounds (bnd2))
	  if (pl_valid_bounds (bnd1)
	      && !pl_incomplete_bounds (bnd1))
	    bounds = bnd1;
	  else
	    bounds = incomplete_bounds;
	else if (!pl_valid_bounds (bnd1))
	  if (pl_valid_bounds (bnd2) && rhs_code != MINUS_EXPR)
	    bounds = bnd2;
	  else if (bnd2 == pl_get_zero_bounds ())
	    bounds = bnd2;
	  else
	    bounds = bnd1;
	else if (!pl_valid_bounds (bnd2))
	  bounds = bnd1;
	else
	  /* Seems both operands may have valid bounds.
	     In such case use default invalid op bounds.  */
	  bounds = pl_get_invalid_op_bounds ();
      }
      break;

    case BIT_NOT_EXPR:
    case NEGATE_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case MULT_EXPR:
    case RDIV_EXPR:
    case TRUNC_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case EXACT_DIV_EXPR:
      bounds = pl_get_invalid_op_bounds ();
      break;

    case COND_EXPR:
      {
	tree val1 = gimple_assign_rhs2 (assign);
	tree val2 = gimple_assign_rhs3 (assign);
	tree bnd1 = pl_find_bounds (val1, &iter);
	tree bnd2 = pl_find_bounds (val2, &iter);
	gimple stmt;

	if (pl_incomplete_bounds (bnd1) || pl_incomplete_bounds (bnd2))
	  bounds = incomplete_bounds;
	else if (bnd1 == bnd2)
	  bounds = bnd1;
	else
	  {
	    bounds = make_ssa_name (pl_get_tmp_var (), assign);
	    stmt = gimple_build_assign_with_ops3 (COND_EXPR, bounds,
						  rhs1, bnd1, bnd2);
	    gsi_insert_after (&iter, stmt, GSI_SAME_STMT);

	    if (!pl_valid_bounds (bnd1) && !pl_valid_bounds (bnd2))
	      pl_mark_invalid_bounds (bounds);
	  }
      }
      break;

    case MAX_EXPR:
    case MIN_EXPR:
      {
	tree rhs2 = gimple_assign_rhs2 (assign);
	tree bnd1 = pl_find_bounds (rhs1, &iter);
	tree bnd2 = pl_find_bounds (rhs2, &iter);

	if (pl_incomplete_bounds (bnd1) || pl_incomplete_bounds (bnd2))
	  bounds = incomplete_bounds;
	else if (bnd1 == bnd2)
	  bounds = bnd1;
	else
	  {
	    gimple stmt;
	    tree cond = build2 (rhs_code == MAX_EXPR ? GT_EXPR : LT_EXPR,
				boolean_type_node, rhs1, rhs2);
	    bounds = make_ssa_name (pl_get_tmp_var (), assign);
	    stmt = gimple_build_assign_with_ops3 (COND_EXPR, bounds,
						  cond, bnd1, bnd2);

	    gsi_insert_after (&iter, stmt, GSI_SAME_STMT);

	    if (!pl_valid_bounds (bnd1) && !pl_valid_bounds (bnd2))
	      pl_mark_invalid_bounds (bounds);
	  }
      }
      break;

    case CONSTRUCTOR:
      /*
	    {
	      unsigned int idx;
	      tree purp, val;
	      FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (rhs1), idx, purp, val)
	      {
	      }
	    }
      */
      /* fall thru */

    default:
      internal_error ("pl_compute_bounds_for_assignment: Unexpected RHS code %s",
		      tree_code_name[rhs_code]);
    }

  gcc_assert (bounds);

  if (node)
    pl_register_bounds (node, bounds);

  return bounds;
}

static tree
pl_get_bounds_by_definition (tree node, gimple def_stmt, gimple_stmt_iterator *iter)
{
  tree var, bounds;
  enum gimple_code code = gimple_code (def_stmt);
  gimple stmt;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Searching for bounds for node: ");
      print_generic_expr (dump_file, node, 0);

      fprintf (dump_file, " using its definition: ");
      print_gimple_stmt (dump_file, def_stmt, 0, TDF_VOPS|TDF_MEMSYMS);
    }

  switch (code)
    {
    case GIMPLE_NOP:
      var = SSA_NAME_VAR (node);
      switch (TREE_CODE (var))
	{
	case PARM_DECL:
	  bounds = pl_get_bound_for_parm (node);
	  /*bounds = pl_find_bounds (var, iter);
	    pl_register_bounds (node, bounds);*/
	  break;

	case VAR_DECL:
	  /* For uninitialized pointers use none bounds.  */
	  bounds = pl_get_none_bounds ();
	  pl_register_bounds (node, bounds);
	  break;

	case RESULT_DECL:
	  gcc_assert (TREE_CODE (TREE_TYPE (node)) == REFERENCE_TYPE);
	  bounds = pl_make_bounds (node, DECL_SIZE (var), NULL, false);
	  pl_register_bounds (node, bounds);
	  break;

	default:
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Unexpected var with no definition\n");
	      print_generic_expr (dump_file, var, 0);
	    }
	  internal_error ("pl_get_bounds_by_definition: Unexpected var of type %s",
			  tree_code_name[(int) TREE_CODE (var)]);
	}
      break;

    case GIMPLE_ASSIGN:
      bounds = pl_compute_bounds_for_assignment (node, def_stmt);
      break;

    case GIMPLE_CALL:
      bounds = pl_build_returned_bound (def_stmt);
      break;

    case GIMPLE_PHI:
      stmt = create_phi_node (pl_get_tmp_var (), gimple_bb (def_stmt));
      bounds = gimple_phi_result (stmt);
      *iter = gsi_for_stmt (stmt);

      pl_register_bounds (node, bounds);
      pl_register_incomplete_bounds (bounds, node);
      break;

    case GIMPLE_ASM:
      bounds = pl_get_zero_bounds ();
      pl_register_bounds (node, bounds);
      warning (0, "PL: inline assembler is a potential pointer producer; using zero bounds (%s:%d)",
	       LOCATION_FILE (gimple_location (def_stmt)),
	       LOCATION_LINE (gimple_location (def_stmt)));
      break;

    default:
      internal_error ("pl_get_bounds_by_definition: Unexpected GIMPLE code %s",
		      gimple_code_name[code]);
    }

  return bounds;
}

static tree
pl_make_bounds (tree lb, tree size, gimple_stmt_iterator *iter, bool after)
{
  gimple_seq seq;
  gimple_stmt_iterator gsi;
  gimple stmt;
  tree bounds;

  if (iter)
    gsi = *iter;
  else
    gsi = gsi_start_bb (pl_get_entry_block ());

  seq = gimple_seq_alloc ();

  lb = pl_force_gimple_call_op (lb, &seq);
  size = pl_force_gimple_call_op (size, &seq);

  stmt = gimple_build_call (pl_bndmk_fndecl, 2, lb, size);
  pl_mark_stmt (stmt);

  bounds = make_ssa_name (pl_get_tmp_var (), stmt);
  gimple_call_set_lhs (stmt, bounds);

  gimple_seq_add_stmt (&seq, stmt);

  if (iter && after)
    gsi_insert_seq_after (&gsi, seq, GSI_SAME_STMT);
  else
    gsi_insert_seq_before (&gsi, seq, GSI_SAME_STMT);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Made bounds: ");
      print_gimple_stmt (dump_file, stmt, 0, TDF_VOPS|TDF_MEMSYMS);
      if (iter)
	{
	  fprintf (dump_file, "  inserted before statement: ");
	  print_gimple_stmt (dump_file, gsi_stmt (*iter), 0, TDF_VOPS|TDF_MEMSYMS);
	}
      else
	fprintf (dump_file, "  at function entry\n");
    }

  /* update_stmt (stmt); */

  return bounds;
}

/* When var has incomplete type we cannot get size to compute its bounds.
   In such cases we generate code to compute var bounds using special
   symbols pointing its begin and end.  */
static tree
pl_generate_extern_var_bounds (tree var)
{
  const char *prefix = "__pl_end_of_";
  const char *var_name = IDENTIFIER_POINTER (DECL_NAME (var));
  tree bounds;
  tree end_decl;
  tree var_end;
  tree lb;
  tree size;
  char *end_name;

  /* If PL instrumentation is not enabled for vars having
     incomplete type then just return zero bounds to avoid
     checks for this var.  */
  if (!flag_pl_incomplete_type)
    return pl_get_zero_bounds ();

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Generating bounds for extern symbol '");
      print_generic_expr (dump_file, var, 0);
      fprintf (dump_file, "'\n");
    }

  end_name = (char *) xmalloc (strlen (var_name) + strlen (prefix) + 1);
  strcpy (end_name, prefix);
  strcat (end_name, var_name);

  end_decl = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			 get_identifier (end_name), TREE_TYPE (var));
  TREE_PUBLIC (end_decl) = 1;
  DECL_EXTERNAL (end_decl) = 1;
  DECL_ARTIFICIAL (end_decl) = 1;

  lb = pl_build_addr_expr (var);
  var_end = pl_build_addr_expr (end_decl);
  size = size_binop (MINUS_EXPR,
		     fold_convert (pl_uintptr_type, var_end),
		     fold_convert (pl_uintptr_type, lb));

  bounds = pl_make_bounds (lb, size, NULL, false);

  free (end_name);

  return bounds;
}

static tree
pl_get_bounds_for_decl (tree decl)
{
  tree bounds;
  tree lb;

  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == PARM_DECL);

  bounds = pl_get_registered_bounds (decl);

  if (bounds)
    return bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Building bounds for decl ");
      print_generic_expr (dump_file, decl, 0);
      fprintf (dump_file, "\n");
    }

  lb = pl_build_addr_expr (decl);

  if (DECL_SIZE (decl))
    bounds = pl_make_bounds (lb, DECL_SIZE_UNIT (decl), NULL, false);
  else
    {
      gcc_assert (TREE_CODE (decl) == VAR_DECL);
      bounds = pl_generate_extern_var_bounds (decl);
    }

  pl_register_bounds (decl, bounds);

  return bounds;
}

static tree
pl_get_bounds_for_decl_addr (tree decl)
{
  tree bounds;
  tree lb;

  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == PARM_DECL
	      || TREE_CODE (decl) == RESULT_DECL);

  bounds = pl_get_registered_addr_bounds (decl);

  if (bounds)
    return bounds;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Building bounds for address of decl ");
      print_generic_expr (dump_file, decl, 0);
      fprintf (dump_file, "\n");
    }

  lb = pl_build_addr_expr (decl);

  if (DECL_SIZE (decl))
    bounds = pl_make_bounds (lb, DECL_SIZE_UNIT (decl), NULL, false);
  else
    {
      gcc_assert (TREE_CODE (decl) == VAR_DECL);
      bounds = pl_generate_extern_var_bounds (decl);
    }

  return bounds;
}

static tree
pl_get_bounds_for_string_cst (tree cst)
{
  tree bounds;
  tree lb;
  tree size;

  gcc_assert (TREE_CODE (cst) == STRING_CST);

  bounds = pl_get_registered_bounds (cst);

  if (bounds)
    return bounds;

  lb = pl_build_addr_expr (cst);
  size = build_int_cst (pl_uintptr_type, TREE_STRING_LENGTH (cst));
  bounds = pl_make_bounds (lb, size, NULL, false);

  pl_register_bounds (cst, bounds);

  return bounds;
}

static tree
pl_make_addressed_object_bounds (tree obj, gimple_stmt_iterator *iter,
				 bool always_narrow_fields)
{
  tree bounds = pl_get_registered_addr_bounds (obj);

  if (bounds)
    return bounds;

  switch (TREE_CODE (obj))
    {
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      bounds = pl_get_bounds_for_decl_addr (obj);
      break;

    case STRING_CST:
      bounds = pl_get_bounds_for_string_cst (obj);
      break;

    case ARRAY_REF:
    case COMPONENT_REF:
      {
	tree elt;
	tree ptr;
	bool safe;
	bool bitfield;

	pl_parse_array_and_component_ref (obj, &ptr, &elt, &safe,
					  &bitfield, &bounds, iter, true,
					  always_narrow_fields);

	gcc_assert (bounds);
      }
      break;

    case FUNCTION_DECL:
      bounds = pl_get_zero_bounds ();
      break;

    case MEM_REF:
      bounds = pl_find_bounds (TREE_OPERAND (obj, 0), iter);
      break;

    default:
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "pl_make_addressed_object_bounds: "
		   "unexpected object of type %s\n",
		   tree_code_name[TREE_CODE (obj)]);
	  print_node (dump_file, "", obj, 0);
	}
      internal_error ("pl_make_addressed_object_bounds: Unexpected tree code %s",
		      tree_code_name[TREE_CODE (obj)]);
    }

  pl_register_addr_bounds (obj, bounds);

  return bounds;
}

/* Compute bounds for pointer PTR loaded from PTR_SRC.  Generate statements
   to compute bounds if required.  Computed bounds should be available at
   position pointed by ITER.

   If PTR_SRC is NULL_TREE then pointer definition is identified.

   If PTR_SRC is not NULL_TREE then ITER points to statements which loads
   PTR.  If PTR is a any memory reference then ITER points to a statement
   after which bndldx will be inserterd.  In both cases ITER will be updated
   to point to the inserted bndldx statement.

   If ALWAYS_NARROW_FIELD is non zero and PTR is an address of structure
   field then we have to ignore flag_pl_first_field_has_own_bounds flag
   value and perform bounds narrowing for this field.  */

static tree
pl_find_bounds_1 (tree ptr, tree ptr_src, gimple_stmt_iterator *iter,
		  bool always_narrow_fields)
{
  tree addr = NULL_TREE;
  tree bounds = NULL_TREE;

  if (!ptr_src)
    ptr_src = ptr;

  bounds = pl_get_registered_bounds (ptr_src);

  if (bounds)
    return bounds;

  switch (TREE_CODE (ptr_src))
    {
    case MEM_REF:
    case ARRAY_REF:
    case COMPONENT_REF:
    case VAR_DECL:
      if (BOUNDED_P (ptr_src))
	{
	  addr = pl_build_addr_expr (ptr_src);
	  bounds = pl_build_bndldx (addr, ptr, iter);
	}
      else
	bounds = pl_get_nonpointer_load_bounds ();
      break;

    case PARM_DECL:
      gcc_unreachable ();
      bounds = pl_get_bound_for_parm (ptr_src);
      break;

    case TARGET_MEM_REF:
      addr = pl_build_addr_expr (ptr_src);
      bounds = pl_build_bndldx (addr, ptr, iter);
      break;

    case SSA_NAME:
      bounds = pl_get_registered_bounds (ptr_src);
      if (!bounds)
	{
	  gimple def_stmt = SSA_NAME_DEF_STMT (ptr_src);
	  gimple_stmt_iterator phi_iter;

	  bounds = pl_get_bounds_by_definition(ptr_src, def_stmt, &phi_iter);

	  gcc_assert (bounds);

	  if (gimple_code (def_stmt) == GIMPLE_PHI)
	    {
	      /* pl_get_bounds_by_definition created new phi
		 statement and phi_iter points to it.  */
	      gimple phi_bnd = gsi_stmt (phi_iter);
	      unsigned i;

	      for (i = 0; i < gimple_phi_num_args (def_stmt); i++)
		{
		  tree arg = gimple_phi_arg_def (def_stmt, i);
		  tree arg_bnd;

		  arg_bnd = pl_find_bounds (arg, NULL);

		  add_phi_arg (phi_bnd, arg_bnd,
			       gimple_phi_arg_edge (def_stmt, i),
			       UNKNOWN_LOCATION);
		}

	      if (pl_may_finish_incomplete_bounds ())
		pl_finish_incomplete_bounds ();
	    }

	  gcc_assert (bounds == pl_get_registered_bounds (ptr_src)
		      || pl_incomplete_bounds (bounds));
	}
      break;

    case ADDR_EXPR:
      bounds = pl_make_addressed_object_bounds (TREE_OPERAND (ptr_src, 0), iter,
						always_narrow_fields);
      break;

    case INTEGER_CST:
      if (ptr_src == integer_zero_node)
	bounds = pl_get_none_bounds ();
      else
	bounds = pl_get_invalid_op_bounds ();
      break;

    default:
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "pl_find_bounds: unexpected ptr of type %s\n",
		   tree_code_name[TREE_CODE (ptr_src)]);
	  print_node (dump_file, "", ptr_src, 0);
	}
      internal_error ("pl_find_bounds: Unexpected tree code %s",
		      tree_code_name[TREE_CODE (ptr_src)]);
    }

  if (!bounds)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (stderr, "pl_find_bounds: cannot find bounds for pointer\n");
	  print_node (dump_file, "", ptr_src, 0);
	}
      internal_error ("pl_find_bounds: Cannot find bounds for pointer");
    }

  return bounds;
}

/* Normal case for bounds search without forced narrowing.  */

static tree
pl_find_bounds (tree ptr, gimple_stmt_iterator *iter)
{
  return pl_find_bounds_1 (ptr, NULL_TREE, iter, false);
}

/* Search bounds for pointer PTR loaded from PTR_SRC
   by statement *ITER points to.  */

static tree
pl_find_bounds_loaded (tree ptr, tree ptr_src, gimple_stmt_iterator *iter)
{
  return pl_find_bounds_1 (ptr, ptr_src, iter, false);
}

/* Search for narrowed bounds if applicable.  */

static tree
pl_find_bounds_narrowed (tree ptr, gimple_stmt_iterator *iter)
{
  return pl_find_bounds_1 (ptr, NULL_TREE, iter, true);
}

static tree
pl_intersect_bounds (tree bounds1, tree bounds2, gimple_stmt_iterator *iter)
{
  if (!bounds1 || bounds1 == pl_get_zero_bounds ())
    return bounds2 ? bounds2 : bounds1;
  else if (!bounds2 || bounds2 == pl_get_zero_bounds ())
    return bounds1;
  else
    {
        gimple_seq seq;
	gimple stmt;
	tree bounds;

	seq = gimple_seq_alloc ();

	stmt = gimple_build_call (pl_intersect_fndecl, 2, bounds1, bounds2);
	pl_mark_stmt (stmt);

	bounds = make_ssa_name (pl_get_tmp_var (), stmt);
	gimple_call_set_lhs (stmt, bounds);

	gimple_seq_add_stmt (&seq, stmt);

	gsi_insert_seq_before (iter, seq, GSI_SAME_STMT);

	if (dump_file && (dump_flags & TDF_DETAILS))
	  {
	    fprintf (dump_file, "Bounds intersection: ");
	    print_gimple_stmt (dump_file, stmt, 0, TDF_VOPS|TDF_MEMSYMS);
	    fprintf (dump_file, "  inserted before statement: ");
	    print_gimple_stmt (dump_file, gsi_stmt (*iter), 0,
			       TDF_VOPS|TDF_MEMSYMS);
	  }

	return bounds;
    }
}

static bool
pl_may_narrow_to_field (tree field)
{
  return DECL_SIZE (field) && TREE_CODE (DECL_SIZE (field)) == INTEGER_CST
    && (!DECL_FIELD_OFFSET (field)
	|| TREE_CODE (DECL_FIELD_OFFSET (field)) == INTEGER_CST)
    && (!DECL_FIELD_BIT_OFFSET (field)
	|| TREE_CODE (DECL_FIELD_BIT_OFFSET (field)) == INTEGER_CST);
}

/* Return true if bounds for FIELD should be narrowed to
   field's own size.

   If ALWAYS_NARROW is non zero then true is returned.  */

static bool
pl_narrow_bounds_for_field (tree field, bool always_narrow)
{
  HOST_WIDE_INT offs;
  HOST_WIDE_INT bit_offs;

  if (!pl_may_narrow_to_field (field))
    return false;

  offs = tree_low_cst (DECL_FIELD_OFFSET (field), 1);
  bit_offs = tree_low_cst (DECL_FIELD_BIT_OFFSET (field), 1);

  return (always_narrow || flag_pl_first_field_has_own_bounds || offs || bit_offs)
    && pl_may_narrow_to_field (field);
}

static tree
pl_narrow_bounds_to_field (tree bounds, tree component,
			   gimple_stmt_iterator *iter)
{
  tree field = TREE_OPERAND (component, 1);
  tree size = DECL_SIZE_UNIT (field);
  tree field_ptr = pl_build_addr_expr (component);
  tree field_bounds;

  field_bounds = pl_make_bounds (field_ptr, size, iter, false);

  return pl_intersect_bounds (field_bounds, bounds, iter);
}

static void
pl_parse_array_and_component_ref (tree node, tree *ptr,
				  tree *elt, bool *safe,
				  bool *bitfield,
				  tree *bounds,
				  gimple_stmt_iterator *iter,
				  bool innermost_bounds,
				  bool always_narrow)
{
  tree comp_to_narrow = NULL_TREE;
  tree last_comp = NULL_TREE;
  bool array_ref_found = false;
  tree *nodes;
  tree var;
  int len;
  int i;

  /* Compute tree height for expression.  */
  var = node;
  len = 1;
  while (TREE_CODE (var) == COMPONENT_REF
	 || TREE_CODE (var) == ARRAY_REF)
    {
      var = TREE_OPERAND (var, 0);
      len++;
    }

  gcc_assert (len > 1);

  /* It is more convenient for us to scan left-to-right,
     so walk tree again and put all node to nodes vector
     in reversed order.  */
  nodes = XALLOCAVEC (tree, len);
  nodes[len - 1] = node;
  for (i = len - 2; i >= 0; i--)
    nodes[i] = TREE_OPERAND (nodes[i + 1], 0);

  *bounds = NULL;
  *safe = true;
  *bitfield = (TREE_CODE (node) == COMPONENT_REF
	       && DECL_BIT_FIELD_TYPE (TREE_OPERAND (node, 1)));
  /* To get bitfield address we will need outer elemnt.  */
  if (*bitfield && (TREE_CODE (nodes[len - 2]) == ARRAY_REF
		    || TREE_CODE (nodes[len - 2]) == COMPONENT_REF))
    *elt = nodes[len - 2];
  else
    *elt = NULL_TREE;

  /* If we have indirection in expression then compute
     outermost structure bounds.  Computed bounds may be
     narrowed later.  */
  if (TREE_CODE (nodes[0]) == MEM_REF || INDIRECT_REF_P (nodes[0]))
    {
      *safe = false;
      *ptr = TREE_OPERAND (nodes[0], 0);
      *bounds = pl_find_bounds (*ptr, iter);
    }
  else
    {
      gcc_assert (TREE_CODE (var) == VAR_DECL
		  || TREE_CODE (var) == PARM_DECL
		  || TREE_CODE (var) == RESULT_DECL
		  || TREE_CODE (var) == STRING_CST);

      *ptr = pl_build_addr_expr (var);
    }

  /* In this loop we are trying to find a field access
     requiring narrowing.  There are two simple rules
     for search:
     1. Leftmost array_ref is chosen if any.
     2. Rightmost suitable component_ref is chosen if innermost
     bounds are required and no array_ref exists.  */
  for (i = 1; i < len; i++)
    {
      var = nodes[i];

      if (TREE_CODE (var) == ARRAY_REF)
	{
	  *safe = false;
	  array_ref_found = true;
	  if (!flag_pl_narrow_to_innermost_arrray
	      && (!last_comp
		  || pl_may_narrow_to_field (TREE_OPERAND (last_comp, 1))))
	    {
	      comp_to_narrow = last_comp;
	      break;
	    }
	}
      else if (TREE_CODE (var) == COMPONENT_REF)
	{
	  tree field = TREE_OPERAND (var, 1);

	  if (innermost_bounds
	      && !array_ref_found
	      && pl_narrow_bounds_for_field (field, always_narrow))
	    comp_to_narrow = var;
	  last_comp = var;

	  if (flag_pl_narrow_to_innermost_arrray
	      && TREE_CODE (TREE_TYPE (field)) == ARRAY_TYPE)
	    {
	      *bounds = pl_narrow_bounds_to_field (*bounds, var, iter);
	      comp_to_narrow = NULL;
	    }
	}
      else
	gcc_unreachable ();
    }

  if (comp_to_narrow && DECL_SIZE (TREE_OPERAND (comp_to_narrow, 1)))
    *bounds = pl_narrow_bounds_to_field (*bounds, comp_to_narrow, iter);

  if (innermost_bounds && !*bounds)
    {
      gcc_assert ((!flag_pl_first_field_has_own_bounds && *safe)
		  || TREE_CODE (nodes[1]) == ARRAY_REF);

      *bounds = pl_find_bounds (*ptr, iter);
    }
}

static tree
pl_build_component_ref (tree obj, tree field)
{
  tree res;

  /* If object is TMR then we do not use component_ref but
     add offset instead.  We need it to be able to get addr
     of the reasult later.  */
  if (TREE_CODE (obj) == TARGET_MEM_REF) {
    tree offs = TMR_OFFSET (obj);
    offs = fold_binary_to_constant (PLUS_EXPR, TREE_TYPE (offs),
				    offs, DECL_FIELD_OFFSET (field));

    gcc_assert (offs);

    res = copy_node (obj);
    TREE_TYPE (res) = TREE_TYPE (field);
    TMR_OFFSET (res) = offs;
  } else
    res = build3 (COMPONENT_REF, TREE_TYPE (field), obj, field, NULL_TREE);

  return res;
}

static tree
pl_build_array_ref (tree arr, tree etype, tree esize,
		    unsigned HOST_WIDE_INT idx)
{
  tree index = build_int_cst (size_type_node, idx);
  tree res;

  /* If object is TMR then we do not use array_ref but
     add offset instead.  We need it to be able to get addr
     of the reasult later.  */
  if (TREE_CODE (arr) == TARGET_MEM_REF)
    {
      tree offs = TMR_OFFSET (arr);

      esize = fold_binary_to_constant (MULT_EXPR, TREE_TYPE (esize),
				     esize, index);
      gcc_assert(esize);

      offs = fold_binary_to_constant (PLUS_EXPR, TREE_TYPE (offs),
				    offs, esize);
      gcc_assert (offs);

      res = copy_node (arr);
      TREE_TYPE (res) = etype;
      TMR_OFFSET (res) = offs;
    }
  else
    res = build4 (ARRAY_REF, etype, arr, index, NULL_TREE, NULL_TREE);

  return res;
}

static void
pl_copy_bounds_for_assign (tree lhs, tree rhs, void *arg)
{
  gimple_stmt_iterator *iter = (gimple_stmt_iterator *)arg;
  tree bounds = pl_find_bounds (rhs, iter);
  tree addr = pl_build_addr_expr(lhs);

  pl_build_bndstx (addr, rhs, bounds, iter);
}

static void
pl_walk_pointer_assignments (tree lhs, tree rhs, void *arg, assign_handler handler)
{
  tree type = TREE_TYPE (rhs);

  /* We have nothing to do with clobbers.  */
  if (TREE_CLOBBER_P (rhs))
    return;

  if (BOUNDED_TYPE_P (type))
    handler (lhs, rhs, arg);
  else if (RECORD_OR_UNION_TYPE_P (type))
    {
      tree field;

      if (TREE_CODE (rhs) == CONSTRUCTOR)
	{
	  unsigned HOST_WIDE_INT cnt;
	  tree val;

	  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (rhs), cnt, field, val)
	    {
	      if (pl_type_has_pointer (TREE_TYPE (field)))
		{
		  tree lhs_field = pl_build_component_ref (lhs, field);
		  pl_walk_pointer_assignments (lhs_field, val, arg, handler);
		}
	    }
	}
      else
	for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	  if (TREE_CODE (field) == FIELD_DECL
	      && pl_type_has_pointer (TREE_TYPE (field)))
	    {
	      tree rhs_field = pl_build_component_ref (rhs, field);
	      tree lhs_field = pl_build_component_ref (lhs, field);
	      pl_walk_pointer_assignments (lhs_field, rhs_field, arg, handler);
	    }
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      unsigned HOST_WIDE_INT cur = -1;
      tree maxval = TYPE_MAX_VALUE (TYPE_DOMAIN (type));
      tree etype = TREE_TYPE (type);
      tree esize = TYPE_SIZE (etype);

      if (TREE_CODE (rhs) == CONSTRUCTOR)
	{
	  unsigned HOST_WIDE_INT cnt;
	  tree purp, val, lhs_elem;

	  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (rhs), cnt, purp, val)
	    {
	      if (TREE_CODE (purp) == RANGE_EXPR)
		{
		  tree lo_index = TREE_OPERAND (purp, 0);
		  tree hi_index = TREE_OPERAND (purp, 1);

		  for (cur = tree_low_cst (lo_index, 1);
		       cur <= tree_low_cst (hi_index, 1);
		       cur++)
		    {
		      lhs_elem = pl_build_array_ref (lhs, etype, esize, cur);
		      pl_walk_pointer_assignments (lhs_elem, val, arg, handler);
		    }
		}
	      else
		{
		  if (TREE_CODE (purp) == INTEGER_CST)
		    cur = tree_low_cst (purp, 1);
		  else
		    {
		      gcc_assert (!purp);
		      cur++;
		    }

		  lhs_elem = pl_build_array_ref (lhs, etype, esize, cur);

		  pl_walk_pointer_assignments (lhs_elem, val, arg, handler);
		}
	    }
	}
      /* Copy array only whe size is known.  */
      else if (maxval)
	for (cur = 0; cur <= TREE_INT_CST_LOW (maxval); cur++)
	  {
	    tree lhs_elem = pl_build_array_ref (lhs, etype, esize, cur);
	    tree rhs_elem = pl_build_array_ref (rhs, etype, esize, cur);
	    pl_walk_pointer_assignments (lhs_elem, rhs_elem, arg, handler);
	  }
    }
  else
    internal_error("pl_walk_pointer_assignments: unexpected RHS type: %s",
		   tree_code_name[TREE_CODE (type)]);
}

static void
pl_process_stmt (gimple_stmt_iterator *iter, tree node,
		 location_t loc, tree dirflag,
		 tree access_offs, tree access_size,
		 bool safe)
{
  tree node_type = TREE_TYPE (node);
  tree size = access_size ? access_size : TYPE_SIZE_UNIT (node_type);
  tree addr_first = NULL_TREE; /* address of the first accessed byte */
  tree addr_last = NULL_TREE; /* address of the last accessed byte */
  tree ptr = NULL_TREE; /* a pointer used for dereference */
  tree bounds = NULL_TREE;

  switch (TREE_CODE (node))
    {
    case ARRAY_REF:
    case COMPONENT_REF:
      {
	bool bitfield;
	tree elt;

	if (safe)
	  {
	    /* We are not going to generate any checks, so do not
	       generate bounds as well.  */
	    addr_first = pl_build_addr_expr (node);
	    break;
	  }

	pl_parse_array_and_component_ref (node, &ptr, &elt, &safe,
					  &bitfield, &bounds, iter, false,
					  false);

	/* Break if there is no dereference and operation is safe.  */
	if (safe)
	  {
	    /* We may still need addr_first for bndstx in case
	       write a pointer into memory.  */
	    addr_first = pl_build_addr_expr (node);
	    break;
	  }

	if (bitfield)
          {
            tree field = TREE_OPERAND (node, 1);

            if (TREE_CODE (DECL_SIZE_UNIT (field)) == INTEGER_CST)
              size = DECL_SIZE_UNIT (field);

	    if (elt)
	      elt = pl_build_addr_expr (elt);
            addr_first = fold_convert_loc (loc, ptr_type_node, elt ? elt : ptr);
            addr_first = fold_build_pointer_plus_loc (loc,
						      addr_first,
						      byte_position (field));
          }
        else
          addr_first = pl_build_addr_expr (node);
      }
      break;

    case INDIRECT_REF:
      ptr = TREE_OPERAND (node, 0);
      addr_first = ptr;
      break;

    case MEM_REF:
      ptr = TREE_OPERAND (node, 0);
      addr_first = pl_build_addr_expr (node);
      break;

    case TARGET_MEM_REF:
      ptr = TMR_BASE (node);
      addr_first = pl_build_addr_expr (node);
      break;

    case ARRAY_RANGE_REF:
      printf("ARRAY_RANGE_REF\n");
      debug_gimple_stmt(gsi_stmt(*iter));
      debug_tree(node);
      gcc_unreachable ();
      break;

    case BIT_FIELD_REF:
      {
	tree offs, rem, bpu;

	gcc_assert (!access_offs);
	gcc_assert (!access_size);

	bpu = fold_convert (size_type_node, bitsize_int (BITS_PER_UNIT));
	offs = fold_convert (size_type_node, TREE_OPERAND (node, 2));
	rem = size_binop_loc (loc, TRUNC_MOD_EXPR, offs, bpu);
	offs = size_binop_loc (loc, TRUNC_DIV_EXPR, offs, bpu);

	size = fold_convert (size_type_node, TREE_OPERAND (node, 1));
        size = size_binop_loc (loc, PLUS_EXPR, size, rem);
        size = size_binop_loc (loc, CEIL_DIV_EXPR, size, bpu);
        size = fold_convert (size_type_node, size);

	pl_process_stmt (iter, TREE_OPERAND (node, 0), loc,
			 dirflag, offs, size, safe);
	return;
      }
      break;

    case VAR_DECL:
    case RESULT_DECL:
    case PARM_DECL:
      if (dirflag != integer_one_node)
	return;

      safe = true;
      addr_first = pl_build_addr_expr (node);
      break;

    default:
      return;
    }

  /* If addr_last was not computed then use (addr_first + size - 1)
     expression to compute it.  */
  if (!addr_last)
    {
      addr_last = fold_build_pointer_plus_loc (loc, addr_first, size);
      addr_last = fold_build_pointer_plus_hwi_loc (loc, addr_last, -1);
    }

  /* Shift both first_addr and last_addr by access_offs if specified.  */
  if (access_offs)
    {
      addr_first = fold_build_pointer_plus_loc (loc, addr_first, access_offs);
      addr_last = fold_build_pointer_plus_loc (loc, addr_last, access_offs);
    }

  /* Generate bndcl/bndcu checks if memory access is not safe.  */
  if (!safe)
    {
      gimple_stmt_iterator stmt_iter = *iter;

      if (!bounds)
	bounds = pl_find_bounds (ptr, iter);

      pl_check_mem_access (addr_first, addr_last, bounds,
			   stmt_iter, loc, dirflag);
    }

  /* We need to generate bndstx in case pointer is stored.  */
  if (dirflag == integer_one_node && pl_type_has_pointer (node_type))
    {
      gimple stmt = gsi_stmt (*iter);
      tree rhs1 = gimple_assign_rhs1 (stmt);
      enum tree_code rhs_code = gimple_assign_rhs_code (stmt);

      if (get_gimple_rhs_class (rhs_code) == GIMPLE_SINGLE_RHS)
	pl_walk_pointer_assignments (node, rhs1, iter, pl_copy_bounds_for_assign);
      else
	{
	  bounds = pl_compute_bounds_for_assignment (NULL_TREE, stmt);
	  pl_build_bndstx (addr_first, rhs1, bounds, iter);
	}
    }
}

/* Add input bound arguments declaration to the function
   declaration DECL.  Do nothing if function already have
   bound arguments.  If MAKE_SSA_NAMES is set then ssa name
   is created and registered as a bound for each added
   argument.  */
static void
pl_fix_function_decl (tree decl, bool make_ssa_names)
{
  tree arg = DECL_ARGUMENTS (decl);
  tree prev_arg = NULL_TREE;
  bool already_fixed = false;
  int bnd_no = 0;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file,
	       "Building bound arguments for funtion declaration '");
      print_generic_expr (dump_file, decl, 0);
      fprintf (dump_file, "'\n");
    }

  /* Nothing to do if function has no input arguments.  */
  if (!arg)
    return;

  for (arg = DECL_ARGUMENTS (decl); arg; arg = TREE_CHAIN (arg))
    {
      /* Bound argument may appear only if the
	 declaration has been already fixed.  */
      if (BOUND_TYPE_P (TREE_TYPE (arg)))
	{
	  if (make_ssa_names)
	    {
	      tree bounds = make_ssa_name (arg, gimple_build_nop ());
	      SSA_NAME_IS_DEFAULT_DEF (bounds) = 1;

	      gcc_assert (prev_arg);
	      pl_register_bounds (prev_arg, bounds);

	      already_fixed = true;
	    }
	  else
	    return;
	}

      prev_arg = arg;
    }

  if (already_fixed)
    return;

  /* Go through all input pointers and create bound
     declaration for each of them.  Bound declaration
     is placed right before pointer arg.  Also make
     register ssa name for each bound if needed.  */
  for (arg = DECL_ARGUMENTS (decl); arg; arg = TREE_CHAIN (arg))
    {
      if (BOUNDED_P (arg))
	{
	  char name_buf[20];
	  tree name;
	  tree bounds;

	  sprintf (name_buf, "__arg_bnd.%d", bnd_no++);
	  name = get_identifier (name_buf);

	  bounds = build_decl (UNKNOWN_LOCATION, PARM_DECL, name,
				 pl_bound_type);
	  DECL_ARG_TYPE (bounds) = pl_bound_type;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "  Built bounds '");
	      print_generic_expr (dump_file, bounds, 0);
	      fprintf (dump_file, "' for arg '");
	      print_generic_expr (dump_file, arg, 0);
	      fprintf (dump_file, "' of type '");
	      print_generic_expr (dump_file, TREE_TYPE (arg), 0);
	      fprintf (dump_file, "'\n");
	    }

	  TREE_CHAIN (bounds) = TREE_CHAIN (arg);
	  TREE_CHAIN (arg) = bounds;

	  if (make_ssa_names)
	    {
	      tree ssa = make_ssa_name (bounds, gimple_build_nop ());
	      SSA_NAME_IS_DEFAULT_DEF (ssa) = 1;
	      pl_register_bounds (arg, ssa);
	    }

	  /* Skip inserted bound arg.  */
	  arg = TREE_CHAIN (arg);
	}
    }
}

static void
pl_fix_function_decls (void)
{
  basic_block bb;
  gimple_stmt_iterator i;

  pl_fix_function_decl (cfun->decl, true);

  FOR_ALL_BB (bb)
    for (i = gsi_start_bb (bb); !gsi_end_p (i); gsi_next (&i))
      {
	gimple stmt = gsi_stmt (i);
	if (is_gimple_call (stmt))
	  {
	    tree fndecl = gimple_call_fndecl (stmt);
	    if (fndecl)
	      pl_fix_function_decl (fndecl, false);
	  }
      }
}

static void
pl_init (void)
{
  pl_reg_bounds = htab_create_ggc (31, tree_map_hash, tree_map_eq,
				   NULL);
  pl_reg_addr_bounds = htab_create_ggc (31, tree_map_hash, tree_map_eq,
					NULL);
  pl_incomplete_bounds_map = htab_create_ggc (31, tree_map_hash, tree_map_eq,
					      NULL);
  pl_marked_stmts = htab_create_ggc (31, htab_hash_pointer, htab_eq_pointer,
				     NULL);
  pl_invalid_bounds = htab_create_ggc (31, htab_hash_pointer,
				       htab_eq_pointer, NULL);
  pl_completed_bounds_map = htab_create_ggc (31, htab_hash_pointer,
					     htab_eq_pointer, NULL);

  entry_block = NULL;
  zero_bounds = NULL_TREE;
  none_bounds = NULL_TREE;
  incomplete_bounds = integer_zero_node;
  tmp_var = NULL_TREE;

  pl_bound_type = TARGET_64BIT ? bound64_type_node : bound32_type_node;
  pl_uintptr_type = lang_hooks.types.type_for_mode (ptr_mode, true);

  /* Build declarations for builtin functions.  */
  pl_bndldx_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDLDX);
  pl_bndstx_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDSTX);
  pl_checkl_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDCL);
  pl_checku_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDCU);
  pl_bndmk_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDMK);
  pl_ret_bnd_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_BNDRET);
  pl_intersect_fndecl = targetm.builtin_pl_function (BUILT_IN_PL_INTERSECT);
  pl_user_intersect_fndecl
    = targetm.builtin_pl_function (BUILT_IN_PL_USER_INTERSECT);
  pl_bind_intersect_fndecl
    = targetm.builtin_pl_function (BUILT_IN_PL_BIND_INTERSECT);
  pl_arg_bnd_fndecl
    = targetm.builtin_pl_function (BUILT_IN_PL_ARG_BND);
}

static void
pl_fini (void)
{

}

static unsigned int
pl_execute (void)
{
  /* FIXME: check we need to instrument this function */
  pl_init ();

  pl_transform_function ();

  pl_fini ();

  return 0;
}

static bool
pl_gate (void)
{
  return flag_pl != 0;
}

struct gimple_opt_pass pass_pl =
{
 {
  GIMPLE_PASS,
  "pl",                                 /* name */
  pl_gate,                              /* gate */
  pl_execute,                           /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_NONE,                              /* tv_id */
  PROP_ssa | PROP_cfg,                  /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_verify_flow | TODO_verify_stmts
  | TODO_update_ssa                     /* todo_flags_finish */
 }
};
