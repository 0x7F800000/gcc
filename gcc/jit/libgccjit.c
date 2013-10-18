/* Implementation of the C API; all wrappers into a C++ API */
#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"

#include "libgccjit.h"
#include "internal-api.h"

struct gcc_jit_context : public gcc::jit::context
{
};

struct gcc_jit_result : public gcc::jit::result
{
};

struct gcc_jit_location : public gcc::jit::location
{
};

struct gcc_jit_type : public gcc::jit::type
{
};

struct gcc_jit_field : public gcc::jit::field
{
};

struct gcc_jit_function : public gcc::jit::function
{
};

struct gcc_jit_label : public gcc::jit::label
{
};

struct gcc_jit_rvalue : public gcc::jit::rvalue
{
};

struct gcc_jit_lvalue : public gcc::jit::lvalue
{
};

struct gcc_jit_param : public gcc::jit::param
{
};

struct gcc_jit_loop : public gcc::jit::loop
{
};

/**********************************************************************
 Error-handling.

 We try to gracefully handle API usage errors by being defensive
 at the API boundary.
 **********************************************************************/

#define JIT_BEGIN_STMT do {
#define JIT_END_STMT   } while(0)

/* TODO: mark failure branches as unlikely? */

#define RETURN_VAL_IF_FAIL(TEST_EXPR, RETURN_EXPR, CTXT, ERR_MSG)	\
  JIT_BEGIN_STMT							\
    if (!(TEST_EXPR))							\
      {								\
	jit_error ((CTXT), "%s: %s", __func__, (ERR_MSG));		\
	return (RETURN_EXPR);						\
      }								\
  JIT_END_STMT

#define RETURN_NULL_IF_FAIL(TEST_EXPR, CTXT, ERR_MSG) \
  RETURN_VAL_IF_FAIL ((TEST_EXPR), NULL, (CTXT), (ERR_MSG))

#define RETURN_IF_FAIL(TEST_EXPR, CTXT, ERR_MSG)			\
  JIT_BEGIN_STMT							\
    if (!(TEST_EXPR))							\
      {								\
	jit_error ((CTXT), "%s: %s", __func__, (ERR_MSG));		\
	return;							\
      }								\
  JIT_END_STMT

/* Check that CTXT is non-NULL, and that is is before the callback.  */
#define RETURN_IF_NOT_INITIAL_CTXT(CTXT)			\
  JIT_BEGIN_STMT						\
    RETURN_IF_FAIL ((CTXT), (CTXT), "NULL context");		\
    RETURN_IF_FAIL (!(CTXT)->within_code_factory (),		\
		    (CTXT),					\
		    ("erroneously used within code factory"	\
		     " callback"));				\
  JIT_END_STMT

#define RETURN_NULL_IF_NOT_INITIAL_CTXT(CTXT)				\
  JIT_BEGIN_STMT							\
    RETURN_NULL_IF_FAIL ((CTXT), (CTXT), "NULL context");		\
    RETURN_NULL_IF_FAIL (!(CTXT)->within_code_factory (),		\
			 (CTXT),					\
			 ("erroneously used within code factory"	\
			  " callback"));				\
  JIT_END_STMT

/* Check that CTXT is non-NULL, and that is is within the callback.  */
#define RETURN_NULL_IF_NOT_CALLBACK_CTXT(CTXT)				\
  JIT_BEGIN_STMT							\
    RETURN_NULL_IF_FAIL ((CTXT), (CTXT), "NULL context");		\
    RETURN_NULL_IF_FAIL ((CTXT)->within_code_factory (),		\
			 (CTXT),					\
			 ("erroneously used outside of code factory"	\
			  " callback"));				\
  JIT_END_STMT

/* Check that FUNC is non-NULL, and that it's OK to add statements to
   it.  */
#define RETURN_IF_NOT_FUNC_DEFINITION(FUNC) \
  JIT_BEGIN_STMT							\
    RETURN_IF_FAIL ((FUNC), NULL, "NULL function");			\
    RETURN_IF_FAIL ((FUNC)->get_kind () != GCC_JIT_FUNCTION_IMPORTED,	\
		    NULL,						\
		    "Cannot add code to an imported function");	\
  JIT_END_STMT

