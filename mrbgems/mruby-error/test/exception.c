#include <mruby.h>
#include <mruby/error.h>
#include <mruby/array.h>
#include <mruby/string.h>
#include <mruby/compile.h>
#include <mruby/variable.h>

/*
 * Reproduces the HackerOne #3677726 PoC (use-after-free in each_backtrace)
 * from a single C entry point.
 *
 * The PoC must run in a fresh mrb_state and call mrb_exc_backtrace()
 * immediately after mrb_load_string returns with mrb->exc set. At that
 * point c->ci is back at cibase[0] and *no Ruby code runs* in between,
 * which keeps the high-index callinfo slots in their stale/unreachable
 * state. Running the equivalent Ruby from the test VM fails to reproduce
 * because the test harness's own Ruby execution between raise and the
 * backtrace call overwrites those slots with live callinfo.
 *
 * Each recursive level builds a *unique* lambda and invokes it, so every
 * callinfo entry in the peak stack references a distinct RProc. When GC
 * sweeps them after the unwind, the ci->proc pointers become dangling;
 * the subsequent mrb_exc_backtrace() dereferences that freed memory.
 */
static const char *uaf_script =
  "def recurse(n)\n"
  "  local = lambda { n }\n"
  "  if n >= 80\n"
  "    raise 'trigger_uaf'\n"
  "  end\n"
  "  f = lambda { recurse(n + 1) }\n"
  "  f.call\n"
  "end\n"
  "recurse(1)\n";

static mrb_value
run_uaf_poc(mrb_state *mrb, mrb_value self)
{
  /*
   * Run the PoC inside a fresh mrb_state so the test harness's own Ruby
   * execution doesn't overwrite the stale callinfo slots between raise
   * and backtrace extraction (which is what prevents reproduction when
   * the same scenario is attempted from pure Ruby in the test VM).
   */
  mrb_state *mrb2 = mrb_open();
  if (!mrb2) return mrb_nil_value();

  mrbc_context *ctx = mrbc_context_new(mrb2);
  mrbc_filename(mrb2, ctx, "uaf_poc.rb");
  mrb_load_string_cxt(mrb2, uaf_script, ctx);

  mrb_int len = -1;
  if (mrb2->exc) {
    mrb_value exception = mrb_obj_value(mrb2->exc);

    /* Churn the heap so freed RProc slots get reused with other data —
       turning a silent UAF into an observable crash (SIGSEGV or ASan
       error). Mirrors the H1 PoC's post-raise loop. */
    for (int i = 0; i < 20; i++) mrb_full_gc(mrb2);
    for (int i = 0; i < 10000; i++) mrb_str_new_lit(mrb2, "ZZZZZZZZZZZZZZZZ");

    /* Unsafe path: walks cibase[ciidx] down to 0, including stale entries. */
    mrb_value backtrace = mrb_exc_backtrace(mrb2, exception);
    len = RARRAY_LEN(backtrace);
  }

  mrbc_context_free(mrb2, ctx);
  mrb_close(mrb2);

  return mrb_fixnum_value(len);
}

static mrb_value
protect_cb(mrb_state *mrb, mrb_value b)
{
  return mrb_yield_argv(mrb, b, 0, NULL);
}

static mrb_value
run_protect(mrb_state *mrb, mrb_value self)
{
  mrb_value b;
  mrb_value ret[2];
  mrb_bool state;
  mrb_get_args(mrb, "&", &b);
  ret[0] = mrb_protect(mrb, protect_cb, b, &state);
  ret[1] = mrb_bool_value(state);
  return mrb_ary_new_from_values(mrb, 2, ret);
}

static mrb_value
run_ensure(mrb_state *mrb, mrb_value self)
{
  mrb_value b, e;
  mrb_get_args(mrb, "oo", &b, &e);
  return mrb_ensure(mrb, protect_cb, b, protect_cb, e);
}

static mrb_value
run_rescue(mrb_state *mrb, mrb_value self)
{
  mrb_value b, r;
  mrb_get_args(mrb, "oo", &b, &r);
  return mrb_rescue(mrb, protect_cb, b, protect_cb, r);
}

static mrb_value
run_rescue_exceptions(mrb_state *mrb, mrb_value self)
{
  mrb_value b, r;
  struct RClass *cls[1];
  mrb_get_args(mrb, "oo", &b, &r);
  cls[0] = E_TYPE_ERROR;
  return mrb_rescue_exceptions(mrb, protect_cb, b, protect_cb, r, 1, cls);
}

void
mrb_mruby_error_gem_test(mrb_state *mrb)
{
  struct RClass *cls;

  cls = mrb_define_class(mrb, "ExceptionTest", mrb->object_class);
  mrb_define_module_function(mrb, cls, "mrb_protect", run_protect, MRB_ARGS_NONE() | MRB_ARGS_BLOCK());
  mrb_define_module_function(mrb, cls, "mrb_ensure", run_ensure, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, cls, "mrb_rescue", run_rescue, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, cls, "mrb_rescue_exceptions", run_rescue_exceptions, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, cls, "run_uaf_poc", run_uaf_poc, MRB_ARGS_NONE());
}
