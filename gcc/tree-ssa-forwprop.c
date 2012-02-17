/* Forward propagation of expressions for single use variables.
   Copyright (C) 2004, 2005, 2007, 2008, 2009, 2010, 2011
   Free Software Foundation, Inc.

This file is part of GCC.

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
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "gimple-pretty-print.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "langhooks.h"
#include "flags.h"
#include "gimple.h"
#include "expr.h"

/* This pass propagates the RHS of assignment statements into use
   sites of the LHS of the assignment.  It's basically a specialized
   form of tree combination.   It is hoped all of this can disappear
   when we have a generalized tree combiner.

   One class of common cases we handle is forward propagating a single use
   variable into a COND_EXPR.

     bb0:
       x = a COND b;
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
       if (a COND b) goto ... else goto ...

   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).

   Or (assuming c1 and c2 are constants):

     bb0:
       x = a + c1;
       if (x EQ/NEQ c2) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a EQ/NEQ (c2 - c1)) goto ... else goto ...

   Similarly for x = a - c1.

   Or

     bb0:
       x = !a
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a == 0) goto ... else goto ...

   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).
   For these cases, we propagate A into all, possibly more than one,
   COND_EXPRs that use X.

   Or

     bb0:
       x = (typecast) a
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a != 0) goto ... else goto ...

   (Assuming a is an integral type and x is a boolean or x is an
    integral and a is a boolean.)

   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).
   For these cases, we propagate A into all, possibly more than one,
   COND_EXPRs that use X.

   In addition to eliminating the variable and the statement which assigns
   a value to the variable, we may be able to later thread the jump without
   adding insane complexity in the dominator optimizer.

   Also note these transformations can cascade.  We handle this by having
   a worklist of COND_EXPR statements to examine.  As we make a change to
   a statement, we put it back on the worklist to examine on the next
   iteration of the main loop.

   A second class of propagation opportunities arises for ADDR_EXPR
   nodes.

     ptr = &x->y->z;
     res = *ptr;

   Will get turned into

     res = x->y->z;

   Or
     ptr = (type1*)&type2var;
     res = *ptr

   Will get turned into (if type1 and type2 are the same size
   and neither have volatile on them):
     res = VIEW_CONVERT_EXPR<type1>(type2var)

   Or

     ptr = &x[0];
     ptr2 = ptr + <constant>;

   Will get turned into

     ptr2 = &x[constant/elementsize];

  Or

     ptr = &x[0];
     offset = index * element_size;
     offset_p = (pointer) offset;
     ptr2 = ptr + offset_p

  Will get turned into:

     ptr2 = &x[index];

  Or
    ssa = (int) decl
    res = ssa & 1

  Provided that decl has known alignment >= 2, will get turned into

    res = 0

  We also propagate casts into SWITCH_EXPR and COND_EXPR conditions to
  allow us to remove the cast and {NOT_EXPR,NEG_EXPR} into a subsequent
  {NOT_EXPR,NEG_EXPR}.

   This will (of course) be extended as other needs arise.  */

static bool forward_propagate_addr_expr (tree name, tree rhs);

/* Set to true if we delete EH edges during the optimization.  */
static bool cfg_changed;

/* Get the next statement we can propagate NAME's value into skipping
   trivial copies.  Returns the statement that is suitable as a
   propagation destination or NULL_TREE if there is no such one.
   This only returns destinations in a single-use chain.  FINAL_NAME_P
   if non-NULL is written to the ssa name that represents the use.  */

static gimple
get_prop_dest_stmt (tree name, tree *final_name_p)
{
  use_operand_p use;
  gimple use_stmt;

  do {
    /* If name has multiple uses, bail out.  */
    if (!single_imm_use (name, &use, &use_stmt))
      return NULL;

    /* If this is not a trivial copy, we found it.  */
    if (!gimple_assign_ssa_name_copy_p (use_stmt)
	|| gimple_assign_rhs1 (use_stmt) != name)
      break;

    /* Continue searching uses of the copy destination.  */
    name = gimple_assign_lhs (use_stmt);
  } while (1);

  if (final_name_p)
    *final_name_p = name;

  return use_stmt;
}

/* Checks if the destination ssa name in DEF_STMT can be used as
   propagation source.  Returns true if so, otherwise false.  */

static bool
can_propagate_from (gimple def_stmt)
{
  gcc_assert (is_gimple_assign (def_stmt));

  /* If the rhs has side-effects we cannot propagate from it.  */
  if (gimple_has_volatile_ops (def_stmt))
    return false;

  /* If the rhs is a load we cannot propagate from it.  */
  if (TREE_CODE_CLASS (gimple_assign_rhs_code (def_stmt)) == tcc_reference
      || TREE_CODE_CLASS (gimple_assign_rhs_code (def_stmt)) == tcc_declaration)
    return false;

  /* Constants can be always propagated.  */
  if (gimple_assign_single_p (def_stmt)
      && is_gimple_min_invariant (gimple_assign_rhs1 (def_stmt)))
    return true;

  /* We cannot propagate ssa names that occur in abnormal phi nodes.  */
  if (stmt_references_abnormal_ssa_name (def_stmt))
    return false;

  /* If the definition is a conversion of a pointer to a function type,
     then we can not apply optimizations as some targets require
     function pointers to be canonicalized and in this case this
     optimization could eliminate a necessary canonicalization.  */
  if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt)))
    {
      tree rhs = gimple_assign_rhs1 (def_stmt);
      if (POINTER_TYPE_P (TREE_TYPE (rhs))
          && TREE_CODE (TREE_TYPE (TREE_TYPE (rhs))) == FUNCTION_TYPE)
        return false;
    }

  return true;
}