#define RETURN_NULL_IF_NOT_FUNC_DEFINITION(FUNC) \
  JIT_BEGIN_STMT							\
    RETURN_NULL_IF_FAIL ((FUNC), NULL, "NULL function");		\
    RETURN_NULL_IF_FAIL ((FUNC)->get_kind () != GCC_JIT_FUNCTION_IMPORTED,\
			 NULL,						\
			 "Cannot add code to an imported function");	\
  JIT_END_STMT

static void
jit_error (gcc_jit_context *ctxt, const char *fmt, ...)
  GNU_PRINTF(2, 3);

static void
jit_error (gcc_jit_context *ctxt, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);

  /* If no context was given/known, try to report it on the active
     context, if any.  */
  if (!ctxt)
    ctxt = (gcc_jit_context *)gcc::jit::active_jit_ctxt;

  if (ctxt)
    ctxt->add_error_va (fmt, ap);
  else
    {
      /* No context?  Send to stderr.  */
      vfprintf (stderr, "%s\n", ap);
    }

  va_end (ap);
}

gcc_jit_context *
gcc_jit_context_acquire (void)
{
  return new gcc_jit_context ();
}

void
gcc_jit_context_release (gcc_jit_context *ctxt)
{
  delete ctxt;
}

void
gcc_jit_context_set_code_factory (gcc_jit_context *ctxt,
				  gcc_jit_code_callback cb,
				  void *user_data)
{
  RETURN_IF_NOT_INITIAL_CTXT (ctxt);
  RETURN_IF_FAIL (cb, ctxt, "NULL callback");
  /* user_data can be anything.  */

  ctxt->set_code_factory (cb, user_data);
}

/**********************************************************************
 Functions for use within the code factory.
 **********************************************************************/
gcc_jit_location *
gcc_jit_context_new_location (gcc_jit_context *ctxt,
			      const char *filename,
			      int line,
			      int column)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);

  return (gcc_jit_location *)ctxt->new_location (filename, line, column);
}

gcc_jit_type *
gcc_jit_context_get_type (gcc_jit_context *ctxt,
			  enum gcc_jit_types type)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  /* The inner function checks "type" for us.  */

  return (gcc_jit_type *)ctxt->get_type (type);
}

gcc_jit_type *
gcc_jit_type_get_pointer (gcc_jit_type *type)
{
  RETURN_NULL_IF_FAIL (type, NULL, "NULL type");
  /* can't check for WITHIN_CALLBACK */

  return (gcc_jit_type *)type->get_pointer ();
}

gcc_jit_type *
gcc_jit_type_get_const (gcc_jit_type *type)
{
  RETURN_NULL_IF_FAIL (type, NULL, "NULL type");
  /* can't check for WITHIN_CALLBACK */

  return (gcc_jit_type *)type->get_const ();
}

gcc_jit_field *
gcc_jit_context_new_field (gcc_jit_context *ctxt,
			   gcc_jit_location *loc,
			   gcc_jit_type *type,
			   const char *name)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");
  RETURN_NULL_IF_FAIL (name, ctxt, "NULL name");

  return (gcc_jit_field *)ctxt->new_field (loc, type, name);
}

gcc_jit_type *
gcc_jit_context_new_struct_type (gcc_jit_context *ctxt,
				 gcc_jit_location *loc,
				 const char *name,
				 int num_fields,
				 gcc_jit_field **fields)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (name, ctxt, "NULL name");
  if (num_fields)
    RETURN_NULL_IF_FAIL (fields, ctxt, "NULL fields ptr");
  for (int i = 0; i < num_fields; i++)
    RETURN_NULL_IF_FAIL (fields[i], ctxt, "NULL field ptr");

  return (gcc_jit_type *)ctxt->new_struct_type (loc, name, num_fields,
						(gcc::jit::field **)fields);
}


/* Constructing functions.  */
gcc_jit_param *
gcc_jit_context_new_param (gcc_jit_context *ctxt,
			   gcc_jit_location *loc,
			   gcc_jit_type *type,
			   const char *name)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");
  RETURN_NULL_IF_FAIL (name, ctxt, "NULL name");

  return (gcc_jit_param *)ctxt->new_param (loc, type, name);
}

gcc_jit_lvalue *
gcc_jit_param_as_lvalue (gcc_jit_param *param)
{
  RETURN_NULL_IF_FAIL (param, NULL, "NULL param");
  /* can't check for WITHIN_CALLBACK */

  return (gcc_jit_lvalue *)param->as_lvalue ();
}

