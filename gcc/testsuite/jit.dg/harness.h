/*
  Code shared between multiple testcases.

  This file contains "main" and support code.
  Each testcase should implement the following hooks:

    extern int
    code_making_callback (gcc_jit_context *ctxt, void * user_data);

    extern void
    verify_code (gcc_jit_result *result);

 */
#include <stdlib.h>
#include <stdio.h>

#include <dejagnu.h>

#include "libgccjit.h"

static char test[1024];

#define CHECK_NON_NULL(PTR) \
  do {                                       \
    if ((PTR) != NULL)                       \
      {                                      \
	pass ("%s: %s is non-null", test, #PTR); \
      }                                      \
    else                                     \
      {                                      \
	fail ("%s: %s is NULL", test, #PTR); \
	abort ();                            \
    }                                        \
  } while (0)

#define CHECK_VALUE(ACTUAL, EXPECTED) \
  do {                                       \
    if ((ACTUAL) == (EXPECTED))              \
      {                                      \
	pass ("%s: actual: %s == expected: %s", test, #ACTUAL, #EXPECTED); \
      }                                      \
    else                                     \
      {                                        \
	fail ("%s: actual: %s != expected: %s", test, #ACTUAL, #EXPECTED); \
	fprintf (stderr, "incorrect value\n"); \
	abort ();                              \
    }                                        \
  } while (0)

/* Hooks that testcases should provide.  */
extern int
code_making_callback (gcc_jit_context *ctxt, void * user_data);

extern void
verify_code (gcc_jit_result *result);

/* Implement framework needed for turning the testcase hooks into an
   executable.  test-combination.c combines multiple testcases into one
   testcase, so we have TEST_COMBINATION as a way of temporarily turning
   off this part of harness.h.  */
#ifndef TEST_COMBINATION

/* Run one iteration of the test.  */
static void
test_jit (const char *argv0)
{
  gcc_jit_context *ctxt;
  gcc_jit_result *result;

  ctxt = gcc_jit_context_acquire ();
     /* FIXME: error-handling */

  gcc_jit_context_set_code_factory (ctxt, code_making_callback, NULL);

  /* Set up options.  */
  gcc_jit_context_set_str_option (
    ctxt,
    GCC_JIT_STR_OPTION_PROGNAME,
    argv0);
  gcc_jit_context_set_int_option (
    ctxt,
    GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL,
    0);
  gcc_jit_context_set_bool_option (
    ctxt,
    GCC_JIT_BOOL_OPTION_DEBUGINFO,
    1);
  gcc_jit_context_set_bool_option (
    ctxt,
    GCC_JIT_BOOL_OPTION_DUMP_INITIAL_TREE,
    0);
  gcc_jit_context_set_bool_option (
    ctxt,
    GCC_JIT_BOOL_OPTION_DUMP_INITIAL_GIMPLE,
    0);
  gcc_jit_context_set_bool_option (
    ctxt,
    GCC_JIT_BOOL_OPTION_SELFCHECK_GC,
    1);

  /* This actually calls into GCC and runs the build, all
     in a mutex for now.  */
  result = gcc_jit_context_compile (ctxt);

  gcc_jit_context_release (ctxt);

  verify_code (result);

  /* Once we're done with the code, this unloads the built .so file: */
  gcc_jit_result_release (result);
}

/* We want to prefix all unit test results with the test, but dejagnu.exp's
   host_execute appears to get confused by the leading "./" of argv0,
   leading to all tests simply reporting as a single period character ".".

   Hence strip out the final component of the path to the program name,
   so that we can use that in unittest reports.  */
const char*
extract_progname (const char *argv0)
{
  const char *p;

  p = argv0 + strlen (argv0);
  while (p != argv0 && p[-1] != '/')
    --p;
  return p;
}

int
main (int argc, char **argv)
{
  int i;

  for (i = 1; i <= 5; i++)
    {
      snprintf (test, sizeof (test),
		"%s iteration %d of %d",
                extract_progname (argv[0]),
                i, 5);

      //printf ("ITERATION %d\n", i);
      test_jit (argv[0]);
      //printf ("\n");
    }

  totals ();

  return 0;
}
#endif /* #ifndef TEST_COMBINATION */