/* Remove a chain of dead statements starting at the definition of
   NAME.  The chain is linked via the first operand of the defining statements.
   If NAME was replaced in its only use then this function can be used
   to clean up dead stmts.  The function handles already released SSA
   names gracefully.
   Returns true if cleanup-cfg has to run.  */

static bool
remove_prop_source_from_use (tree name)
{
  gimple_stmt_iterator gsi;
  gimple stmt;
  bool cfg_changed = false;

  do {
    basic_block bb;

    if (SSA_NAME_IN_FREE_LIST (name)
	|| SSA_NAME_IS_DEFAULT_DEF (name)
	|| !has_zero_uses (name))
      return cfg_changed;

    stmt = SSA_NAME_DEF_STMT (name);
    if (gimple_code (stmt) == GIMPLE_PHI
	|| gimple_has_side_effects (stmt))
      return cfg_changed;

    bb = gimple_bb (stmt);
    gsi = gsi_for_stmt (stmt);
    unlink_stmt_vdef (stmt);
    gsi_remove (&gsi, true);
    release_defs (stmt);
    cfg_changed |= gimple_purge_dead_eh_edges (bb);

    name = is_gimple_assign (stmt) ? gimple_assign_rhs1 (stmt) : NULL_TREE;
  } while (name && TREE_CODE (name) == SSA_NAME);

  return cfg_changed;
}

/* We've just substituted an ADDR_EXPR into stmt.  Update all the
   relevant data structures to match.  */

static void
tidy_after_forward_propagate_addr (gimple stmt)
{
  /* We may have turned a trapping insn into a non-trapping insn.  */
  if (maybe_clean_or_replace_eh_stmt (stmt, stmt)
      && gimple_purge_dead_eh_edges (gimple_bb (stmt)))
    cfg_changed = true;

  if (TREE_CODE (gimple_assign_rhs1 (stmt)) == ADDR_EXPR)
     recompute_tree_invariant_for_addr_expr (gimple_assign_rhs1 (stmt));
}

/* DEF_RHS contains the address of the 0th element in an array.
   USE_STMT uses type of DEF_RHS to compute the address of an
   arbitrary element within the array.  The (variable) byte offset
   of the element is contained in OFFSET.

   We walk back through the use-def chains of OFFSET to verify that
   it is indeed computing the offset of an element within the array
   and extract the index corresponding to the given byte offset.

   We then try to fold the entire address expression into a form
   &array[index].

   If we are successful, we replace the right hand side of USE_STMT
   with the new address computation.  */