gcc_jit_rvalue *
gcc_jit_param_as_rvalue (gcc_jit_param *param)
{
  RETURN_NULL_IF_FAIL (param, NULL, "NULL param");
  /* can't check for WITHIN_CALLBACK */

  return (gcc_jit_rvalue *)param->as_rvalue ();
}

gcc_jit_function *
gcc_jit_context_new_function (gcc_jit_context *ctxt,
			      gcc_jit_location *loc,
			      enum gcc_jit_function_kind kind,
			      gcc_jit_type *return_type,
			      const char *name,
			      int num_params,
			      gcc_jit_param **params,
			      int is_variadic)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (return_type, ctxt, "NULL return_type");
  RETURN_NULL_IF_FAIL (name, ctxt, "NULL name");
  RETURN_NULL_IF_FAIL ((num_params == 0) || params, ctxt, "NULL params");
  for (int i = 0; i < num_params; i++)
    if (!params[i])
      {
	jit_error (ctxt, "%s: NULL parameter %i", __func__, i);
	return NULL;
      }

  return (gcc_jit_function*)
    ctxt->new_function (loc, kind, return_type, name,
			num_params,
			(gcc::jit::param **)params,
			is_variadic);
}

gcc_jit_label*
gcc_jit_function_new_forward_label (gcc_jit_function *func,
				    const char *name)
{
  /* can't check for WITHIN_CALLBACK */
  RETURN_NULL_IF_FAIL (func, NULL, "NULL function");
  /* name can be NULL.  */

  return (gcc_jit_label *)func->new_forward_label (name);
}

gcc_jit_lvalue *
gcc_jit_context_new_global (gcc_jit_context *ctxt,
			    gcc_jit_location *loc,
			    gcc_jit_type *type,
			    const char *name)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");
  RETURN_NULL_IF_FAIL (name, ctxt, "NULL name");

  return (gcc_jit_lvalue *)ctxt->new_global (loc, type, name);
}

gcc_jit_rvalue *
gcc_jit_lvalue_as_rvalue (gcc_jit_lvalue *lvalue)
{
  RETURN_NULL_IF_FAIL (lvalue, NULL, "NULL lvalue");
  /* can't check for WITHIN_CALLBACK */

  return (gcc_jit_rvalue *)lvalue->as_rvalue ();
}

gcc_jit_rvalue *
gcc_jit_context_new_rvalue_from_int (gcc_jit_context *ctxt,
				     gcc_jit_type *type,
				     int value)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");

  return (gcc_jit_rvalue *)ctxt->new_rvalue_from_int (type, value);
}

gcc_jit_rvalue *
gcc_jit_context_zero (gcc_jit_context *ctxt,
		      gcc_jit_type *type)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");

  return gcc_jit_context_new_rvalue_from_int (ctxt, type, 0);
}

gcc_jit_rvalue *
gcc_jit_context_one (gcc_jit_context *ctxt,
		     gcc_jit_type *type)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");

  return gcc_jit_context_new_rvalue_from_int (ctxt, type, 1);
}

gcc_jit_rvalue *
gcc_jit_context_new_rvalue_from_double (gcc_jit_context *ctxt,
					gcc_jit_type *type,
					double value)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");

  return (gcc_jit_rvalue *)ctxt->new_rvalue_from_double (type, value);
}

gcc_jit_rvalue *
gcc_jit_context_new_rvalue_from_ptr (gcc_jit_context *ctxt,
				     gcc_jit_type *type,
				     void *value)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (type, ctxt, "NULL type");

  return (gcc_jit_rvalue *)ctxt->new_rvalue_from_ptr (type, value);
}

gcc_jit_rvalue *
gcc_jit_context_new_string_literal (gcc_jit_context *ctxt,
				    const char *value)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (value, ctxt, "NULL value");

  return (gcc_jit_rvalue *)ctxt->new_string_literal (value);
}

