#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libgccjit.h"

#include "harness.h"

int
code_making_callback (gcc_jit_context *ctxt, void *user_data)
{
  const int FIRST_LINE = __LINE__ + 4;
  /* Let's try to inject the equivalent of:
0000000001111111111222222222233333333334444444444555555555566666666667
1234567890123456789012345678901234567890123456789012345678901234567890
FIRST_LINE + 0: int
FIRST_LINE + 1: my_fibonacci (int x)
FIRST_LINE + 2: {
FIRST_LINE + 3:   if (x < 2)
FIRST_LINE + 4:     return x;
FIRST_LINE + 5:   else
FIRST_LINE + 6:     return my_fibonacci (x - 1) + my_fibonacci (x - 2);
FIRST_LINE + 7: }
0000000001111111111222222222233333333334444444444555555555566666666667
1234567890123456789012345678901234567890123456789012345678901234567890

     where the source locations are set up to point to the commented-out
     code above.
     It should therefore be possible to step through the generated code
     in the debugger, stepping through the above commented-out code
     fragement.
   */
  gcc_jit_type *the_type = gcc_jit_context_get_int_type (ctxt);
  gcc_jit_type *return_type = the_type;

  gcc_jit_param *x =
    gcc_jit_context_new_param (
      ctxt,
      gcc_jit_context_new_location (
	ctxt, __FILE__, FIRST_LINE + 1, 35),
      the_type, "x");
  gcc_jit_param *params[1] = {x};
  gcc_jit_function *func =
    gcc_jit_context_new_function (ctxt,
				  gcc_jit_context_new_location (
				    ctxt, __FILE__, FIRST_LINE, 17),
				  GCC_JIT_FUNCTION_EXPORTED,
				  return_type,
				  "my_fibonacci",
				  1, params, 0);

 /* Create forward labels: */
  gcc_jit_label *label_true =
    gcc_jit_function_new_forward_label (func, NULL);

  gcc_jit_label *label_false =
    gcc_jit_function_new_forward_label (func, NULL);

 /* if (x < 2) */
  gcc_jit_function_add_conditional (
    func,
    gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 3, 19),
    gcc_jit_context_new_comparison (
      ctxt,
      gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 3, 25),
      GCC_JIT_COMPARISON_LT,
      gcc_jit_param_as_rvalue (x),
      gcc_jit_context_new_rvalue_from_int (
	ctxt,
	the_type,
	2)),
    label_true,
    label_false);

  /* true branch: */
  gcc_jit_function_place_forward_label (
    func,
    gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 4, 21),
    label_true);
  /* return x */
  gcc_jit_function_add_return (
    func,
    gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 4, 21),
    gcc_jit_param_as_rvalue (x));

  /* false branch: */
  gcc_jit_function_place_forward_label (
    func,
    gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 21),
    label_false);
  gcc_jit_rvalue *x_minus_1 =
    gcc_jit_context_new_binary_op (
      ctxt,
      gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 44),
      GCC_JIT_BINARY_OP_MINUS, the_type,
      gcc_jit_param_as_rvalue (x),
      gcc_jit_context_new_rvalue_from_int (
	ctxt,
	the_type,
	1));
  gcc_jit_rvalue *x_minus_2 =
    gcc_jit_context_new_binary_op (
      ctxt,
      gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 67),
      GCC_JIT_BINARY_OP_MINUS, the_type,
      gcc_jit_param_as_rvalue (x),
      gcc_jit_context_new_rvalue_from_int (
	ctxt,
	the_type,
	2));
  gcc_jit_function_add_return (
    func,
    gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 21),
    gcc_jit_context_new_binary_op (
      ctxt,
      gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 49),
      GCC_JIT_BINARY_OP_PLUS, the_type,
      /* my_fibonacci (x - 1) */
      gcc_jit_context_new_call (
	ctxt,
	gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 28),
	func,
	1, &x_minus_1),
      /* my_fibonacci (x - 2) */
      gcc_jit_context_new_call (
	ctxt,
	gcc_jit_context_new_location (ctxt, __FILE__, FIRST_LINE + 6, 51),
	func,
	1, &x_minus_2)));
  return 0;
}

void
verify_code (gcc_jit_result *result)
{
  typedef int (*my_fibonacci_fn_type) (int);
  CHECK_NON_NULL (result);
  my_fibonacci_fn_type my_fibonacci =
    (my_fibonacci_fn_type)gcc_jit_result_get_code (result, "my_fibonacci");
  CHECK_NON_NULL (my_fibonacci);
  int val = my_fibonacci (10);
  printf("my_fibonacci returned: %d\n", val);
  CHECK_VALUE (val, 55);
}