static bool
forward_propagate_addr_into_variable_array_index (tree offset,
						  tree def_rhs,
						  gimple_stmt_iterator *use_stmt_gsi)
{
  tree index, tunit;
  gimple offset_def, use_stmt = gsi_stmt (*use_stmt_gsi);
  tree new_rhs, tmp;

  if (TREE_CODE (TREE_OPERAND (def_rhs, 0)) == ARRAY_REF)
    tunit = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (def_rhs)));
  else if (TREE_CODE (TREE_TYPE (TREE_OPERAND (def_rhs, 0))) == ARRAY_TYPE)
    tunit = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (TREE_TYPE (def_rhs))));
  else
    return false;
  if (!host_integerp (tunit, 1))
    return false;

  /* Get the offset's defining statement.  */
  offset_def = SSA_NAME_DEF_STMT (offset);

  /* Try to find an expression for a proper index.  This is either a
     multiplication expression by the element size or just the ssa name we came
     along in case the element size is one. In that case, however, we do not
     allow multiplications because they can be computing index to a higher
     level dimension (PR 37861). */
  if (integer_onep (tunit))
    {
      if (is_gimple_assign (offset_def)
	  && gimple_assign_rhs_code (offset_def) == MULT_EXPR)
	return false;

      index = offset;
    }
  else
    {
      /* The statement which defines OFFSET before type conversion
         must be a simple GIMPLE_ASSIGN.  */
      if (!is_gimple_assign (offset_def))
	return false;

      /* The RHS of the statement which defines OFFSET must be a
	 multiplication of an object by the size of the array elements.
	 This implicitly verifies that the size of the array elements
	 is constant.  */
     if (gimple_assign_rhs_code (offset_def) == MULT_EXPR
	 && TREE_CODE (gimple_assign_rhs2 (offset_def)) == INTEGER_CST
	 && tree_int_cst_equal (gimple_assign_rhs2 (offset_def), tunit))
       {
	 /* The first operand to the MULT_EXPR is the desired index.  */
	 index = gimple_assign_rhs1 (offset_def);
       }
     /* If we have idx * tunit + CST * tunit re-associate that.  */
     else if ((gimple_assign_rhs_code (offset_def) == PLUS_EXPR
	       || gimple_assign_rhs_code (offset_def) == MINUS_EXPR)
	      && TREE_CODE (gimple_assign_rhs1 (offset_def)) == SSA_NAME
	      && TREE_CODE (gimple_assign_rhs2 (offset_def)) == INTEGER_CST
	      && (tmp = div_if_zero_remainder (EXACT_DIV_EXPR,
					       gimple_assign_rhs2 (offset_def),
					       tunit)) != NULL_TREE)
       {
	 gimple offset_def2 = SSA_NAME_DEF_STMT (gimple_assign_rhs1 (offset_def));
	 if (is_gimple_assign (offset_def2)
	     && gimple_assign_rhs_code (offset_def2) == MULT_EXPR
	     && TREE_CODE (gimple_assign_rhs2 (offset_def2)) == INTEGER_CST
	     && tree_int_cst_equal (gimple_assign_rhs2 (offset_def2), tunit))
	   {
	     index = fold_build2 (gimple_assign_rhs_code (offset_def),
				  TREE_TYPE (offset),
				  gimple_assign_rhs1 (offset_def2), tmp);
	   }
	 else
	   return false;
       }
     else
	return false;
    }

  /* Replace the pointer addition with array indexing.  */
  index = force_gimple_operand_gsi (use_stmt_gsi, index, true, NULL_TREE,
				    true, GSI_SAME_STMT);
  if (TREE_CODE (TREE_OPERAND (def_rhs, 0)) == ARRAY_REF)
    {
      new_rhs = unshare_expr (def_rhs);
      TREE_OPERAND (TREE_OPERAND (new_rhs, 0), 1) = index;
    }
  else
    {
      new_rhs = build4 (ARRAY_REF, TREE_TYPE (TREE_TYPE (TREE_TYPE (def_rhs))),
			unshare_expr (TREE_OPERAND (def_rhs, 0)),
			index, integer_zero_node, NULL_TREE);
      new_rhs = build_fold_addr_expr (new_rhs);
      if (!useless_type_conversion_p (TREE_TYPE (gimple_assign_lhs (use_stmt)),
				      TREE_TYPE (new_rhs)))
	{
	  new_rhs = force_gimple_operand_gsi (use_stmt_gsi, new_rhs, true,
					      NULL_TREE, true, GSI_SAME_STMT);
	  new_rhs = fold_convert (TREE_TYPE (gimple_assign_lhs (use_stmt)),
				  new_rhs);
	}
    }
  gimple_assign_set_rhs_from_tree (use_stmt_gsi, new_rhs);
  fold_stmt (use_stmt_gsi);
  tidy_after_forward_propagate_addr (gsi_stmt (*use_stmt_gsi));
  return true;
}

/* NAME is a SSA_NAME representing DEF_RHS which is of the form
   ADDR_EXPR <whatever>.

   Try to forward propagate the ADDR_EXPR into the use USE_STMT.
   Often this will allow for removal of an ADDR_EXPR and INDIRECT_REF
   node or for recovery of array indexing from pointer arithmetic.

   Return true if the propagation was successful (the propagation can
   be not totally successful, yet things may have been changed).  */

