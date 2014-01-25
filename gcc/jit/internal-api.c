#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"
#include "tree.h"
#include "debug.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "cgraph.h"
#include "toplev.h"
#include "tree-iterator.h"
#include "gimple.h"
#include "gimple-pretty-print.h"
#include "timevar.h"
#include "diagnostic-core.h"
#include "dumpfile.h"
#include "tree-cfg.h"

#include <pthread.h>

#include "internal-api.h"

namespace gcc {
namespace jit {

/**********************************************************************
 Recording.
 **********************************************************************/

playback::location *
recording::playback_location (recording::location *loc)
{
  if (loc)
    return loc->playback_location ();
  else
    return NULL;
}

const char *
recording::playback_string (recording::string *str)
{
  if (str)
    return str->c_str ();
  else
    return NULL;
}

playback::label *
recording::playback_label (recording::label *lab)
{
  if (lab)
    return lab->playback_label ();
  else
    return NULL;
}

/* gcc::jit::recording::context:: */

recording::context::~context ()
{
  int i;
  memento *m;
  FOR_EACH_VEC_ELT (m_mementos, i, m)
    {
      delete m;
    }
}

void
recording::context::replay_into (replayer *r)
{
  int i;
  memento *m;
  FOR_EACH_VEC_ELT (m_mementos, i, m)
    {
      m->replay_into (r);
    }
}

/* During a playback, we associate objects from the recording with
   their counterparts during this playback.

   For simplicity, we store this within the recording objects.

   The following method cleans away these associations, to ensure that
   we never have out-of-date associations lingering on subsequent
   playbacks (the objects pointed to are GC-managed, but the
   recording objects don't own refs to them).  */

void
recording::context::disassociate_from_playback ()
{
  int i;
  memento *m;
  FOR_EACH_VEC_ELT (m_mementos, i, m)
    {
      m->set_playback_obj (NULL);
    }
}

recording::string *
recording::context::new_string (const char *text)
{
  if (!text)
    return NULL;

  recording::string *result = new string (this, text);
  record (result);
  return result;
}

recording::location *
recording::context::new_location (const char *filename,
				 int line,
				 int column)
{
  recording::location *result =
    new recording::location (this,
			    new_string (filename),
			    line, column);
  record (result);
  return result;
}

recording::type *
recording::context::get_type (enum gcc_jit_types kind)
{
  recording::type *result = new memento_of_get_type (this, kind);
  record (result);
  return result;
}

recording::field *
recording::context::new_field (recording::location *loc,
			       recording::type *type,
			       const char *name)
{
  recording::field *result =
    new recording::field (this, loc, type, new_string (name));
  record (result);
  return result;
}

recording::type *
recording::context::new_struct_type (recording::location *loc,
				     const char *name,
				     int num_fields,
				     field **fields)
{
  vec<field *> vec_fields;
  vec_fields.create (num_fields);
  for (int i = 0; i < num_fields; i++)
    vec_fields.safe_push (fields[i]);
  recording::type *result = new struct_ (this, loc, new_string (name),
			      vec_fields);
  record (result);
  return result;
}


recording::param *
recording::context::new_param (recording::location *loc,
			       recording::type *type,
			       const char *name)
{
  recording::param *result = new recording::param (this, loc, type, new_string (name));
  record (result);
  return result;
}

recording::function *
recording::context::new_function (recording::location *loc,
				  enum gcc_jit_function_kind kind,
				  recording::type *return_type,
				  const char *name,
				  int num_params,
				  recording::param **params,
				  int is_variadic)
{
  recording::function *result =
    new recording::function (this,
			     loc, kind, return_type,
			     new_string (name),
			     num_params, params, is_variadic);
  record (result);
  return result;
}

recording::lvalue *
recording::context::new_global (recording::location *loc,
				recording::type *type,
				const char *name)
{
  recording::lvalue *result =
    new recording::global (this, loc, type, new_string (name));
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_rvalue_from_int (recording::type *type,
					 int value)
{
  recording::rvalue *result =
    new memento_of_new_rvalue_from_int (this, NULL, type, value);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_rvalue_from_double (recording::type *type,
					    double value)
{
  recording::rvalue *result =
    new memento_of_new_rvalue_from_double (this, NULL, type, value);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_rvalue_from_ptr (recording::type *type,
					 void *value)
{
  recording::rvalue *result =
    new memento_of_new_rvalue_from_ptr (this, NULL, type, value);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_string_literal (const char *value)
{
  recording::rvalue *result =
    new memento_of_new_string_literal (this, NULL, new_string (value));
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_unary_op (recording::location *loc,
				  enum gcc_jit_unary_op op,
				  recording::type *result_type,
				  recording::rvalue *a)
{
  recording::rvalue *result =
    new unary_op (this, loc, op, result_type, a);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_binary_op (recording::location *loc,
				   enum gcc_jit_binary_op op,
				   recording::type *result_type,
				   recording::rvalue *a,
				   recording::rvalue *b)
{
  recording::rvalue *result =
    new binary_op (this, loc, op, result_type, a, b);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_comparison (recording::location *loc,
				    enum gcc_jit_comparison op,
				    recording::rvalue *a,
				    recording::rvalue *b)
{
  recording::rvalue *result = new comparison (this, loc, op, a, b);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_call (recording::location *loc,
			      function *func,
			      int numargs , recording::rvalue **args)
{
  recording::rvalue *result = new call (this, loc, func, numargs, args);
  record (result);
  return result;
}

recording::rvalue *
recording::context::new_array_lookup (recording::location *loc,
				      recording::rvalue *ptr,
				      recording::rvalue *index)
{
  recording::rvalue *result = new array_lookup (this, loc, ptr, index);
  record (result);
  return result;
}

void
recording::context::set_str_option (enum gcc_jit_str_option opt,
				    const char *value)
{
  if (opt < 0 || opt >= GCC_JIT_NUM_STR_OPTIONS)
    {
      add_error ("unrecognized (enum gcc_jit_str_option) value: %i", opt);
      return;
    }
  m_str_options[opt] = value;
}

void
recording::context::set_int_option (enum gcc_jit_int_option opt,
				    int value)
{
  if (opt < 0 || opt >= GCC_JIT_NUM_INT_OPTIONS)
    {
      add_error ("unrecognized (enum gcc_jit_int_option) value: %i", opt);
      return;
    }
  m_int_options[opt] = value;
}

void
recording::context::set_bool_option (enum gcc_jit_bool_option opt,
				     int value)
{
  if (opt < 0 || opt >= GCC_JIT_NUM_BOOL_OPTIONS)
    {
      add_error ("unrecognized (enum gcc_jit_bool_option) value: %i", opt);
      return;
    }
  m_bool_options[opt] = value ? true : false;
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

result *
recording::context::compile ()
{
  if (errors_occurred ())
    return NULL;

  /* Acquire the big GCC mutex. */
  pthread_mutex_lock (&mutex);
  gcc_assert (NULL == ::gcc::jit::active_playback_ctxt);

  /* Set up a playback context.  */
  ::gcc::jit::playback::context replayer (this);
  ::gcc::jit::active_playback_ctxt = &replayer;

  result *result_obj = replayer.compile ();

  /* Release the big GCC mutex. */
  ::gcc::jit::active_playback_ctxt = NULL;
  pthread_mutex_unlock (&mutex);

  return result_obj;
}

void
recording::context::add_error (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  add_error_va (fmt, ap);
  va_end (ap);
}

void
recording::context::add_error_va (const char *fmt, va_list ap)
{
  char buf[1024];
  vsnprintf (buf, sizeof (buf) - 1, fmt, ap);

  fprintf (stderr, "%s: %s\n",
	   get_str_option (GCC_JIT_STR_OPTION_PROGNAME),
	   buf);

  if (!m_error_count)
    {
      strncpy (m_first_error_str, buf, sizeof(m_first_error_str));
      m_first_error_str[sizeof(m_first_error_str) - 1] = '\0';
    }

  m_error_count++;
}

const char *
recording::context::get_first_error () const
{
  if (m_error_count)
    return m_first_error_str;
  else
    return NULL;
}

/* gcc::jit::recording::string:: */
recording::string::string (context *ctxt, const char *text)
  : memento (ctxt)
{
  m_len = strlen (text) ;
  m_copy = new char[m_len + 1];
  strcpy (m_copy, text);
}

recording::string::~string ()
{
  delete[] m_copy;
}



/* gcc::jit::recording::location:: */
void
recording::location::replay_into (replayer *r)
{
  m_playback_obj = r->new_location (m_filename->c_str (), m_line, m_column);
}

/* gcc::jit::recording::type:: */

recording::type *
recording::type::get_pointer ()
{
  recording::type *result = new memento_of_get_pointer (this);
  m_ctxt->record (result);
  return result;
}

recording::type *
recording::type::get_const ()
{
  recording::type *result = new memento_of_get_const (this);
  m_ctxt->record (result);
  return result;
}

/* gcc::jit::recording::memento_of_get_type:: */
void
recording::memento_of_get_type::replay_into (replayer *r)
{
  set_playback_obj (r->get_type (m_kind));
}

/* gcc::jit::recording::memento_of_get_pointer:: */
void
recording::memento_of_get_pointer::replay_into (replayer *)
{
  set_playback_obj (m_other_type->playback_type ()->get_pointer ());
}

/* gcc::jit::recording::memento_of_get_const:: */

void
recording::memento_of_get_const::replay_into (replayer *)
{
  set_playback_obj (m_other_type->playback_type ()->get_const ());
}

/* gcc::jit::recording::field:: */
void
recording::field::replay_into (replayer *r)
{
  set_playback_obj (r->new_field (playback_location (m_loc),
				  m_type->playback_type (),
				  playback_string (m_name)));
}

/* gcc::jit::recording::struct_:: */
void
recording::struct_::replay_into (replayer *r)
{
  vec<playback::field *> playback_fields;
  playback_fields.create (m_fields.length ());
  for (unsigned i = 0; i < m_fields.length (); i++)
    playback_fields.safe_push (m_fields[i]->playback_field ());

  set_playback_obj (
    r->new_struct_type (playback_location (m_loc),
			m_name->c_str (),
			&playback_fields));
}


/* gcc::jit::recording::rvalue:: */

recording::rvalue *
recording::rvalue::access_field (recording::location *loc,
		      const char *fieldname)
{
  recording::rvalue *result =
    new access_field_rvalue (m_ctxt, loc, this,
			     new_string (fieldname));
  m_ctxt->record (result);
  return result;
}

recording::lvalue *
recording::rvalue::dereference_field (recording::location *loc,
			   const char *fieldname)
{
  recording::lvalue *result =
    new dereference_field_rvalue (m_ctxt, loc, this,
				  new_string (fieldname));
  m_ctxt->record (result);
  return result;
}

recording::lvalue *
recording::rvalue::dereference (recording::location *loc)
{
  recording::lvalue *result =
    new dereference_rvalue (m_ctxt, loc, this);
  m_ctxt->record (result);
  return result;
}

/* gcc::jit::recording::lvalue:: */

recording::lvalue *
recording::lvalue::access_field (recording::location *loc,
		      const char *fieldname)
{
  recording::lvalue *result =
    new access_field_of_lvalue (m_ctxt, loc, this,
				new_string (fieldname));
  m_ctxt->record (result);
  return result;
}

recording::rvalue *
recording::lvalue::get_address (recording::location *loc)
{
  recording::rvalue *result =
    new get_address_of_lvalue (m_ctxt, loc, this);
  m_ctxt->record (result);
  return result;
}

/* gcc::jit::recording::param:: */
void
recording::param::replay_into (replayer *r)
{
  set_playback_obj (r->new_param (playback_location (m_loc),
				  m_type->playback_type (),
				  m_name->c_str ()));
}


/* gcc::jit::recording::function:: */

recording::function::function (context *ctxt,
			       recording::location *loc,
			       enum gcc_jit_function_kind kind,
			       type *return_type,
			       recording::string *name,
			       int num_params,
			       recording::param **params,
			       int is_variadic)
: memento (ctxt),
  m_loc (loc),
  m_kind (kind),
  m_return_type (return_type),
  m_name (name),
  m_params (),
  m_is_variadic (is_variadic)
{
  for (int i = 0; i< num_params; i++)
    m_params.safe_push (params[i]);
}

void
recording::function::replay_into (replayer *r)
{
  /* Convert m_params to a vec of playback param.  */
  vec <playback::param *> params;
  int i;
  recording::param *param;
  params.create (m_params.length ());
  FOR_EACH_VEC_ELT (m_params, i, param)
    params.safe_push (param->playback_param ());

  set_playback_obj (r->new_function (playback_location (m_loc),
				     m_kind,
				     m_return_type->playback_type (),
				     m_name->c_str (),
				     &params,
				     m_is_variadic));
}

recording::lvalue *
recording::function::new_local (recording::location *loc,
		     type *type,
		     const char *name)
{
  local *result = new local (this, loc, type, new_string (name));
  m_ctxt->record (result);
  return result;
}

recording::label*
recording::function::new_forward_label (const char *name)
{
  recording::label *result =
    new recording::label (this, new_string (name));
  m_ctxt->record (result);
  return result;
}

void
recording::function::add_eval (recording::location *loc,
			       recording::rvalue *rvalue)
{
  statement *result = new eval (this, loc, rvalue);
  m_ctxt->record (result);
}

void
recording::function::add_assignment (recording::location *loc,
				     recording::lvalue *lvalue,
				     recording::rvalue *rvalue)
{
  statement *result = new assignment (this, loc, lvalue, rvalue);
  m_ctxt->record (result);
}

void
recording::function::add_assignment_op (recording::location *loc,
					recording::lvalue *lvalue,
					enum gcc_jit_binary_op op,
					recording::rvalue *rvalue)
{
  statement *result = new assignment_op (this, loc, lvalue, op, rvalue);
  m_ctxt->record (result);
}

void
recording::function::add_comment (recording::location *loc,
				  const char *text)
{
  statement *result = new comment (this, loc, new_string (text));
  m_ctxt->record (result);
}

void
recording::function::add_conditional (recording::location *loc,
				      recording::rvalue *boolval,
				      recording::label *on_true,
				      recording::label *on_false)
{
  statement *result = new conditional (this, loc, boolval, on_true, on_false);
  m_ctxt->record (result);
}

recording::label *
recording::function::add_label (recording::location *loc,
				const char *name)
{
  recording::label *lab = new_forward_label (name);
  place_forward_label (loc, lab);
  return lab;
}

void
recording::function::place_forward_label (recording::location *loc,
					  recording::label *lab)
{
  statement *result = new place_label (this, loc, lab);
  m_ctxt->record (result);
}

void
recording::function::add_jump (recording::location *loc,
			       recording::label *target)
{
  statement *result = new jump (this, loc, target);
  m_ctxt->record (result);
}

void
recording::function::add_return (recording::location *loc,
				 recording::rvalue *rvalue)
{
  statement *result = new return_ (this, loc, rvalue);
  m_ctxt->record (result);
}

recording::loop *
recording::function::new_loop (recording::location *loc,
			       recording::rvalue *boolval)
{
  recording::loop *result = new recording::loop (this, loc, boolval);
  m_ctxt->record (result);
  return result;
}

/* gcc::jit::recording::label:: */

void
recording::label::replay_into (replayer *)
{
  set_playback_obj (m_func->playback_function ()
		      ->new_forward_label (playback_string (m_name)));
}

/* gcc::jit::recording::global:: */
void
recording::global::replay_into (replayer *r)
{
  set_playback_obj (r->new_global (playback_location (m_loc),
				   m_type->playback_type (),
				   playback_string (m_name)));
}

/* gcc::jit::recording::memento_of_new_rvalue_from_int:: */
void
recording::memento_of_new_rvalue_from_int::replay_into (replayer *r)
{
  set_playback_obj (r->new_rvalue_from_int (m_type->playback_type (),
					    m_value));
}

/* gcc::jit::recording::memento_of_new_rvalue_from_double:: */
void
recording::memento_of_new_rvalue_from_double::replay_into (replayer *r)
{
  set_playback_obj (r->new_rvalue_from_double (m_type->playback_type (),
					       m_value));
}

/* gcc::jit::recording::memento_of_new_rvalue_from_ptr:: */
void
recording::memento_of_new_rvalue_from_ptr::replay_into (replayer *r)
{
  set_playback_obj (r->new_rvalue_from_ptr (m_type->playback_type (),
					    m_value));
}

/* gcc::jit::recording::memento_of_new_string_literal:: */
void
recording::memento_of_new_string_literal::replay_into (replayer *r)
{
  set_playback_obj (r->new_string_literal (m_value->c_str ()));
}

/* gcc::jit::recording::unary_op:: */
void
recording::unary_op::replay_into (replayer *r)
{
  set_playback_obj (r->new_unary_op (playback_location (m_loc),
				     m_op,
				     m_result_type->playback_type (),
				     m_a->playback_rvalue ()));
}


/* gcc::jit::recording::binary_op:: */
void
recording::binary_op::replay_into (replayer *r)
{
  set_playback_obj (r->new_binary_op (playback_location (m_loc),
				      m_op,
				      m_result_type->playback_type (),
				      m_a->playback_rvalue (),
				      m_b->playback_rvalue ()));
}

void
recording::comparison::replay_into (replayer *r)
{
  set_playback_obj (r->new_comparison (playback_location (m_loc),
				       m_op,
				       m_a->playback_rvalue (),
				       m_b->playback_rvalue ()));
}

recording::call::call (recording::context *ctxt,
		       recording::location *loc,
		       recording::function *func,
		       int numargs,
		       rvalue **args)
: rvalue (ctxt, loc),
  m_func (func),
  m_args ()
{
  for (int i = 0; i< numargs; i++)
    m_args.safe_push (args[i]);
}

void
recording::call::replay_into (replayer *r)
{
  vec<playback::rvalue *> playback_args;
  playback_args.create (m_args.length ());
  for (unsigned i = 0; i< m_args.length (); i++)
    playback_args.safe_push (m_args[i]->playback_rvalue ());

  set_playback_obj (r->new_call (playback_location (m_loc),
				 m_func->playback_function (),
				 playback_args));
}

void
recording::array_lookup::replay_into (replayer *r)
{
  set_playback_obj (
    r->new_array_lookup (playback_location (m_loc),
			 m_ptr->playback_rvalue (),
			 m_index->playback_rvalue ()));
}

void
recording::access_field_of_lvalue::replay_into (replayer *)
{
  set_playback_obj (
    m_lvalue->playback_lvalue ()
      ->access_field (playback_location (m_loc),
		      m_fieldname->c_str ()));
}

void
recording::access_field_rvalue::replay_into (replayer *)
{
  set_playback_obj (
    m_rvalue->playback_rvalue ()
      ->access_field (playback_location (m_loc),
		      m_fieldname->c_str ()));
}

void
recording::dereference_field_rvalue::replay_into (replayer *)
{
  set_playback_obj (
    m_rvalue->playback_rvalue ()->
      dereference_field (playback_location (m_loc),
			 m_fieldname->c_str ()));
}

void
recording::dereference_rvalue::replay_into (replayer *)
{
  set_playback_obj (
    m_rvalue->playback_rvalue ()->
      dereference (playback_location (m_loc)));
}

void
recording::get_address_of_lvalue::replay_into (replayer *)
{
  set_playback_obj (
    m_lvalue->playback_lvalue ()->
      get_address (playback_location (m_loc)));
}

void
recording::local::replay_into (replayer *)
{
  set_playback_obj (
    m_func->playback_function ()
      ->new_local (playback_location (m_loc),
		   m_type->playback_type (),
		   playback_string (m_name)));
}

void
recording::eval::replay_into (replayer *)
{
  playback_function ()
    ->add_eval (playback_location (),
		m_rvalue->playback_rvalue ());
}

void
recording::assignment::replay_into (replayer *)
{
  playback_function ()
    ->add_assignment (playback_location (),
		      m_lvalue->playback_lvalue (),
		      m_rvalue->playback_rvalue ());
}

void
recording::assignment_op::replay_into (replayer *r)
{
  playback::type *result_type =
    m_lvalue->playback_lvalue ()->get_type ();

  playback::rvalue *binary_op =
    r->new_binary_op (playback_location (),
		      m_op,
		      result_type,
		      m_lvalue->playback_rvalue (),
		      m_rvalue->playback_rvalue ());

  playback_function ()
    ->add_assignment (playback_location (),
		      m_lvalue->playback_lvalue (),
		      binary_op);
}

void
recording::comment::replay_into (replayer *)
{
  playback_function ()
    ->add_comment (playback_location (),
		   m_text->c_str ());
}

void
recording::conditional::replay_into (replayer *)
{
  playback_function ()
    ->add_conditional (playback_location (),
		       m_boolval->playback_rvalue (),
		       playback_label (m_on_true),
		       playback_label (m_on_false));
}

void
recording::place_label::replay_into (replayer *)
{
  playback_function ()
    ->place_forward_label (playback_location (),
			   m_label->playback_label ());
}

void
recording::jump::replay_into (replayer *)
{
  playback_function ()
    ->add_jump (playback_location (),
		m_target->playback_label ());
}

void
recording::return_::replay_into (replayer *)
{
  playback_function ()
    ->add_return (playback_location (),
		  m_rvalue ? m_rvalue->playback_rvalue () : NULL);
}

void
recording::loop::replay_into (replayer *)
{
  set_playback_obj (
    m_func->playback_function ()
      ->new_loop (playback_location (m_loc),
		  m_boolval->playback_rvalue ()));
}

void
recording::loop::end (location *loc)
{
  recording::loop_end *m = new loop_end (this, loc);
  m_ctxt->record (m);
}

void
recording::loop_end::replay_into (replayer *)
{
  m_loop->playback_loop ()->end (playback_location (m_loc));
}

/**********************************************************************
 Playback.
 **********************************************************************/

playback::context::context (recording::context *ctxt)
  : m_recording_ctxt (ctxt),
    m_char_array_type_node (NULL),
    m_const_char_ptr (NULL)
{
  m_functions.create (0);
  m_source_files.create (0);
  m_cached_locations.create (0);
}

playback::context::~context ()
{
  if (get_bool_option (GCC_JIT_BOOL_OPTION_KEEP_INTERMEDIATES))
    fprintf (stderr, "intermediate files written to %s\n", m_path_tempdir);
  else
    {
      /* Clean up .s/.so and tempdir. */
      if (m_path_s_file)
        unlink (m_path_s_file);
      if (m_path_so_file)
        unlink (m_path_so_file);
      if (m_path_tempdir)
        rmdir (m_path_tempdir);
    }

  free (m_path_template);
  /* m_path_tempdir aliases m_path_template, or is NULL, so don't
     attempt to free it .  */
  free (m_path_c_file);
  free (m_path_s_file);
  free (m_path_so_file);
  m_functions.release ();
}

void
playback::context::
gt_ggc_mx ()
{
  int i;
  function *func;
  FOR_EACH_VEC_ELT (m_functions, i, func)
    {
      if (ggc_test_and_set_mark (func))
	func->gt_ggc_mx ();
    }
}

static tree
get_tree_node_for_type (enum gcc_jit_types type_)
{
  switch (type_)
    {
    case GCC_JIT_TYPE_VOID:
      return void_type_node;

    case GCC_JIT_TYPE_VOID_PTR:
      return ptr_type_node;

    case GCC_JIT_TYPE_CHAR:
      return char_type_node;
    case GCC_JIT_TYPE_SIGNED_CHAR:
      return signed_char_type_node;
    case GCC_JIT_TYPE_UNSIGNED_CHAR:
      return unsigned_char_type_node;

    case GCC_JIT_TYPE_SHORT:
      return short_integer_type_node;
    case GCC_JIT_TYPE_UNSIGNED_SHORT:
      return short_unsigned_type_node;

    case GCC_JIT_TYPE_CONST_CHAR_PTR:
      {
	tree const_char = build_qualified_type (char_type_node,
						TYPE_QUAL_CONST);
	return build_pointer_type (const_char);
      }

    case GCC_JIT_TYPE_INT:
      return integer_type_node;
    case GCC_JIT_TYPE_UNSIGNED_INT:
      return unsigned_type_node;

    case GCC_JIT_TYPE_LONG:
      return long_integer_type_node;
    case GCC_JIT_TYPE_UNSIGNED_LONG:
      return long_unsigned_type_node;

    case GCC_JIT_TYPE_LONG_LONG:
      return long_long_integer_type_node;
    case GCC_JIT_TYPE_UNSIGNED_LONG_LONG:
      return long_long_unsigned_type_node;

    case GCC_JIT_TYPE_FLOAT:
      return float_type_node;
    case GCC_JIT_TYPE_DOUBLE:
      return double_type_node;
    case GCC_JIT_TYPE_LONG_DOUBLE:
      return long_double_type_node;

    case GCC_JIT_TYPE_SIZE_T:
      return size_type_node;

    case GCC_JIT_TYPE_FILE_PTR:
      return fileptr_type_node;
    }

  return NULL;
}

playback::type *
playback::context::
get_type (enum gcc_jit_types type_)
{
  tree type_node = get_tree_node_for_type (type_);
  if (NULL == type_node)
    {
      add_error ("unrecognized (enum gcc_jit_types) value: %i", type_);
      return NULL;
    }

  return new type (type_node);
}

playback::field *
playback::context::
new_field (location *loc,
	   type *type,
	   const char *name)
{
  gcc_assert (type);
  gcc_assert (name);

  /* compare with c/c-decl.c:grokfield and grokdeclarator.  */
  tree decl = build_decl (UNKNOWN_LOCATION, FIELD_DECL,
			  get_identifier (name), type->as_tree ());

  if (loc)
    set_tree_location (decl, loc);

  return new field (decl);
}

playback::type *
playback::context::
new_struct_type (location *loc,
		 const char *name,
		 vec<field *> *fields)
{
  gcc_assert (name);
  gcc_assert (fields);

  /* Compare with c/c-decl.c: start_struct and finish_struct. */

  tree t = make_node (RECORD_TYPE);
  TYPE_NAME (t) = get_identifier (name);
  TYPE_SIZE (t) = 0;

  tree fieldlist = NULL;
  for (unsigned i = 0; i < fields->length (); i++)
    {
      field *f = (*fields)[i];
      DECL_CONTEXT (f->as_tree ()) = t;
      fieldlist = chainon (f->as_tree (), fieldlist);
    }
  fieldlist = nreverse (fieldlist);
  TYPE_FIELDS (t) = fieldlist;

  layout_type (t);

  if (loc)
    set_tree_location (t, loc);

  return new type (t);
}


playback::param *
playback::context::
new_param (location *loc,
	   type *type,
	   const char *name)
{
  gcc_assert (type);
  gcc_assert (name);
  tree inner = build_decl (UNKNOWN_LOCATION, PARM_DECL,
			   get_identifier (name), type->as_tree ());
  if (loc)
    set_tree_location (inner, loc);

  return new param (this, inner);
}

playback::function *
playback::context::
new_function (location *loc,
	      enum gcc_jit_function_kind kind,
	      type *return_type,
	      const char *name,
	      vec<param *> *params,
	      int is_variadic)
{
  int i;
  param *param;

  //can return_type be NULL?
  gcc_assert (name);

  tree *arg_types = (tree *)xcalloc(params->length (), sizeof(tree*));
  FOR_EACH_VEC_ELT (*params, i, param)
    arg_types[i] = TREE_TYPE (param->as_tree ());

  tree fn_type;
  if (is_variadic)
    fn_type = build_varargs_function_type_array (return_type->as_tree (),
						 params->length (), arg_types);
  else
    fn_type = build_function_type_array (return_type->as_tree (),
					 params->length (), arg_types);
  free (arg_types);

  /* FIXME: this uses input_location: */
  tree fndecl = build_fn_decl (name, fn_type);

  if (loc)
    set_tree_location (fndecl, loc);

  tree resdecl = build_decl (UNKNOWN_LOCATION, RESULT_DECL,
			     NULL_TREE, return_type->as_tree ());
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_IGNORED_P (resdecl) = 1;
  DECL_RESULT (fndecl) = resdecl;

  if (kind != GCC_JIT_FUNCTION_IMPORTED)
    {
      tree param_decl_list = NULL;
      FOR_EACH_VEC_ELT (*params, i, param)
	{
	  param_decl_list = chainon (param->as_tree (), param_decl_list);
	}

      /* The param list was created in reverse order; fix it: */
      param_decl_list = nreverse (param_decl_list);

      tree t;
      for (t = param_decl_list; t; t = DECL_CHAIN (t))
	{
	  DECL_CONTEXT (t) = fndecl;
	  DECL_ARG_TYPE (t) = TREE_TYPE (t);
	}

      /* Set it up on DECL_ARGUMENTS */
      DECL_ARGUMENTS(fndecl) = param_decl_list;
    }

  function *func = new function (this, fndecl, kind);
  m_functions.safe_push (func);
  return func;
}

playback::lvalue *
playback::context::
new_global (location *loc,
            type *type,
            const char *name)
{
  gcc_assert (type);
  gcc_assert (name);
  tree inner = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			   get_identifier (name),
			   type->as_tree ());
  TREE_PUBLIC (inner) = 1;
  DECL_COMMON (inner) = 1;
  DECL_EXTERNAL (inner) = 1;

  if (loc)
    set_tree_location (inner, loc);

  return new lvalue (this, inner);
}

playback::rvalue *
playback::context::
new_rvalue_from_int (type *type,
		     int value)
{
  // FIXME: type-checking, or coercion?
  tree inner_type = type->as_tree ();
  if (INTEGRAL_TYPE_P (inner_type))
    {
      tree inner = build_int_cst (inner_type, value);
      return new rvalue (this, inner);
    }
  else
    {
      REAL_VALUE_TYPE real_value;
      real_from_integer (&real_value, VOIDmode, value, 0, 0);
      tree inner = build_real (inner_type, real_value);
      return new rvalue (this, inner);
    }
}

playback::rvalue *
playback::context::
new_rvalue_from_double (type *type,
			double value)
{
  // FIXME: type-checking, or coercion?
  tree inner_type = type->as_tree ();

  /* We have a "double", we want a REAL_VALUE_TYPE.

     real.c:real_from_target appears to require the representation to be
     split into 32-bit values, and then sent as an pair of host long
     ints.  */
  REAL_VALUE_TYPE real_value;
  union
  {
    double as_double;
    uint32_t as_uint32s[2];
  } u;
  u.as_double = value;
  long int as_long_ints[2];
  as_long_ints[0] = u.as_uint32s[0];
  as_long_ints[1] = u.as_uint32s[1];
  real_from_target (&real_value, as_long_ints, DFmode);
  tree inner = build_real (inner_type, real_value);
  return new rvalue (this, inner);
}

playback::rvalue *
playback::context::
new_rvalue_from_ptr (type *type,
		     void *value)
{
  tree inner_type = type->as_tree ();
  /* FIXME: how to ensure we have a wide enough type?  */
  tree inner = build_int_cstu (inner_type, (unsigned HOST_WIDE_INT)value);
  return new rvalue (this, inner);
}

playback::rvalue *
playback::context::
new_string_literal (const char *value)
{
  tree t_str = build_string (strlen (value), value);
  gcc_assert (m_char_array_type_node);
  TREE_TYPE (t_str) = m_char_array_type_node;

  /* Convert to (const char*), loosely based on
     c/c-typeck.c: array_to_pointer_conversion,
     by taking address of start of string.  */
  tree t_addr = build1 (ADDR_EXPR, m_const_char_ptr, t_str);

  return new rvalue (this, t_addr);
}

tree
playback::context::
as_truth_value (tree expr, location *loc)
{
  /* Compare to c-typeck.c:c_objc_common_truthvalue_conversion */
  tree typed_zero = fold_build1 (CONVERT_EXPR,
				 TREE_TYPE (expr),
				 integer_zero_node);
  if (loc)
    set_tree_location (typed_zero, loc);

  expr = build2 (NE_EXPR, integer_type_node, expr, typed_zero);
  if (loc)
    set_tree_location (expr, loc);

  return expr;
}

playback::rvalue *
playback::context::
new_unary_op (location *loc,
	      enum gcc_jit_unary_op op,
	      type *result_type,
	      rvalue *a)
{
  // FIXME: type-checking, or coercion?
  enum tree_code inner_op;

  gcc_assert (result_type);
  gcc_assert (a);

  tree node = a->as_tree ();
  tree inner_result = NULL;

  switch (op)
    {
    default:
      add_error ("unrecognized (enum gcc_jit_unary_op) value: %i", op);
      return NULL;

    case GCC_JIT_UNARY_OP_MINUS:
      inner_op = NEGATE_EXPR;
      break;

    case GCC_JIT_UNARY_OP_BITWISE_NEGATE:
      inner_op = BIT_NOT_EXPR;
      break;

    case GCC_JIT_UNARY_OP_LOGICAL_NEGATE:
      node = as_truth_value (node, loc);
      inner_result = invert_truthvalue (node);
      if (loc)
	set_tree_location (inner_result, loc);
      return new rvalue (this, inner_result);
    }

  inner_result = build1 (inner_op,
			 result_type->as_tree (),
			 node);
  if (loc)
    set_tree_location (inner_result, loc);

  return new rvalue (this, inner_result);
}

playback::rvalue *
playback::context::
new_binary_op (location *loc,
	       enum gcc_jit_binary_op op,
	       type *result_type,
	       rvalue *a, rvalue *b)
{
  // FIXME: type-checking, or coercion?
  enum tree_code inner_op;

  gcc_assert (result_type);
  gcc_assert (a);
  gcc_assert (b);

  tree node_a = a->as_tree ();
  tree node_b = b->as_tree ();

  switch (op)
    {
    default:
      add_error ("unrecognized (enum gcc_jit_binary_op) value: %i", op);
      return NULL;

    case GCC_JIT_BINARY_OP_PLUS:
      inner_op = PLUS_EXPR;
      break;

    case GCC_JIT_BINARY_OP_MINUS:
      inner_op = MINUS_EXPR;
      break;

    case GCC_JIT_BINARY_OP_MULT:
      inner_op = MULT_EXPR;
      break;

    case GCC_JIT_BINARY_OP_DIVIDE:
      if (FLOAT_TYPE_P (result_type->as_tree ()))
	/* Floating-point division: */
	inner_op = RDIV_EXPR;
      else
	/* Truncating to zero: */
	inner_op = TRUNC_DIV_EXPR;
      break;

    case GCC_JIT_BINARY_OP_MODULO:
      inner_op = TRUNC_MOD_EXPR;
      break;

    case GCC_JIT_BINARY_OP_BITWISE_AND:
      inner_op = BIT_AND_EXPR;
      break;

    case GCC_JIT_BINARY_OP_BITWISE_XOR:
      inner_op = BIT_XOR_EXPR;
      break;

    case GCC_JIT_BINARY_OP_BITWISE_OR:
      inner_op = BIT_IOR_EXPR;
      break;

    case GCC_JIT_BINARY_OP_LOGICAL_AND:
      node_a = as_truth_value (node_a, loc);
      node_b = as_truth_value (node_b, loc);
      inner_op = TRUTH_ANDIF_EXPR;
      break;

    case GCC_JIT_BINARY_OP_LOGICAL_OR:
      node_a = as_truth_value (node_a, loc);
      node_b = as_truth_value (node_b, loc);
      inner_op = TRUTH_ORIF_EXPR;
      break;
    }

  tree inner_expr = build2 (inner_op,
			    result_type->as_tree (),
			    node_a,
			    node_b);
  if (loc)
    set_tree_location (inner_expr, loc);

  return new rvalue (this, inner_expr);
}

playback::rvalue *
playback::context::
new_comparison (location *loc,
		enum gcc_jit_comparison op,
		rvalue *a, rvalue *b)
{
  // FIXME: type-checking, or coercion?
  enum tree_code inner_op;

  gcc_assert (a);
  gcc_assert (b);

  switch (op)
    {
    default:
      add_error ("unrecognized (enum gcc_jit_comparison) value: %i", op);
      return NULL;

    case GCC_JIT_COMPARISON_EQ:
      inner_op = EQ_EXPR;
      break;
    case GCC_JIT_COMPARISON_NE:
      inner_op = NE_EXPR;
      break;
    case GCC_JIT_COMPARISON_LT:
      inner_op = LT_EXPR;
      break;
    case GCC_JIT_COMPARISON_LE:
      inner_op = LE_EXPR;
      break;
    case GCC_JIT_COMPARISON_GT:
      inner_op = GT_EXPR;
      break;
    case GCC_JIT_COMPARISON_GE:
      inner_op = GE_EXPR;
      break;
    }

  tree inner_expr = build2 (inner_op,
			    boolean_type_node,
			    a->as_tree (),
			    b->as_tree ());
  if (loc)
    set_tree_location (inner_expr, loc);
  return new rvalue (this, inner_expr);
}

playback::rvalue *
playback::context::
new_call (location *loc,
	  function *func,
	  vec<rvalue *> args)
{
  tree fndecl;
  vec<tree, va_gc> *tree_args;

  gcc_assert (func);

  // FIXME: type checking
  // FIXME: check num args and types

  fndecl = func->as_fndecl ();

  vec_alloc (tree_args, args.length ());
  for (unsigned i = 0; i < args.length (); i++)
    tree_args->quick_push (args[i]->as_tree ());

  tree fntype = TREE_TYPE (fndecl);
  tree fn = build1 (ADDR_EXPR, build_pointer_type (fntype), fndecl);

  if (loc)
    set_tree_location (fn, loc);

  return new rvalue (this,
		     build_call_vec (func->get_return_type_as_tree (),
				     fn, tree_args));

  /* see c-typeck.c: build_function_call
     which calls build_function_call_vec

     which does lots of checking, then:
    result = build_call_array_loc (loc, TREE_TYPE (fntype),
				   function, nargs, argarray);
    which is in tree.c
    (see also build_call_vec)
   */
}

playback::rvalue *
playback::context::
new_array_lookup (location *loc,
		  rvalue *ptr,
		  rvalue *index)
{
  gcc_assert (ptr);
  gcc_assert (index);

  /* For comparison, see:
       c/c-typeck.c: build_array_ref
       c-family/c-common.c: pointer_int_sum
  */
  tree t_ptr = ptr->as_tree ();
  tree t_index = index->as_tree ();
  tree t_type_ptr = TREE_TYPE (t_ptr);
  tree t_type_star_ptr = TREE_TYPE (t_type_ptr);

  if (TREE_CODE (t_type_ptr) == ARRAY_TYPE)
    {
      tree t_result = build4 (ARRAY_REF, t_type_star_ptr, t_ptr, t_index,
			      NULL_TREE, NULL_TREE);
      if (loc)
        set_tree_location (t_result, loc);
      return new rvalue (this, t_result);
    }
  else
    {
      /* Convert index to an offset in bytes.  */
      tree t_sizeof = size_in_bytes (t_type_star_ptr);
      t_index = fold_build1 (CONVERT_EXPR, sizetype, t_index);
      tree t_offset = build2 (MULT_EXPR, sizetype, t_index, t_sizeof);

      /* Locate (ptr + offset).  */
      tree t_address = build2 (POINTER_PLUS_EXPR, t_type_ptr, t_ptr, t_offset);

      tree t_indirection = build1 (INDIRECT_REF, t_type_star_ptr, t_address);
      if (loc)
        {
          set_tree_location (t_sizeof, loc);
          set_tree_location (t_offset, loc);
          set_tree_location (t_address, loc);
          set_tree_location (t_indirection, loc);
        }

      return new rvalue (this, t_indirection);
    }
}

static tree
get_field (tree type, tree component)
{
  for (tree field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
    if (DECL_NAME (field) == component)
      return field;
  return NULL;
}

tree
playback::context::
new_field_access (location *loc,
		  tree datum,
		  const char *fieldname)
{
  gcc_assert (datum);
  gcc_assert (fieldname);

  /* Compare with c/c-typeck.c:lookup_field, build_indirect_ref, and
     build_component_ref. */
  tree type = TREE_TYPE (datum);
  gcc_assert (type);
  gcc_assert (TREE_CODE (type) != POINTER_TYPE);

  tree component = get_identifier (fieldname);
  tree field = get_field (type, component);
  if (!field)
    {
      add_error ("field not found: \"%s\"", fieldname);
      return NULL;
    }
  tree ref = build3 (COMPONENT_REF, TREE_TYPE (field), datum,
		     field, NULL_TREE);
  if (loc)
    set_tree_location (ref, loc);
  return ref;
}

tree
playback::context::
new_dereference (tree ptr,
		 location *loc)
{
  gcc_assert (ptr);

  tree type = TREE_TYPE (TREE_TYPE(ptr));
  tree datum = build1 (INDIRECT_REF, type, ptr);
  if (loc)
    set_tree_location (datum, loc);
  return datum;
}

playback::lvalue *
playback::lvalue::
access_field (location *loc,
	      const char *fieldname)
{
  tree datum = as_tree ();
  tree ref = get_context ()->new_field_access (loc, datum, fieldname);
  if (!ref)
    return NULL;
  return new lvalue (get_context (), ref);
}

playback::rvalue *
playback::rvalue::
access_field (location *loc,
	      const char *fieldname)
{
  tree datum = as_tree ();
  tree ref = get_context ()->new_field_access (loc, datum, fieldname);
  if (!ref)
    return NULL;
  return new rvalue (get_context (), ref);
}

playback::lvalue *
playback::rvalue::
dereference_field (location *loc,
		   const char *fieldname)
{
  tree ptr = as_tree ();
  tree datum = get_context ()->new_dereference (ptr, loc);
  if (!datum)
    return NULL;
  tree ref = get_context ()->new_field_access (loc, datum, fieldname);
  if (!ref)
    return NULL;
  return new lvalue (get_context (), ref);
}

playback::lvalue *
playback::rvalue::
dereference (location *loc)
{
  tree ptr = as_tree ();
  tree datum = get_context ()->new_dereference (ptr, loc);
  return new lvalue (get_context (), datum);
}

playback::rvalue *
playback::lvalue::
get_address (location *loc)
{
  tree t_lvalue = as_tree ();
  tree t_thistype = TREE_TYPE (t_lvalue);
  tree t_ptrtype = build_pointer_type (t_thistype);
  tree ptr = build1 (ADDR_EXPR, t_ptrtype, t_lvalue);
  if (loc)
    get_context ()->set_tree_location (ptr, loc);
  return new rvalue (get_context (), ptr);
}

void *
playback::wrapper::
operator new (size_t sz)
{
  return ggc_internal_cleared_alloc_stat (sz MEM_STAT_INFO);
}

playback::function::
function (context *ctxt,
	  tree fndecl,
	  enum gcc_jit_function_kind kind)
: m_ctxt(ctxt),
  m_inner_fndecl (fndecl),
  m_inner_bind_expr (NULL),
  m_kind (kind)
{
  if (m_kind != GCC_JIT_FUNCTION_IMPORTED)
    {
      /* Create a BIND_EXPR, and within it, a statement list.  */
      m_stmt_list = alloc_stmt_list ();
      m_stmt_iter = tsi_start (m_stmt_list);
      m_inner_bind_expr =
	build3 (BIND_EXPR, void_type_node, NULL, m_stmt_list, NULL);
    }
  else
    {
      m_stmt_list = NULL;
    }
}

void
playback::function::
gt_ggc_mx ()
{
  gt_ggc_m_9tree_node (m_inner_fndecl);
  gt_ggc_m_9tree_node (m_inner_bind_expr);
  gt_ggc_m_9tree_node (m_stmt_list);
}

tree
playback::function::
get_return_type_as_tree () const
{
  return TREE_TYPE (TREE_TYPE(m_inner_fndecl));
}

playback::lvalue *
playback::function::
new_local (location *loc,
	   type *type,
	   const char *name)
{
  gcc_assert (type);
  gcc_assert (name);
  tree inner = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			   get_identifier (name),
			   type->as_tree ());
  DECL_CONTEXT (inner) = this->m_inner_fndecl;

  /* Prepend to BIND_EXPR_VARS: */
  DECL_CHAIN (inner) = BIND_EXPR_VARS (m_inner_bind_expr);
  BIND_EXPR_VARS (m_inner_bind_expr) = inner;

  if (loc)
    set_tree_location (inner, loc);
  return new lvalue (m_ctxt, inner);
}

playback::label *
playback::function::
new_forward_label (const char *name)
{
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);
  return new playback::label (this, name);
}

void
playback::function::
postprocess ()
{
  if (m_ctxt->get_bool_option (GCC_JIT_BOOL_OPTION_DUMP_INITIAL_TREE))
    debug_tree (m_stmt_list);

  /* Do we need this to force cgraphunit.c to output the function? */
  if (m_kind == GCC_JIT_FUNCTION_EXPORTED)
    {
      DECL_EXTERNAL (m_inner_fndecl) = 0;
      DECL_PRESERVE_P (m_inner_fndecl) = 1;
    }

  if (m_kind != GCC_JIT_FUNCTION_IMPORTED)
    {
      /* Seem to need this in gimple-low.c: */
      DECL_INITIAL (m_inner_fndecl) = make_node (BLOCK);

      /* how to add to function? the following appears to be how to
	 set the body of a m_inner_fndecl: */
      DECL_SAVED_TREE(m_inner_fndecl) = m_inner_bind_expr;
      //debug_tree (m_inner_fndecl);

      /* Convert to gimple: */
      //printf("about to gimplify_function_tree\n");
      gimplify_function_tree (m_inner_fndecl);
      //printf("finished gimplify_function_tree\n");

      current_function_decl = m_inner_fndecl;
      if (m_ctxt->get_bool_option (GCC_JIT_BOOL_OPTION_DUMP_INITIAL_GIMPLE))
	dump_function_to_file (m_inner_fndecl, stderr, TDF_VOPS|TDF_MEMSYMS|TDF_LINENO);
      //debug_tree (m_inner_fndecl);

      //printf("about to add to cgraph\n");
      /* Add to cgraph: */
      cgraph_finalize_function (m_inner_fndecl, false);
      /* This can trigger a collection, so we need to have all of
	 the funcs as roots.  */

      current_function_decl = NULL;
    }
}

void
playback::function::
add_eval (location *loc,
	  rvalue *rvalue)
{
  gcc_assert (rvalue);
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  if (loc)
    set_tree_location (rvalue->as_tree (), loc);

  add_stmt (rvalue->as_tree ());
}

void
playback::function::
add_assignment (location *loc,
		lvalue *lvalue,
		rvalue *rvalue)
{
  gcc_assert (lvalue);
  gcc_assert (rvalue);
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  tree t_lvalue = lvalue->as_tree ();
  tree t_rvalue = rvalue->as_tree ();
  if (TREE_TYPE (t_rvalue) != TREE_TYPE (t_lvalue))
    {
      t_rvalue = build1 (CONVERT_EXPR,
		         TREE_TYPE (t_lvalue),
		         t_rvalue);
      if (loc)
	set_tree_location (t_rvalue, loc);
    }

  tree stmt =
    build2 (MODIFY_EXPR, TREE_TYPE (t_lvalue),
	    t_lvalue, t_rvalue);
  if (loc)
    set_tree_location (stmt, loc);
  add_stmt (stmt);
}

void
playback::function::
add_comment (location *loc,
	     const char *text)
{
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  /* Wrap the text in C-style comment delimiters.  */
  size_t sz =
    (3 /* opening delim */
     + strlen (text)
     + 3 /* closing delim */
     + 1 /* terminator */);
  char *wrapped = (char *)ggc_internal_alloc_stat (sz);
  snprintf (wrapped, sz, "/* %s */", text);

  /* For now we simply implement this by adding a dummy label with a name
     containing the given text.  */
  add_label (loc, wrapped);
}

void
playback::function::
add_conditional (location *loc,
		 rvalue *boolval,
		 label *on_true,
		 label *on_false)
{
  gcc_assert (boolval);
  gcc_assert (on_true);
  /* on_false can be NULL */
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  /* COND_EXPR wants statement lists for the true/false operands, but we
     want labels.
     Shim it by creating jumps to the labels */
  tree true_jump = build1 (GOTO_EXPR, void_type_node,
			   on_true->as_label_decl ());
  if (loc)
    set_tree_location (true_jump, loc);
  tree false_jump;
  if (on_false)
    {
      false_jump = build1 (GOTO_EXPR, void_type_node,
			   on_false->as_label_decl ());
      if (loc)
        set_tree_location (false_jump, loc);
    }
  else
    false_jump = NULL;

  tree stmt =
    build3 (COND_EXPR, void_type_node, boolval->as_tree (),
	    true_jump, false_jump);
  if (loc)
    set_tree_location (stmt, loc);
  add_stmt (stmt);
}

playback::label *
playback::function::
add_label (location *loc,
	   const char *name)
{
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  label *lab = new label (this, name);
  place_forward_label (loc, lab);
  return lab;
}

void
playback::function::
place_forward_label (location *loc, label *lab)
{
  gcc_assert (lab);
  gcc_assert (NULL == lab->m_label_expr); // must not have already been placed
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  lab->m_label_expr = build1 (LABEL_EXPR,
			     void_type_node,
			     lab->as_label_decl ());
  if (loc)
    set_tree_location (lab->m_label_expr, loc);
  add_stmt (lab->m_label_expr);
}

void
playback::function::
add_jump (location *loc,
	  label *target)
{
  gcc_assert (target);
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  // see c_finish_loop
  //tree top = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
  //add_stmt (top);

  //tree stmt = build_and_jump (&LABEL_EXPR_LABEL (target->label_));
  TREE_USED (target->as_label_decl ()) = 1;
  tree stmt = build1 (GOTO_EXPR, void_type_node, target->as_label_decl ());
  if (loc)
    set_tree_location (stmt, loc);
  add_stmt (stmt);

  /*
  from c-typeck.c:
tree
c_finish_goto_label (location_t loc, tree label)
{
  tree decl = lookup_label_for_goto (loc, label);
  if (!decl)
    return NULL_TREE;
  TREE_USED (decl) = 1;
  {
    tree t = build1 (GOTO_EXPR, void_type_node, decl);
    SET_EXPR_LOCATION (t, loc);
    return add_stmt (t);
  }
}
  */

}

void
playback::function::
add_return (location *loc,
	    rvalue *rvalue)
{
  gcc_assert (m_kind != GCC_JIT_FUNCTION_IMPORTED);

  tree return_type = TREE_TYPE (TREE_TYPE (m_inner_fndecl));
  tree t_lvalue = DECL_RESULT (m_inner_fndecl);
  tree t_rvalue = rvalue->as_tree ();
  if (TREE_TYPE (t_rvalue) != TREE_TYPE (t_lvalue))
    t_rvalue = build1 (CONVERT_EXPR,
		       TREE_TYPE (t_lvalue),
		       t_rvalue);
  tree modify_retval = build2 (MODIFY_EXPR, return_type,
			       t_lvalue, t_rvalue);
  tree return_stmt = build1 (RETURN_EXPR, return_type,
			     modify_retval);
  if (loc)
    {
      set_tree_location (modify_retval, loc);
      set_tree_location (return_stmt, loc);
    }
  add_stmt (return_stmt);
}

playback::loop *
playback::function::
new_loop (location *loc,
	  rvalue *boolval)
{
  return new loop (this, loc, boolval);
}

playback::label::
label (function *func,
       const char *name)
{
  tree identifier;

  gcc_assert (func);
  // name can be NULL
  if (name)
    identifier = get_identifier (name);
  else
    identifier = NULL;
  m_label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
			    identifier, void_type_node);
  DECL_CONTEXT (m_label_decl) = func->as_fndecl ();
  m_label_expr = NULL;
}


playback::loop::
loop (function *func, location *loc, rvalue *boolval) :
  m_func(func)
{
  m_label_cond = func->add_label (loc, "loop_cond");
  m_label_body = func->new_forward_label ("loop_body");
  m_label_end = func->new_forward_label ("loop_end");
  func->add_conditional (loc, boolval, m_label_body, m_label_end);
  func->place_forward_label (loc, m_label_body);
}

void
playback::loop::
end (location *loc)
{
  m_func->add_jump (loc, m_label_cond);
  m_func->place_forward_label (loc, m_label_end);
}

result *
playback::context::
compile ()
{
  void *handle = NULL;
  result *result_obj = NULL;
  const char *progname;
  const char *fake_args[20];
  unsigned int num_args;

  m_path_template = xstrdup ("/tmp/libgccjit-XXXXXX");
  if (!m_path_template)
    return NULL;

  /* Create tempdir using mkdtemp.  This is created with 0700 perms and
     is unique.  Hence no other (non-root) users should have access to
     the paths within it.  */
  m_path_tempdir = mkdtemp (m_path_template);
  if (!m_path_tempdir)
    return NULL;
  m_path_c_file = concat (m_path_tempdir, "/fake.c", NULL);
  m_path_s_file = concat (m_path_tempdir, "/fake.s", NULL);
  m_path_so_file = concat (m_path_tempdir, "/fake.so", NULL);

  /* Call into the rest of gcc.
     For now, we have to assemble command-line options to pass into
     toplev_main, so that they can be parsed. */

  /* Pass in user-provided "progname", if any, so that it makes it
     into GCC's "progname" global, used in various diagnostics. */
  progname = get_str_option (GCC_JIT_STR_OPTION_PROGNAME);
  fake_args[0] = progname ? progname : "libgccjit.so";

  fake_args[1] = m_path_c_file;
  num_args = 2;

#define ADD_ARG(arg) \
  do \
    { \
      gcc_assert(num_args < sizeof(fake_args)/sizeof(char*)); \
      fake_args[num_args++] = arg; \
    } \
  while (0)

  ADD_ARG ("-fPIC");

  /* Handle int options: */
  switch (get_int_option (GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL))
    {
    default:
      add_error ("unrecognized optimization level: %i",
		 get_int_option (GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL));
      return NULL;

    case 0:
      ADD_ARG ("-O0");
      break;

    case 1:
      ADD_ARG ("-O1");
      break;

    case 2:
      ADD_ARG ("-O2");
      break;

    case 3:
      ADD_ARG ("-O3");
      break;
    }
  /* What about -Os? */

  /* Handle bool options: */
  if (get_bool_option (GCC_JIT_BOOL_OPTION_DEBUGINFO))
    ADD_ARG ("-g");

  /* Suppress timing (and other) info.  */
  if (!get_bool_option (GCC_JIT_BOOL_OPTION_DUMP_SUMMARY))
    {
      ADD_ARG ("-quiet");
      quiet_flag = 1;
    }

  /* Aggressively garbage-collect, to shake out bugs: */
  if (get_bool_option (GCC_JIT_BOOL_OPTION_SELFCHECK_GC))
    {
      ADD_ARG ("--param");
      ADD_ARG ("ggc-min-expand=0");
      ADD_ARG ("--param");
      ADD_ARG ("ggc-min-heapsize=0");
    }

  if (get_bool_option (GCC_JIT_BOOL_OPTION_DUMP_EVERYTHING))
    {
      ADD_ARG ("-fdump-tree-all");
      ADD_ARG ("-fdump-rtl-all");
      ADD_ARG ("-fdump-ipa-all");
    }

  toplev_options toplev_opts;
  toplev_opts.use_TV_TOTAL = false;

  if (time_report || !quiet_flag  || flag_detailed_statistics)
    timevar_init ();

  timevar_start (TV_TOTAL);

  toplev_main (num_args, const_cast <char **> (fake_args), &toplev_opts);
  toplev_finalize ();

  active_playback_ctxt = NULL;

  if (errors_occurred ())
    {
      timevar_stop (TV_TOTAL);
      timevar_print (stderr);
      return NULL;
    }

  timevar_push (TV_ASSEMBLE);

  /* Gross hacks follow:
     We have a .s file; we want a .so file.
     We could reuse parts of gcc/gcc.c to do this.
     For now, just use the /usr/bin/gcc on the system...
   */
  /* FIXME: totally faking it for now, not even using pex */
  {
    char cmd[1024];
    snprintf (cmd, 1024, "gcc -shared %s -o %s",
              m_path_s_file, m_path_so_file);
    if (0)
      printf ("cmd: %s\n", cmd);
    int ret = system (cmd);
    if (ret)
      {
	timevar_pop (TV_ASSEMBLE);
	timevar_stop (TV_TOTAL);
	timevar_print (stderr);
	return NULL;
      }
  }
  timevar_pop (TV_ASSEMBLE);

  // TODO: split out assembles vs linker

  /* dlopen the .so file. */
  {
    const char *error;

    timevar_push (TV_LOAD);

    /* Clear any existing error.  */
    dlerror ();

    handle = dlopen (m_path_so_file, RTLD_NOW | RTLD_LOCAL);
    if ((error = dlerror()) != NULL)  {
      fprintf(stderr, "%s\n", error);
    }
    if (handle)
      result_obj = new result (handle);
    else
      result_obj = NULL;

    timevar_pop (TV_LOAD);
  }

  timevar_stop (TV_TOTAL);
  timevar_print (stderr);

  return result_obj;
}

void
playback::context::
replay ()
{
  /* Adapted from c-common.c:c_common_nodes_and_builtins.  */
  tree array_domain_type = build_index_type (size_int (200));
  m_char_array_type_node
    = build_array_type (char_type_node, array_domain_type);

  m_const_char_ptr
    = build_pointer_type (build_qualified_type (char_type_node,
						TYPE_QUAL_CONST));

  /* Replay the recorded events:  */
  timevar_push (TV_JIT_REPLAY);

  m_recording_ctxt->replay_into (this);

  /* Clean away the temporary references from recording objects
     to playback objects.  We have to do this now since the
     latter are GC-allocated, but the former don't mark these
     refs.  Hence we must stop using them before the GC can run.  */
  m_recording_ctxt->disassociate_from_playback ();

  timevar_pop (TV_JIT_REPLAY);

  if (!errors_occurred ())
    {
      int i;
      function *func;

      /* No GC can happen yet; process the cached source locations.  */
      handle_locations ();

      /* Postprocess the functions.  This could trigger GC.  */
      FOR_EACH_VEC_ELT (m_functions, i, func)
	{
	  gcc_assert (func);
	  func->postprocess ();
	}
    }
}

void
playback::context::
dump_generated_code ()
{
  char buf[4096];
  size_t sz;
  FILE *f_in = fopen (m_path_s_file, "r");
  if (!f_in)
    return;

  while ( (sz = fread (buf, 1, sizeof (buf), f_in)) )
    fwrite (buf, 1, sz, stderr);

  fclose (f_in);
}

static int
line_comparator (const void *lhs, const void *rhs)
{
  const playback::source_line *line_lhs = \
    *static_cast<const playback::source_line * const*> (lhs);
  const playback::source_line *line_rhs = \
    *static_cast<const playback::source_line * const*> (rhs);
  return line_lhs->get_line_num () - line_rhs->get_line_num ();
}

static int
location_comparator (const void *lhs, const void *rhs)
{
  const playback::location *loc_lhs = \
    *static_cast<const playback::location * const *> (lhs);
  const playback::location *loc_rhs = \
    *static_cast<const playback::location * const *> (rhs);
  return loc_lhs->get_column_num () - loc_rhs->get_column_num ();
}

void
playback::context::
handle_locations ()
{
  /* Create the source code locations, following the ordering rules
     imposed by the linemap API.

     line_table is a global.  */
  int i;
  source_file *file;

  FOR_EACH_VEC_ELT (m_source_files, i, file)
    {
      linemap_add (line_table, LC_ENTER, false, file->get_filename (), 0);

      /* Sort lines by ascending line numbers.  */
      file->m_source_lines.qsort (&line_comparator);

      int j;
      source_line *line;
      FOR_EACH_VEC_ELT (file->m_source_lines, j, line)
	{
	  int k;
	  location *loc;

	  /* Sort locations in line by ascending column numbers.  */
	  line->m_locations.qsort (&location_comparator);

	  /* Determine maximum column within this line.  */
	  gcc_assert (line->m_locations.length () > 0);
	  location *final_column =
	    line->m_locations[line->m_locations.length () - 1];
	  int max_col = final_column->get_column_num ();

	  linemap_line_start (line_table, line->get_line_num (), max_col);
	  FOR_EACH_VEC_ELT (line->m_locations, k, loc)
	    {
	      loc->m_srcloc =					   \
		linemap_position_for_column (line_table, loc->get_column_num ());
	    }
	}

      linemap_add (line_table, LC_LEAVE, false, NULL, 0);
    }

  /* Now assign them to tree nodes as appropriate.  */
  std::pair<tree, location *> *cached_location;

  FOR_EACH_VEC_ELT (m_cached_locations, i, cached_location)
    {
      tree t = cached_location->first;
      source_location srcloc = cached_location->second->m_srcloc;
#if 0
      inform (srcloc, "location of ");
      debug_tree (t);
#endif

      /* This covers expressions: */
      if (CAN_HAVE_LOCATION_P (t))
	SET_EXPR_LOCATION (t, srcloc);
      else if (CODE_CONTAINS_STRUCT(TREE_CODE(t), TS_DECL_MINIMAL))
	DECL_SOURCE_LOCATION (t) = srcloc;
      else
	{
	  /* Don't know how to set location on this node.  */
	  if (0)
	    {
	      fprintf (stderr, "can't set location on:");
	      debug_tree (t);
	      fprintf (stderr, "\n");
	    }
	}
    }
}


void
playback::context::
add_error (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  m_recording_ctxt->add_error_va (fmt, ap);
  va_end (ap);
}

void
playback::context::
add_error_va (const char *fmt, va_list ap)
{
  m_recording_ctxt->add_error_va (fmt, ap);
}

result::
result(void *dso_handle)
  : m_dso_handle(dso_handle)
{
}

result::~result()
{
  dlclose (m_dso_handle);
}

void *
result::
get_code (const char *funcname)
{
  void *code;
  const char *error;

  /* Clear any existing error.  */
  dlerror ();

  code = dlsym (m_dso_handle, funcname);

  if ((error = dlerror()) != NULL)  {
    fprintf(stderr, "%s\n", error);
  }

  return code;
}

/* Dealing with the linemap API.  */

playback::location *
playback::context::
new_location (const char *filename,
	      int line,
	      int column)
{
  /* Get the source_file for filename, creating if necessary.  */
  source_file *src_file = get_source_file (filename);
  /* Likewise for the line within the file.  */
  source_line *src_line = src_file->get_source_line (line);
  /* Likewise for the column within the line.  */
  location *loc = src_line->get_location (column);
  return loc;
}

void
playback::context::
set_tree_location (tree t, location *loc)
{
  m_cached_locations.safe_push (std::make_pair (t, loc));
}


playback::source_file *
playback::context::
get_source_file (const char *filename)
{
  /* Locate the file.
     For simplicitly, this is currently a linear search.
     Replace with a hash if this shows up in the profile.  */
  int i;
  source_file *file;
  tree ident_filename = get_identifier (filename);

  FOR_EACH_VEC_ELT (m_source_files, i, file)
    if (file->filename_as_tree () == ident_filename)
      return file;

  /* Not found.  */
  file = new source_file (ident_filename);
  m_source_files.safe_push (file);
  return file;
}

playback::source_file::source_file (tree filename) :
  m_source_lines (),
  m_filename (filename)
{
}

playback::source_line *
playback::source_file::
get_source_line (int line_num)
{
  /* Locate the line.
     For simplicitly, this is currently a linear search.
     Replace with a hash if this shows up in the profile.  */
  int i;
  source_line *line;

  FOR_EACH_VEC_ELT (m_source_lines, i, line)
    if (line->get_line_num () == line_num)
      return line;

  /* Not found.  */
  line = new source_line (this, line_num);
  m_source_lines.safe_push (line);
  return line;
}

playback::source_line::source_line (source_file *file, int line_num) :
  m_locations (),
  m_source_file (file),
  m_line_num (line_num)
{
}

playback::location *
playback::source_line::
get_location (int column_num)
{
  int i;
  location *loc;

  /* Another linear search that probably should be a hash table.  */
  FOR_EACH_VEC_ELT (m_locations, i, loc)
    if (loc->get_column_num () == column_num)
      return loc;

  /* Not found.  */
  loc = new location (this, column_num);
  m_locations.safe_push (loc);
  return loc;
}

playback::location::location (source_line *line, int column_num) :
  m_line (line),
  m_column_num(column_num)
{
}

playback::context *active_playback_ctxt;

} // namespace gcc::jit

} // namespace gcc