gcc_jit_rvalue *
gcc_jit_context_new_binary_op (gcc_jit_context *ctxt,
			       gcc_jit_location *loc,
			       enum gcc_jit_binary_op op,
			       gcc_jit_type *result_type,
			       gcc_jit_rvalue *a, gcc_jit_rvalue *b)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  /* op is checked by the inner function.  */
  RETURN_NULL_IF_FAIL (result_type, ctxt, "NULL result_type");
  RETURN_NULL_IF_FAIL (a, ctxt, "NULL a");
  RETURN_NULL_IF_FAIL (b, ctxt, "NULL b");

  return (gcc_jit_rvalue *)ctxt->new_binary_op (loc, op, result_type, a, b);
}

gcc_jit_rvalue *
gcc_jit_context_new_comparison (gcc_jit_context *ctxt,
				gcc_jit_location *loc,
				enum gcc_jit_comparison op,
				gcc_jit_rvalue *a, gcc_jit_rvalue *b)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  /* op is checked by the inner function.  */
  RETURN_NULL_IF_FAIL (a, ctxt, "NULL a");
  RETURN_NULL_IF_FAIL (b, ctxt, "NULL b");

  return (gcc_jit_rvalue *)ctxt->new_comparison (loc, op, a, b);
}

gcc_jit_rvalue *
gcc_jit_context_new_call (gcc_jit_context *ctxt,
			  gcc_jit_location *loc,
			  gcc_jit_function *func,
			  int numargs , gcc_jit_rvalue **args)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (func, ctxt, "NULL function");
  if (numargs)
    RETURN_NULL_IF_FAIL (args, ctxt, "NULL args");

  return (gcc_jit_rvalue *)ctxt->new_call (loc,
					   func,
					   numargs,
					   (gcc::jit::rvalue **)args);
}

extern gcc_jit_rvalue *
gcc_jit_context_new_array_lookup (gcc_jit_context *ctxt,
				  gcc_jit_location *loc,
				  gcc_jit_rvalue *ptr,
				  gcc_jit_rvalue *index)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (ptr, ctxt, "NULL ptr");
  RETURN_NULL_IF_FAIL (index, ctxt, "NULL index");

  return (gcc_jit_rvalue *)ctxt->new_array_lookup (loc, ptr, index);
}

extern gcc_jit_lvalue *
gcc_jit_context_new_field_access (gcc_jit_context *ctxt,
				  gcc_jit_location *loc,
				  gcc_jit_rvalue *ptr_or_struct,
				  const char *fieldname)
{
  RETURN_NULL_IF_NOT_CALLBACK_CTXT (ctxt);
  RETURN_NULL_IF_FAIL (ptr_or_struct, ctxt, "NULL ptr_or_struct");
  RETURN_NULL_IF_FAIL (fieldname, ctxt, "NULL fieldname");

  return (gcc_jit_lvalue *)ctxt->new_field_access (loc, ptr_or_struct, fieldname);
}

gcc_jit_lvalue *
gcc_jit_function_new_local (gcc_jit_function *func,
			    gcc_jit_location *loc,
			    gcc_jit_type *type,
			    const char *name)
{
  RETURN_NULL_IF_NOT_FUNC_DEFINITION (func);
  RETURN_NULL_IF_FAIL (type, NULL, "NULL type");
  RETURN_NULL_IF_FAIL (name, NULL, "NULL name");

  return (gcc_jit_lvalue *)func->new_local (loc, type, name);
}

gcc_jit_label *
gcc_jit_function_add_label (gcc_jit_function *func,
			    gcc_jit_location *loc,
			    const char *name)
{
  RETURN_NULL_IF_NOT_FUNC_DEFINITION (func);
  /* loc and name can be NULL.  */

  return (gcc_jit_label *)func->add_label (loc, name);
}

void
gcc_jit_function_place_forward_label (gcc_jit_function *func,
				      gcc_jit_location *loc,
				      gcc_jit_label *lab)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (lab, NULL, "NULL label");

  func->place_forward_label (loc, lab);
}

void
gcc_jit_function_add_eval (gcc_jit_function *func,
			   gcc_jit_location *loc,
			   gcc_jit_rvalue *rvalue)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (rvalue, NULL, "NULL rvalue");

  return func->add_eval (loc, rvalue);
}

void
gcc_jit_function_add_assignment (gcc_jit_function *func,
				 gcc_jit_location *loc,
				 gcc_jit_lvalue *lvalue,
				 gcc_jit_rvalue *rvalue)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (lvalue, NULL, "NULL lvalue");
  RETURN_IF_FAIL (rvalue, NULL, "NULL rvalue");

  return func->add_assignment (loc, lvalue, rvalue);
}