static bool
forward_propagate_addr_expr_1 (tree name, tree def_rhs,
			       gimple_stmt_iterator *use_stmt_gsi,
			       bool single_use_p)
{
  tree lhs, rhs, rhs2, array_ref;
  gimple use_stmt = gsi_stmt (*use_stmt_gsi);
  enum tree_code rhs_code;
  bool res = true;

  gcc_assert (TREE_CODE (def_rhs) == ADDR_EXPR);

  lhs = gimple_assign_lhs (use_stmt);
  rhs_code = gimple_assign_rhs_code (use_stmt);
  rhs = gimple_assign_rhs1 (use_stmt);

  /* Trivial cases.  The use statement could be a trivial copy or a
     useless conversion.  Recurse to the uses of the lhs as copyprop does
     not copy through different variant pointers and FRE does not catch
     all useless conversions.  Treat the case of a single-use name and
     a conversion to def_rhs type separate, though.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && ((rhs_code == SSA_NAME && rhs == name)
	  || CONVERT_EXPR_CODE_P (rhs_code)))
    {
      /* Only recurse if we don't deal with a single use or we cannot
	 do the propagation to the current statement.  In particular
	 we can end up with a conversion needed for a non-invariant
	 address which we cannot do in a single statement.  */
      if (!single_use_p
	  || (!useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (def_rhs))
	      && (!is_gimple_min_invariant (def_rhs)
		  || (INTEGRAL_TYPE_P (TREE_TYPE (lhs))
		      && POINTER_TYPE_P (TREE_TYPE (def_rhs))
		      && (TYPE_PRECISION (TREE_TYPE (lhs))
			  > TYPE_PRECISION (TREE_TYPE (def_rhs)))))))
	return forward_propagate_addr_expr (lhs, def_rhs);

      gimple_assign_set_rhs1 (use_stmt, unshare_expr (def_rhs));
      if (useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (def_rhs)))
	gimple_assign_set_rhs_code (use_stmt, TREE_CODE (def_rhs));
      else
	gimple_assign_set_rhs_code (use_stmt, NOP_EXPR);
      return true;
    }

  /* Propagate through constant pointer adjustments.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && rhs_code == POINTER_PLUS_EXPR
      && rhs == name
      && TREE_CODE (gimple_assign_rhs2 (use_stmt)) == INTEGER_CST)
    {
      tree new_def_rhs;
      /* As we come here with non-invariant addresses in def_rhs we need
         to make sure we can build a valid constant offsetted address
	 for further propagation.  Simply rely on fold building that
	 and check after the fact.  */
      new_def_rhs = fold_build2 (MEM_REF, TREE_TYPE (TREE_TYPE (rhs)),
				 def_rhs,
				 fold_convert (ptr_type_node,
					       gimple_assign_rhs2 (use_stmt)));
      if (TREE_CODE (new_def_rhs) == MEM_REF
	  && !is_gimple_mem_ref_addr (TREE_OPERAND (new_def_rhs, 0)))
	return false;
      new_def_rhs = build_fold_addr_expr_with_type (new_def_rhs,
						    TREE_TYPE (rhs));

      /* Recurse.  If we could propagate into all uses of lhs do not
	 bother to replace into the current use but just pretend we did.  */
      if (TREE_CODE (new_def_rhs) == ADDR_EXPR
	  && forward_propagate_addr_expr (lhs, new_def_rhs))
	return true;

      if (useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (new_def_rhs)))
	gimple_assign_set_rhs_with_ops (use_stmt_gsi, TREE_CODE (new_def_rhs),
					new_def_rhs, NULL_TREE);
      else if (is_gimple_min_invariant (new_def_rhs))
	gimple_assign_set_rhs_with_ops (use_stmt_gsi, NOP_EXPR,
					new_def_rhs, NULL_TREE);
      else
	return false;
      gcc_assert (gsi_stmt (*use_stmt_gsi) == use_stmt);
      update_stmt (use_stmt);
      return true;
    }

  /* Now strip away any outer COMPONENT_REF/ARRAY_REF nodes from the LHS.
     ADDR_EXPR will not appear on the LHS.  */
  lhs = gimple_assign_lhs (use_stmt);
  while (handled_component_p (lhs))
    lhs = TREE_OPERAND (lhs, 0);

  /* Now see if the LHS node is a MEM_REF using NAME.  If so,
     propagate the ADDR_EXPR into the use of NAME and fold the result.  */
  if (TREE_CODE (lhs) == MEM_REF
      && TREE_OPERAND (lhs, 0) == name)
    {
      tree def_rhs_base;
      HOST_WIDE_INT def_rhs_offset;
      /* If the address is invariant we can always fold it.  */
      if ((def_rhs_base = get_addr_base_and_unit_offset (TREE_OPERAND (def_rhs, 0),
							 &def_rhs_offset)))
	{
	  double_int off = mem_ref_offset (lhs);
	  tree new_ptr;
	  off = double_int_add (off,
				shwi_to_double_int (def_rhs_offset));
	  if (TREE_CODE (def_rhs_base) == MEM_REF)
	    {
	      off = double_int_add (off, mem_ref_offset (def_rhs_base));
	      new_ptr = TREE_OPERAND (def_rhs_base, 0);
	    }
	  else
	    new_ptr = build_fold_addr_expr (def_rhs_base);
	  TREE_OPERAND (lhs, 0) = new_ptr;
	  TREE_OPERAND (lhs, 1)
	    = double_int_to_tree (TREE_TYPE (TREE_OPERAND (lhs, 1)), off);
	  tidy_after_forward_propagate_addr (use_stmt);
	  /* Continue propagating into the RHS if this was not the only use.  */
	  if (single_use_p)
	    return true;
	}
      /* If the LHS is a plain dereference and the value type is the same as
         that of the pointed-to type of the address we can put the
	 dereferenced address on the LHS preserving the original alias-type.  */
      else if (gimple_assign_lhs (use_stmt) == lhs
	       && useless_type_conversion_p
	            (TREE_TYPE (TREE_OPERAND (def_rhs, 0)),
		     TREE_TYPE (gimple_assign_rhs1 (use_stmt))))
	{
	  tree *def_rhs_basep = &TREE_OPERAND (def_rhs, 0);
	  tree new_offset, new_base, saved, new_lhs;
	  while (handled_component_p (*def_rhs_basep))
	    def_rhs_basep = &TREE_OPERAND (*def_rhs_basep, 0);
	  saved = *def_rhs_basep;
	  if (TREE_CODE (*def_rhs_basep) == MEM_REF)
	    {
	      new_base = TREE_OPERAND (*def_rhs_basep, 0);
	      new_offset
		= int_const_binop (PLUS_EXPR, TREE_OPERAND (lhs, 1),
				   TREE_OPERAND (*def_rhs_basep, 1));
	    }
	  else
	    {
	      new_base = build_fold_addr_expr (*def_rhs_basep);
	      new_offset = TREE_OPERAND (lhs, 1);
	    }
	  *def_rhs_basep = build2 (MEM_REF, TREE_TYPE (*def_rhs_basep),
				   new_base, new_offset);
	  TREE_THIS_VOLATILE (*def_rhs_basep) = TREE_THIS_VOLATILE (lhs);
	  TREE_SIDE_EFFECTS (*def_rhs_basep) = TREE_SIDE_EFFECTS (lhs);
	  TREE_THIS_NOTRAP (*def_rhs_basep) = TREE_THIS_NOTRAP (lhs);
	  new_lhs = unshare_expr (TREE_OPERAND (def_rhs, 0));
	  gimple_assign_set_lhs (use_stmt, new_lhs);
	  TREE_THIS_VOLATILE (new_lhs) = TREE_THIS_VOLATILE (lhs);
	  TREE_SIDE_EFFECTS (new_lhs) = TREE_SIDE_EFFECTS (lhs);
	  *def_rhs_basep = saved;
	  tidy_after_forward_propagate_addr (use_stmt);
	  /* Continue propagating into the RHS if this was not the
	     only use.  */
	  if (single_use_p)
	    return true;
	}
      else
	/* We can have a struct assignment dereferencing our name twice.
	   Note that we didn't propagate into the lhs to not falsely
	   claim we did when propagating into the rhs.  */
	res = false;
    }

  /* Strip away any outer COMPONENT_REF, ARRAY_REF or ADDR_EXPR
     nodes from the RHS.  */
  rhs = gimple_assign_rhs1 (use_stmt);
  if (TREE_CODE (rhs) == ADDR_EXPR)
    rhs = TREE_OPERAND (rhs, 0);
  while (handled_component_p (rhs))
    rhs = TREE_OPERAND (rhs, 0);

  /* Now see if the RHS node is a MEM_REF using NAME.  If so,
     propagate the ADDR_EXPR into the use of NAME and fold the result.  */
  if (TREE_CODE (rhs) == MEM_REF
      && TREE_OPERAND (rhs, 0) == name)
    {
      tree def_rhs_base;
      HOST_WIDE_INT def_rhs_offset;
      if ((def_rhs_base = get_addr_base_and_unit_offset (TREE_OPERAND (def_rhs, 0),
							 &def_rhs_offset)))
	{
	  double_int off = mem_ref_offset (rhs);
	  tree new_ptr;
	  off = double_int_add (off,
				shwi_to_double_int (def_rhs_offset));
	  if (TREE_CODE (def_rhs_base) == MEM_REF)
	    {
	      off = double_int_add (off, mem_ref_offset (def_rhs_base));
	      new_ptr = TREE_OPERAND (def_rhs_base, 0);
	    }
	  else
	    new_ptr = build_fold_addr_expr (def_rhs_base);
	  TREE_OPERAND (rhs, 0) = new_ptr;
	  TREE_OPERAND (rhs, 1)
	    = double_int_to_tree (TREE_TYPE (TREE_OPERAND (rhs, 1)), off);
	  fold_stmt_inplace (use_stmt_gsi);
	  tidy_after_forward_propagate_addr (use_stmt);
	  return res;
	}
      /* If the RHS is a plain dereference and the value type is the same as
         that of the pointed-to type of the address we can put the
	 dereferenced address on the RHS preserving the original alias-type.  */
      else if (gimple_assign_rhs1 (use_stmt) == rhs
	       && useless_type_conversion_p
		    (TREE_TYPE (gimple_assign_lhs (use_stmt)),
		     TREE_TYPE (TREE_OPERAND (def_rhs, 0))))
	{
	  tree *def_rhs_basep = &TREE_OPERAND (def_rhs, 0);
	  tree new_offset, new_base, saved, new_rhs;
	  while (handled_component_p (*def_rhs_basep))
	    def_rhs_basep = &TREE_OPERAND (*def_rhs_basep, 0);
	  saved = *def_rhs_basep;
	  if (TREE_CODE (*def_rhs_basep) == MEM_REF)
	    {
	      new_base = TREE_OPERAND (*def_rhs_basep, 0);
	      new_offset
		= int_const_binop (PLUS_EXPR, TREE_OPERAND (rhs, 1),
				   TREE_OPERAND (*def_rhs_basep, 1));
	    }
	  else
	    {
	      new_base = build_fold_addr_expr (*def_rhs_basep);
	      new_offset = TREE_OPERAND (rhs, 1);
	    }
	  *def_rhs_basep = build2 (MEM_REF, TREE_TYPE (*def_rhs_basep),
				   new_base, new_offset);
	  TREE_THIS_VOLATILE (*def_rhs_basep) = TREE_THIS_VOLATILE (rhs);
	  TREE_SIDE_EFFECTS (*def_rhs_basep) = TREE_SIDE_EFFECTS (rhs);
	  TREE_THIS_NOTRAP (*def_rhs_basep) = TREE_THIS_NOTRAP (rhs);
	  new_rhs = unshare_expr (TREE_OPERAND (def_rhs, 0));
	  gimple_assign_set_rhs1 (use_stmt, new_rhs);
	  TREE_THIS_VOLATILE (new_rhs) = TREE_THIS_VOLATILE (rhs);
	  TREE_SIDE_EFFECTS (new_rhs) = TREE_SIDE_EFFECTS (rhs);
	  *def_rhs_basep = saved;
	  fold_stmt_inplace (use_stmt_gsi);
	  tidy_after_forward_propagate_addr (use_stmt);
	  return res;
	}
    }

  /* If the use of the ADDR_EXPR is not a POINTER_PLUS_EXPR, there
     is nothing to do. */
  if (gimple_assign_rhs_code (use_stmt) != POINTER_PLUS_EXPR
      || gimple_assign_rhs1 (use_stmt) != name)
    return false;

  /* The remaining cases are all for turning pointer arithmetic into
     array indexing.  They only apply when we have the address of
     element zero in an array.  If that is not the case then there
     is nothing to do.  */
  array_ref = TREE_OPERAND (def_rhs, 0);
  if ((TREE_CODE (array_ref) != ARRAY_REF
       || TREE_CODE (TREE_TYPE (TREE_OPERAND (array_ref, 0))) != ARRAY_TYPE
       || TREE_CODE (TREE_OPERAND (array_ref, 1)) != INTEGER_CST)
      && TREE_CODE (TREE_TYPE (array_ref)) != ARRAY_TYPE)
    return false;

  rhs2 = gimple_assign_rhs2 (use_stmt);
  /* Optimize &x[C1] p+ C2 to  &x p+ C3 with C3 = C1 * element_size + C2.  */
  if (TREE_CODE (rhs2) == INTEGER_CST)
    {
      tree new_rhs = build1_loc (gimple_location (use_stmt),
				 ADDR_EXPR, TREE_TYPE (def_rhs),
				 fold_build2 (MEM_REF,
					      TREE_TYPE (TREE_TYPE (def_rhs)),
					      unshare_expr (def_rhs),
					      fold_convert (ptr_type_node,
							    rhs2)));
      gimple_assign_set_rhs_from_tree (use_stmt_gsi, new_rhs);
      use_stmt = gsi_stmt (*use_stmt_gsi);
      update_stmt (use_stmt);
      tidy_after_forward_propagate_addr (use_stmt);
      return true;
    }

  /* Try to optimize &x[0] p+ OFFSET where OFFSET is defined by
     converting a multiplication of an index by the size of the
     array elements, then the result is converted into the proper
     type for the arithmetic.  */
  if (TREE_CODE (rhs2) == SSA_NAME
      && (TREE_CODE (array_ref) != ARRAY_REF
	  || integer_zerop (TREE_OPERAND (array_ref, 1)))
      && useless_type_conversion_p (TREE_TYPE (name), TREE_TYPE (def_rhs))
      /* Avoid problems with IVopts creating PLUS_EXPRs with a
	 different type than their operands.  */
      && useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (def_rhs)))
    return forward_propagate_addr_into_variable_array_index (rhs2, def_rhs,
							     use_stmt_gsi);
  return false;
}

