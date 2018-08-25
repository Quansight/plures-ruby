/* BSD 3-Clause License
 *
 * Copyright (c) 2018, Quansight and Sameer Deshmukh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* File containing the majority implementation of the Ruby XND wrapper.
 *
 * Author: Sameer Deshmukh (@v0dro)
 */

#include "ruby_xnd_internal.h"

static VALUE cRubyXND;
static VALUE cRubyXND_MBlock;
static VALUE rb_eNotImplementedError;
static const rb_data_type_t MemoryBlockObject_type;

/****************************************************************************/
/*                           MemoryBlock Object                             */
/****************************************************************************/

/* The MemoryBlockObject is shared among several XND views/objects. */
typedef struct MemoryBlockObject {
  VALUE type;        /* type owner (ndtype) */  
  xnd_master_t *xnd; /* memblock owner */
} MemoryBlockObject;

#define GET_MBLOCK(obj, mblock_p) do {                              \
    TypedData_Get_Struct((obj), MemoryBlockObject,                  \
                         &MemoryBlockObject_type, (mblock_p));      \
} while (0)
#define MAKE_MBLOCK(self, mblock_p) TypedData_Make_Struct(self, MemoryBufferObject, \
                                                          &MemoryBufferObject_type, mblock_p)
#define WRAP_MBLOCK(self, mblock_p) TypedData_Wrap_Struct(self,         \
                                                          &MemoryBufferObject_type, mblock_p)

/* Mark Ruby objects within MemoryBlockObject. */
static void
MemoryBlockObject_dmark(void *self)
{
  MemoryBlockObject *mblock = (MemoryBlockObject*)self;

  rb_gc_mark(mblock->type);
}

static void
MemoryBlockObject_dfree(void *self)
{
  MemoryBlockObject *mblock = (MemoryBlockObject*)self;

  xnd_del(mblock->xnd);
  mblock->xnd = NULL;
  xfree(mblock);
}

static size_t
MemoryBlockObject_dsize(const void *self)
{
  return sizeof(MemoryBlockObject);
}