void
gcc_jit_function_add_assignment_op (gcc_jit_function *func,
				    gcc_jit_location *loc,
				    gcc_jit_lvalue *lvalue,
				    enum gcc_jit_binary_op op,
				    gcc_jit_rvalue *rvalue)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (lvalue, NULL, "NULL lvalue");
  /* op is checked by new_binary_op */
  RETURN_IF_FAIL (rvalue, NULL, "NULL rvalue");

  gcc_jit_type *type = (gcc_jit_type *)lvalue->get_type ();
  gcc_jit_function_add_assignment (
    func, loc,
    lvalue,
    gcc_jit_context_new_binary_op (
      (gcc_jit_context *)func->m_ctxt,
      loc, op, type,
      gcc_jit_lvalue_as_rvalue (lvalue),
      rvalue));
}

void
gcc_jit_function_add_conditional (gcc_jit_function *func,
				  gcc_jit_location *loc,
				  gcc_jit_rvalue *boolval,
				  gcc_jit_label *on_true,
				  gcc_jit_label *on_false)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (boolval, NULL, "NULL boolval");
  RETURN_IF_FAIL (on_true, NULL, "NULL on_true");
  /* on_false can be NULL */

  return func->add_conditional (loc, boolval, on_true, on_false);
}

void
gcc_jit_function_add_jump (gcc_jit_function *func,
			gcc_jit_location *loc,
			gcc_jit_label *target)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (target, NULL, "NULL target");

  func->add_jump (loc, target);
}

void
gcc_jit_function_add_return (gcc_jit_function *func,
			     gcc_jit_location *loc,
			     gcc_jit_rvalue *rvalue)
{
  RETURN_IF_NOT_FUNC_DEFINITION (func);
  RETURN_IF_FAIL (rvalue, NULL, "NULL rvalue");

  return func->add_return (loc, rvalue);
}

gcc_jit_loop *
gcc_jit_function_new_loop (gcc_jit_function *func,
			   gcc_jit_location *loc,
			   gcc_jit_rvalue *boolval)
{
  RETURN_NULL_IF_NOT_FUNC_DEFINITION (func);
  RETURN_NULL_IF_FAIL (boolval, NULL, "NULL boolval");

  return (gcc_jit_loop *)func->new_loop (loc, boolval);
}

void
gcc_jit_loop_end (gcc_jit_loop *loop,
		  gcc_jit_location *loc)
{
  RETURN_IF_FAIL (loop, NULL, "NULL loop");

  loop->end (loc);
}

/**********************************************************************
 Option-management
 **********************************************************************/

void
gcc_jit_context_set_str_option (gcc_jit_context *ctxt,
				enum gcc_jit_str_option opt,
				const char *value)
{
  RETURN_IF_NOT_INITIAL_CTXT (ctxt);
  /* opt is checked by the inner function.
     value can be NULL.  */

  ctxt->set_str_option (opt, value);
}

void
gcc_jit_context_set_int_option (gcc_jit_context *ctxt,
				enum gcc_jit_int_option opt,
				int value)
{
  RETURN_IF_NOT_INITIAL_CTXT (ctxt);
  /* opt is checked by the inner function.  */

  ctxt->set_int_option (opt, value);
}

void
gcc_jit_context_set_bool_option (gcc_jit_context *ctxt,
				 enum gcc_jit_bool_option opt,
				 int value)
{
  RETURN_IF_NOT_INITIAL_CTXT (ctxt);
  /* opt is checked by the inner function.  */

  ctxt->set_bool_option (opt, value);
}

gcc_jit_result *
gcc_jit_context_compile (gcc_jit_context *ctxt)
{
  RETURN_NULL_IF_NOT_INITIAL_CTXT (ctxt);

  return (gcc_jit_result *)ctxt->compile ();
}

void *
gcc_jit_result_get_code (gcc_jit_result *result,
			 const char *fnname)
{
  RETURN_NULL_IF_FAIL (result, NULL, "NULL result");
  RETURN_NULL_IF_FAIL (fnname, NULL, "NULL fnname");

  return result->get_code (fnname);
}

void
gcc_jit_result_release (gcc_jit_result *result)
{
  RETURN_IF_FAIL (result, NULL, "NULL result");

  delete result;
}