/* STMT is a statement of the form SSA_NAME = ADDR_EXPR <whatever>.

   Try to forward propagate the ADDR_EXPR into all uses of the SSA_NAME.
   Often this will allow for removal of an ADDR_EXPR and INDIRECT_REF
   node or for recovery of array indexing from pointer arithmetic.
   Returns true, if all uses have been propagated into.  */

static bool
forward_propagate_addr_expr (tree name, tree rhs)
{
  int stmt_loop_depth = gimple_bb (SSA_NAME_DEF_STMT (name))->loop_depth;
  imm_use_iterator iter;
  gimple use_stmt;
  bool all = true;
  bool single_use_p = has_single_use (name);

  FOR_EACH_IMM_USE_STMT (use_stmt, iter, name)
    {
      bool result;
      tree use_rhs;

      /* If the use is not in a simple assignment statement, then
	 there is nothing we can do.  */
      if (gimple_code (use_stmt) != GIMPLE_ASSIGN)
	{
	  if (!is_gimple_debug (use_stmt))
	    all = false;
	  continue;
	}

      /* If the use is in a deeper loop nest, then we do not want
	 to propagate non-invariant ADDR_EXPRs into the loop as that
	 is likely adding expression evaluations into the loop.  */
      if (gimple_bb (use_stmt)->loop_depth > stmt_loop_depth
	  && !is_gimple_min_invariant (rhs))
	{
	  all = false;
	  continue;
	}

      {
	gimple_stmt_iterator gsi = gsi_for_stmt (use_stmt);
	result = forward_propagate_addr_expr_1 (name, rhs, &gsi,
						single_use_p);
	/* If the use has moved to a different statement adjust
	   the update machinery for the old statement too.  */
	if (use_stmt != gsi_stmt (gsi))
	  {
	    update_stmt (use_stmt);
	    use_stmt = gsi_stmt (gsi);
	  }

	update_stmt (use_stmt);
      }
      all &= result;

      /* Remove intermediate now unused copy and conversion chains.  */
      use_rhs = gimple_assign_rhs1 (use_stmt);
      if (result
	  && TREE_CODE (gimple_assign_lhs (use_stmt)) == SSA_NAME
	  && TREE_CODE (use_rhs) == SSA_NAME
	  && has_zero_uses (gimple_assign_lhs (use_stmt)))
	{
	  gimple_stmt_iterator gsi = gsi_for_stmt (use_stmt);
	  release_defs (use_stmt);
	  gsi_remove (&gsi, true);
	}
    }

  return all && has_zero_uses (name);
}