static const rb_data_type_t MemoryBlockObject_type = {
  .wrap_struct_name = "MemoryBlockObject",
  .function = {
    .dmark = MemoryBlockObject_dmark,
    .dfree = MemoryBlockObject_dfree,
    .dsize = MemoryBlockObject_dsize,
    .reserved = {0,0},
  },
  .parent = 0,
  .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Allocate a MemoryBlockObject and return a pointer to allocated memory. */
static MemoryBlockObject *
mblock_alloc(void)
{
  MemoryBlockObject *self;

  self = ALLOC(MemoryBlockObject);
  if (self == NULL) {
    
  }

  self->type = NULL;
  self->xnd = NULL;

  return self;
}

/* Allocate a MemoryBlockObject and wrap it in a Ruby object. */
static VALUE
mblock_allocate(void)
{
  MemoryBlockObject *self = mblock_alloc();

  return WRAP_MBLOCK(cRubyXND_MBlock, self);
}

/* Create empty mblock with no data. */
static VALUE
mblock_empty(VALUE type)
{
  NDT_STATIC_CONTEXT(ctx);
  MemoryBlockObject *mblock;
  NdtObject *ndt;
  VALUE self;
  
  if (!NDT_CHECK_TYPE(type)) {
    rb_raise(rb_eArgError, "expected NDT object.");
  }

  GET_NDT(type, ndt);

  mblock = mblock_alloc();
  mblock->xnd = xnd_empty_from_type(CONST_NDT(ndt), XND_OWN_EMBEDDED, &ctx);
  if (mblock->xnd == NULL) {
    
  }
  mblock->type = type;

  return MAKE_MBLOCK(cRubyXND_MBlock, mblock);
}

static int64_t
get_int(VALUE data, int64_t min, int64_t max)
{
  int64_t x;

  x = NUM2LL(data);
  if (x < min || x > max) {
    // TODO: error about integer out of range.
    return -1;
  }

  return x;
}

static int
mblock_init(xnd_t * const x, VALUE data)
{
  NDT_STATIC_CONTEXT(ctx);
  const ndt_t * const t = x->type;

  if (!check_invariants(t)) {
    /* raise error */
    return -1;
  }

  if (ndt_is_abstract(t)) {
    // rb_raise(rb_eTypeError, "xnd has abstract type.");
    return -1;
  }

  /* set missing value. */
  if (ndt_is_optional(t)) {
    if (t->ndim > 0) {
      return -1;
    }

    if (v == Qnil) {
      xnd_set_na(x);
      return 0;
    }

    xnd_set_valid(x);
  }

  switch (t->tag) {
  case FixedDim: {
    const int64_t shape = t->FixedDim.shape;
    int64_t i;

    Check_Type(data, T_ARRAY);

    if (RARRAY_LEN(data) != shape) {
      // TODO: error
    }

    for (i = 0; i < shape; i++) {
      xnd_t next = xnd_fixed_dim_next(x, i);
      VALUE rb_index[1] = { LL2NUM(i) };
      if (mblock_init(&next, rb_ary_aref(1, rb_index, data)) < 0) {
        // error
      }

      return 0;
    }
  }
  case Int64: {
    int64_t tmp = get_int(data, INT64_MIN, INT64_MAX);
    if (tmp == -1) { // error occured
      
    }
    PACK_SINGLE(x->ptr, tmp, int64_t, t->flags);
    return 0;
  }
  default:                      /* TODO: remove after implemented all dtypes. */
    rb_raise(rb_eNotImplementedError, "invalid type tag.");
  }
}

/* Create mblock from NDT type. 
 *
 * @param type - NDT Ruby object.
 * @param data - Data as a Ruby object.
 */
static VALUE
mblock_from_typed_value(VALUE type, VALUE data)
{
  VALUE mblock;
  MemoryBlockObject *mblock_p;

  mblock = mblock_empty(type);
  
  GET_MBLOCK(mblock, mblock_p);
  
  if (mblock_init(mblock_p->xnd->master, data) < 0) {
    
  }

  return WRAP_MBLOCK(cRubyXND_MBlock, mblock_p);
}

/****************************************************************************/
/*                                 xnd object                               */
/****************************************************************************/

typedef struct {
  VALUE mblock;              /* owner of the primary type and memory block */
  VALUE type;                /* owner of the current type */
  xnd_t xnd;                 /* typed view, does not own anything */
} XndObject;

#define GET_XND(obj, xnd_p) do {                        \
    TypedData_Get_Struct((obj), XndObject,              \
                         &XndObject_type, (ndt_p));     \
  } while (0)
#define MAKE_XND(klass, mblock_p) TypedData_Make_Struct(klass, XndObject, \
                                                    &XndObject_type, xnd_p)
#define WRAP_XND(klass, mblock_p) TypedData_Wrap_Struct(klass, &XndObject_type, xnd_p)

static VALUE
RubyXND_from_mblock(VALUE self, VALUE mblock)
{
  
}

/* Allocator for RubyXND object. Called by Ruby before initialize. */
static VALUE
RubyXND_allocate(VALUE self)
{
  XndObject *xnd;

  xnd = ALLOC(XndObject);
  if (xnd == NULL) {
    
  }

  xnd->mblock = NULL;
  xnd->type = NULL;
  xnd->xnd.bitmap.data = NULL;
  xnd->xnd.bitmap.size = 0;
  xnd->xnd.bitmap.next = NULL;
  xnd->xnd.index = 0;
  xnd->xnd.type  = NULL;
  xnd->xnd.ptr = NULL;

  return WRAP_XND(self, xnd);
}

/* Initialize a RubyXND object. */
static VALUE
RubyXND_initialize(VALUE self, VALUE type, VALUE data)
{
  VALUE mblock;

  mblock = mblock_from_typed_value(type, data);

  return RubyXND_from_mblock(self, mblock);
}

void Init_ruby_xnd(void)
{
  /* init classes */
  cRubyXND = rb_define_class("RubyXND", rb_cObject);
  cRubyXND_MBlock = rb_define_class_under(cRubyXND, "MBlock", rb_cObject);

  /* init errors */
  rb_eNotImplementedError = rb_define_class("NotImplementedError", rb_eScriptError);

  /* initializers */
  rb_define_alloc_func(cRubyXND, RubyXND_allocate);
  rb_define_method(cRubyXND, "initialize", RubyXND_initialize, 2);
}
