#include "mruby.h"
#include "mruby/class.h"
#include "mruby/proc.h"
#include "mruby/array.h"
#include "mruby/proc.h"
#include "mruby/variable.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define FIBER_STACK_INIT_SIZE 64
#define FIBER_CI_INIT_SIZE 8

typedef struct {
  int argc;
  mrb_value* argv;
  struct RProc* proc;
  pthread_t thread;
  mrb_state* mrb;
  mrb_state* oldmrb;
  mrb_value self;
  mrb_value result;
} mrb_future_context;

enum future_status {
  FUTURE_RUNNING,
  FUTURE_FINISH,
};

static void*
mrb_future_func(void* data) {
  mrb_value result;

  mrb_future_context *context = (mrb_future_context *) data;
  struct mrb_state *mrb = context->mrb;
  struct mrb_state *oldmrb = context->oldmrb;
  mrb_value self = context->self;

  result = mrb_yield_with_class(mrb, mrb_obj_value(context->proc), context->argc, context->argv, context->self, mrb_class(mrb, context->self));

  mrb_iv_set(oldmrb, self, mrb_intern_cstr(oldmrb, "value"), result);
  mrb_iv_set(oldmrb, self, mrb_intern_cstr(oldmrb, "_state"), mrb_fixnum_value(FUTURE_FINISH));

  return NULL;
}

static mrb_value
mrb_future_init(mrb_state *oldmrb, mrb_value self)
{
  static const struct mrb_context mrb_context_zero = { 0 };
  mrb_state *mrb = mrb_open();
  mrb_future_context *context = (mrb_future_context *) malloc(sizeof(mrb_future_context));
  struct mrb_context *c;
  struct RProc *p;
  mrb_callinfo *ci;
  mrb_value blk;
  size_t slen;
  int argc;
  mrb_value* argv;

  *mrb = *oldmrb;
  mrb_get_args(mrb, "*&", &argv, &argc, &blk);

  p = mrb_proc_ptr(blk);

  c = (struct mrb_context*)mrb_malloc(mrb, sizeof(struct mrb_context));
  *c = mrb_context_zero;
  mrb->c = c;
  context->mrb = mrb;
  context->oldmrb = oldmrb;
  context->proc = mrb_proc_ptr(blk);
  context->argc = argc;
  context->argv = argv;
  context->self = self;

  /* initialize VM stack */
  slen = FIBER_STACK_INIT_SIZE;
  if (!MRB_PROC_CFUNC_P(p) && p->body.irep->nregs > slen) {
    slen += p->body.irep->nregs;
  }
  c->stbase = (mrb_value *)mrb_malloc(mrb, slen*sizeof(mrb_value));
  c->stend = c->stbase + slen;
  c->stack = c->stbase;

#ifdef MRB_NAN_BOXING
  {
    mrb_value *p = c->stbase;
    mrb_value *pend = c->stend;
    
    while (p < pend) {
      SET_NIL_VALUE(*p);
      p++;
    }
  }
#else
  memset(c->stbase, 0, slen * sizeof(mrb_value));
#endif

  /* copy future object(self) from a block */
  c->stack[0] = self;

  /* initialize callinfo stack */
  c->cibase = (mrb_callinfo *)mrb_calloc(mrb, FIBER_CI_INIT_SIZE, sizeof(mrb_callinfo));
  c->ciend = c->cibase + FIBER_CI_INIT_SIZE;
  c->ci = c->cibase;
  c->ci->stackent = c->stack;

  /* adjust return callinfo */
  ci = c->ci;
  ci->target_class = p->target_class;
  ci->proc = p;
  if (!MRB_PROC_CFUNC_P(p)) {
    ci->pc = p->body.irep->iseq;
    ci->nregs = p->body.irep->nregs;
  }
  ci[1] = ci[0];
  c->ci++;                      /* push dummy callinfo */

  mrb_iv_set(oldmrb, self, mrb_intern_cstr(oldmrb, "_context"), mrb_cptr_value(oldmrb, context));
  mrb_iv_set(oldmrb, self, mrb_intern_cstr(oldmrb, "_state"), mrb_fixnum_value(FUTURE_RUNNING));

  pthread_create(&context->thread, NULL, &mrb_future_func, (void *)context);

  return self;
}

static mrb_value
mrb_future_value(mrb_state *mrb, mrb_value self)
{
  mrb_future_context *context = (mrb_future_context *)mrb_cptr(mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "_context")));

  if (mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "_state"))) == FUTURE_RUNNING) {
    pthread_join(context->thread, NULL);
  }
  return mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "value"));
}

static mrb_value
mrb_future_state(mrb_state *mrb, mrb_value self)
{
  mrb_sym res;

  switch(mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "_state")))) {
  case FUTURE_RUNNING:
    res = mrb_intern_cstr(mrb, "running");
    break;

  case FUTURE_FINISH:
    res = mrb_intern_cstr(mrb, "finish");
    break;

  default:
    res = mrb_intern_cstr(mrb, "unkown");
    break;
  }

  return mrb_symbol_value(res);
}


static mrb_value
mrb_future_peek(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "value"));
}

void
mrb_mruby_future_gem_init(mrb_state *mrb) {
  struct RClass *future_class = mrb_define_class(mrb, "Future", mrb->object_class);
  mrb_define_method(mrb, future_class, "initialize", mrb_future_init, MRB_ARGS_ANY());
  mrb_define_method(mrb, future_class, "value", mrb_future_value, MRB_ARGS_NONE());
  mrb_define_method(mrb, future_class, "state", mrb_future_state, MRB_ARGS_NONE());
  mrb_define_method(mrb, future_class, "peek", mrb_future_peek, MRB_ARGS_NONE());
}

void
mrb_mruby_future_gem_final(mrb_state *mrb) {
}