/* Forward propagate the comparison defined in STMT like
   cond_1 = x CMP y to uses of the form
     a_1 = (T')cond_1
     a_1 = !cond_1
     a_1 = cond_1 != 0
   Returns true if stmt is now unused.  */

static bool
forward_propagate_comparison (gimple stmt)
{
  tree name = gimple_assign_lhs (stmt);
  gimple use_stmt;
  tree tmp = NULL_TREE;
  gimple_stmt_iterator gsi;
  enum tree_code code;
  tree lhs;

  /* Don't propagate ssa names that occur in abnormal phis.  */
  if ((TREE_CODE (gimple_assign_rhs1 (stmt)) == SSA_NAME
       && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (gimple_assign_rhs1 (stmt)))
      || (TREE_CODE (gimple_assign_rhs2 (stmt)) == SSA_NAME
        && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (gimple_assign_rhs2 (stmt))))
    return false;

  /* Do not un-cse comparisons.  But propagate through copies.  */
  use_stmt = get_prop_dest_stmt (name, &name);
  if (!use_stmt
      || !is_gimple_assign (use_stmt))
    return false;

  code = gimple_assign_rhs_code (use_stmt);
  lhs = gimple_assign_lhs (use_stmt);
  if (!INTEGRAL_TYPE_P (TREE_TYPE (lhs)))
    return false;

  /* We can propagate the condition into a statement that
     computes the logical negation of the comparison result.  */
  if ((code == BIT_NOT_EXPR
       && TYPE_PRECISION (TREE_TYPE (lhs)) == 1)
      || (code == BIT_XOR_EXPR
	  && integer_onep (gimple_assign_rhs2 (use_stmt))))
    {
      tree type = TREE_TYPE (gimple_assign_rhs1 (stmt));
      bool nans = HONOR_NANS (TYPE_MODE (type));
      enum tree_code inv_code;
      inv_code = invert_tree_comparison (gimple_assign_rhs_code (stmt), nans);
      if (inv_code == ERROR_MARK)
	return false;

      tmp = build2 (inv_code, TREE_TYPE (lhs), gimple_assign_rhs1 (stmt),
		    gimple_assign_rhs2 (stmt));
    }
  else
    return false;

  gsi = gsi_for_stmt (use_stmt);
  gimple_assign_set_rhs_from_tree (&gsi, unshare_expr (tmp));
  use_stmt = gsi_stmt (gsi);
  update_stmt (use_stmt);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  Replaced '");
      print_gimple_expr (dump_file, stmt, 0, dump_flags);
      fprintf (dump_file, "' with '");
      print_gimple_expr (dump_file, use_stmt, 0, dump_flags);
      fprintf (dump_file, "'\n");
    }

  /* Remove defining statements.  */
  return remove_prop_source_from_use (name);
}

/* Implement a simple nonzero function, just return
   the integer value if it is known.  */
static double_int
forward_prop_nonzero (tree val)
{
  if (TREE_CODE (val) == INTEGER_CST)
    return tree_to_double_int (val);
  return double_int_minus_one;
}

/* Delete possible dead code in BB which might have been caused
   unused by ssa_combine until statement UNTIL. */
static void
delete_dead_code_uptil (basic_block bb, gimple until)
{
  gimple_stmt_iterator gsi;
  for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi);)
    {
      gimple stmt = gsi_stmt (gsi);
      if (stmt == until)
	return;
      if (gimple_get_lhs (stmt)
	  && TREE_CODE (gimple_get_lhs (stmt)) == SSA_NAME
	  && has_zero_uses (gimple_get_lhs (stmt))
	  && !stmt_could_throw_p (stmt)
	  && !gimple_has_side_effects (stmt))
	{
	  gimple_stmt_iterator i2;
	  if (dump_file && dump_flags & TDF_DETAILS)
	    {
	      fprintf (dump_file, "Removing dead stmt ");
	      print_gimple_stmt (dump_file, stmt, 0, 0);
	      fprintf (dump_file, "\n");
	    }
	  i2 = gsi;
	  gsi_next (&gsi);
	  gsi_remove (&i2, true);
	  release_defs (stmt);
	  continue;
	}
      gsi_next (&gsi);
    }
}

/* Main entry point for the forward propagation and statement combine
   optimizer.  */

static unsigned int
ssa_forward_propagate_and_combine (void)
{
  basic_block bb;
  unsigned int todoflags = 0;

  cfg_changed = false;

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator gsi;

      /* Apply forward propagation to all stmts in the basic-block.
	 Note we update GSI within the loop as necessary.  */
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); )
	{
	  gimple stmt = gsi_stmt (gsi);
	  tree lhs, rhs;
	  enum tree_code code;

	  if (!is_gimple_assign (stmt))
	    {
	      gsi_next (&gsi);
	      continue;
	    }

	  lhs = gimple_assign_lhs (stmt);
	  rhs = gimple_assign_rhs1 (stmt);
	  code = gimple_assign_rhs_code (stmt);
	  if (TREE_CODE (lhs) != SSA_NAME
	      || has_zero_uses (lhs))
	    {
	      gsi_next (&gsi);
	      continue;
	    }

	  /* If this statement sets an SSA_NAME to an address,
	     try to propagate the address into the uses of the SSA_NAME.  */
	  if (code == ADDR_EXPR
	      /* Handle pointer conversions on invariant addresses
		 as well, as this is valid gimple.  */
	      || (CONVERT_EXPR_CODE_P (code)
		  && TREE_CODE (rhs) == ADDR_EXPR
		  && POINTER_TYPE_P (TREE_TYPE (lhs))))
	    {
	      tree base = get_base_address (TREE_OPERAND (rhs, 0));
	      if ((!base
		   || !DECL_P (base)
		   || decl_address_invariant_p (base))
		  && !stmt_references_abnormal_ssa_name (stmt)
		  && forward_propagate_addr_expr (lhs, rhs))
		{
		  release_defs (stmt);
		  todoflags |= TODO_remove_unused_locals;
		  gsi_remove (&gsi, true);
		}
	      else
		gsi_next (&gsi);
	    }
	  else if (code == POINTER_PLUS_EXPR)
	    {
	      tree off = gimple_assign_rhs2 (stmt);
	      if (TREE_CODE (off) == INTEGER_CST
		  && can_propagate_from (stmt)
		  && !simple_iv_increment_p (stmt)
		  /* ???  Better adjust the interface to that function
		     instead of building new trees here.  */
		  && forward_propagate_addr_expr
		       (lhs,
			build1_loc (gimple_location (stmt),
				    ADDR_EXPR, TREE_TYPE (rhs),
				    fold_build2 (MEM_REF,
						 TREE_TYPE (TREE_TYPE (rhs)),
						 rhs,
						 fold_convert (ptr_type_node,
							       off)))))
		{
		  release_defs (stmt);
		  todoflags |= TODO_remove_unused_locals;
		  gsi_remove (&gsi, true);
		}
	      else if (is_gimple_min_invariant (rhs))
		{
		  /* Make sure to fold &a[0] + off_1 here.  */
		  fold_stmt_inplace (&gsi);
		  update_stmt (stmt);
		  if (gimple_assign_rhs_code (stmt) == POINTER_PLUS_EXPR)
		    gsi_next (&gsi);
		}
	      else
		gsi_next (&gsi);
	    }
	  else if (TREE_CODE_CLASS (code) == tcc_comparison)
	    {
	      if (forward_propagate_comparison (stmt))
	        cfg_changed = true;
	      gsi_next (&gsi);
	    }
	  else
	    gsi_next (&gsi);
	}

      /* Combine stmts with the stmts defining their operands.
	 Note we update GSI within the loop as necessary.  */
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi);)
	{
	  bool did_replace = false;
	  gimple stmt = gsi_stmt (gsi);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Folding statement: ");
	      print_gimple_stmt (dump_file, stmt, 0, TDF_SLIM);
	    }

	  if (ssa_combine (&gsi, forward_prop_nonzero))
	    {
	      cfg_changed = true;
	      did_replace = true;
	      delete_dead_code_uptil (bb, stmt);
	      if (gsi_end_p (gsi))
		gsi = gsi_start_bb (bb);
	      else
	 	{
		  gsi_prev_nondebug (&gsi);
		  if (gsi_end_p (gsi))
		    gsi = gsi_start_bb (bb);
		}
	    }
	  else
	    gsi_next (&gsi);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      if (did_replace)
		{
		  fprintf (dump_file, "Folded into: ");
		  print_gimple_stmt (dump_file, stmt, 0, TDF_SLIM);
		  fprintf (dump_file, "\n");
		}
	      else
		fprintf (dump_file, "Not folded\n");
	    }
	}
    }

  if (cfg_changed)
    todoflags |= TODO_cleanup_cfg;

  return todoflags;
}


static bool
gate_forwprop (void)
{
  return flag_tree_forwprop;
}

struct gimple_opt_pass pass_forwprop =
{
 {
  GIMPLE_PASS,
  "forwprop",			/* name */
  gate_forwprop,		/* gate */
  ssa_forward_propagate_and_combine,	/* execute */
  NULL,				/* sub */
  NULL,				/* next */
  0,				/* static_pass_number */
  TV_TREE_FORWPROP,		/* tv_id */
  PROP_cfg | PROP_ssa,		/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  TODO_ggc_collect
  | TODO_update_ssa
  | TODO_verify_ssa		/* todo_flags_finish */
 }
};
