/* Copyright 2014 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
-
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */



#include <unistd.h>

#include "s7.h"
#include "s7webserver/s7webserver.h"

#include <QCoreApplication>
#include <QRegExp>
#include <QStringList>
#include <QDebug>

#include <qhttpserver.h>
#include <qhttprequest.h>
#include <qhttpresponse.h>

#include "../common/nsmtracker.h"
#include "../common/OS_settings_proc.h"
#include "../common/placement_proc.h"
#include "../common/visual_proc.h"

#include "scheme_proc.h"
#include "s7extra_proc.h"

#include "../api/api_common_proc.h"
#include "../api/api_proc.h"


extern struct TEvent tevent;

static s7_scheme *s7;
static s7webserver_t *s7webserver;



extern "C" {
  void init_radium_s7(s7_scheme *s7);
}

namespace{
  static int g_evals = 0;
  
  struct ScopedEvalTracker{
    ScopedEvalTracker(){
      R_ASSERT(THREADING_is_main_thread());
      g_evals++;
    }
    ~ScopedEvalTracker(){
      g_evals--;
    }
  };

  static int g_gc_is_off = 0;
  
  struct ScopedGcDisabler{
    ScopedGcDisabler(){
      g_gc_is_off++;
      if (g_gc_is_off==1)
        s7_gc_on(s7, false);
    }

    ~ScopedGcDisabler(){
      R_ASSERT_RETURN_IF_FALSE(g_gc_is_off >= 0);
      g_gc_is_off--;
      if (g_gc_is_off==0)
        s7_gc_on(s7, true);
    }
  };

  struct ProtectedS7Pointer{
    s7_pointer v;
    int pos;
    
    ProtectedS7Pointer(s7_pointer val)
      :v(val)
      ,pos(s7_gc_protect(s7, v))
    {      
    }
    
    ~ProtectedS7Pointer(){
      R_ASSERT_NON_RELEASE(s7_gc_protected_at(s7, pos)==v);
      s7_gc_unprotect_at(s7, pos);
    }
    
    // Or just use ".v".
    s7_pointer get(void){
      return v;
    }
  };

  typedef ProtectedS7Pointer Protect;
}


static s7_pointer find_and_protect_scheme_func(const char *funcname){
  s7_pointer scheme_func = s7_name_to_value(s7, funcname);
  s7_gc_protect(s7, scheme_func);
  return scheme_func;
}

static s7_pointer place_to_ratio(const Place *p){
  R_ASSERT(p->dividor != 0);

  s7_Int temp = p->line*p->dividor; // use temp variable (of type s7_Int, which is at least 64 bit) to make sure it doesn't overflow.
  s7_Int a = temp + p->counter;
  s7_Int b = p->dividor;
  s7_pointer ratio = s7_make_ratio(s7, a, b);
  
  //fprintf(stderr,"\n\n          a: %d, b: %d. is_number: %d, is_integer: %d, is_ratio: %d, is_real: %d\n\n\n", (int)a, (int)b, s7_is_number(ratio),s7_is_integer(ratio),s7_is_ratio(ratio),s7_is_real(ratio));  
  return ratio;
}

                           
static Place *ratio_to_place(s7_pointer ratio){
  s7_Int num = s7_numerator(ratio);
  s7_Int den = s7_denominator(ratio);

  Place ret = place_from_64b(num, den);

  return PlaceCreate(ret.line, ret.counter, ret.dividor);
}

static Place number_to_place(s7_pointer number){

  if (s7_is_ratio(number)) {
    s7_Int num = s7_numerator(number);
    s7_Int den = s7_denominator(number);

    return place_from_64b(num, den);
  }

  if (s7_is_integer(number))
    return place((int)s7_integer(number), 0, 1);
  
  if (s7_is_real(number)) {
    Place place;
    Double2Placement(s7_real(number), &place);
    
#if !defined(RELEASE)
    RError("scheme.cpp/number_to_place: input parameter was a real. is_number: %d, is_integer: %d, is_ratio: %d, is_real: %d, value: %f, is_complex: %d, is_ulong: %d\n\n\n",s7_is_number(number),s7_is_integer(number),s7_is_ratio(number),s7_is_real(number),s7_number_to_real(s7,number),s7_is_complex(number),s7_is_ulong(number));
#endif

    return place;
  }

  
  RError("scheme.cpp/number_to_place: input parameter was not a number. returning 0. is_number: %d, is_integer: %d, is_ratio: %d, is_real: %d, value: %f, is_complex: %d, is_ulong: %d\n\n\n",s7_is_number(number),s7_is_integer(number),s7_is_ratio(number),s7_is_real(number),s7_number_to_real(s7,number),s7_is_complex(number),s7_is_ulong(number));
    
  return place(0,0,1);
}

bool s7extra_is_place(s7_pointer place){
  return s7_is_rational(place);
}

Place s7extra_place(s7_scheme *s7, s7_pointer place){
  return number_to_place(place); // Should we allow floating numbers? (i.e. not give error message for it) Yes, definitely.
}

s7_pointer s7extra_make_place(s7_scheme *radiums7_sc, const Place place){
  return place_to_ratio(&place);
}

bool s7extra_is_dyn(s7_pointer dyn){
  return s7_is_number(dyn) || s7_is_string(dyn) || s7_is_boolean(dyn) || s7_is_hash_table(dyn) || s7_is_vector(dyn) || s7_is_pair(dyn);
}

// 's_hash' has been gc-protected before calling
static hash_t *s7extra_hash(s7_scheme *s7, s7_pointer s_hash){
  int hash_size = 16;

  hash_t *r_hash = HASH_create(hash_size); // would be nice to know size of s_hash so we didn't have to rehash

  R_ASSERT_RETURN_IF_FALSE2(s7_is_hash_table(s_hash), r_hash);

  ProtectedS7Pointer protect(s_hash); // Not sure if this is necessary. (doesn't the iterator below hold a pointer to the vector?)
    
  ProtectedS7Pointer iterator(s7_make_iterator(s7, s_hash));

  int num_elements = 0;
  while(true){
    s7_pointer val = s7_iterate(s7, iterator.v);

    if (s7_iterator_is_at_end(s7, iterator.v))
      break;

    s7_pointer key = s7_car(val);

    const char *key_name;

    if (s7_is_symbol(key))
      key_name = s7_symbol_name(key);
    else{
      handleError("Only symbols are supported as hash table keys");
      key_name = "___radium__illegal_key type";
    }

    HASH_put_dyn(r_hash,
                 key_name,
                 s7extra_dyn(s7, s7_cdr(val)));

    num_elements++;
    if (num_elements > hash_size*2){
      r_hash = HASH_copy(r_hash); // rehash
      hash_size = num_elements;
    }
  }

  return r_hash;
}

static dynvec_t s7extra_array(s7_scheme *s7, s7_pointer vector){
  ProtectedS7Pointer protect(vector); // Not sure if this is necessary. (doesn't the iterator below hold a pointer to the vector?)
  
  dynvec_t dynvec = {0};

  ProtectedS7Pointer iterator(s7_make_iterator(s7, vector));

  while(true){
    s7_pointer val = s7_iterate(s7, iterator.v);

    if (s7_iterator_is_at_end(s7, iterator.v))
      break;

    DYNVEC_push_back(&dynvec, s7extra_dyn(s7, val));
  }

  return dynvec;
}

dyn_t s7extra_dyn(s7_scheme *s7, s7_pointer s){
  
  if (s7_is_integer(s))
    return DYN_create_int(s7_integer(s));

  if (s7_is_ratio(s))
    return DYN_create_ratio(make_ratio(s7_numerator(s), s7_denominator(s)));

  if (s7_is_number(s))
    return DYN_create_float(s7_number_to_real(s7, s));

  if (s7_is_string(s))
    return DYN_create_string_from_chars(s7_string(s));

  if (s7_is_boolean(s))
    return DYN_create_bool(s7_boolean(s7, s));

  if (s7_is_hash_table(s))
    return DYN_create_hash(s7extra_hash(s7, s));

  if (s7_is_vector(s) || s7_is_pair(s))
    return DYN_create_array(s7extra_array(s7, s));

  if (s7_is_null(s7, s)){
    dynvec_t vec = {0};
    return DYN_create_array(vec);
  }    
   
  GFX_Message(NULL, "s7extra_dyn: Unsupported s7 type");
  return DYN_create_bool(false);
}

static s7_pointer hash_to_s7(s7_scheme *sc, const hash_t *r_hash){
  
  ProtectedS7Pointer s_hash(s7_make_hash_table(sc, HASH_get_num_elements(r_hash)));

  dynvec_t dynvec = HASH_get_values(r_hash);
  vector_t keys = HASH_get_keys(r_hash);

  for(int i = 0 ; i < dynvec.num_elements ; i++){
    s7_hash_table_set(sc,
                      s_hash.v,
                      s7_make_symbol(sc, (char*)keys.elements[i]),
                      s7extra_make_dyn(sc, dynvec.elements[i]));
  }

  return s_hash.v;
}

static s7_pointer dynvec_to_s7(s7_scheme *sc, const dynvec_t &dynvec){
  
  ProtectedS7Pointer s_vec(s7_make_vector(sc, dynvec.num_elements));

  for(int i = 0 ; i < dynvec.num_elements ; i++){
    s7_vector_set(sc,
                  s_vec.v,
                  i,
                  s7extra_make_dyn(sc, dynvec.elements[i]));
  }

  return s_vec.v;
}

s7_pointer s7extra_make_dyn(s7_scheme *radiums7_sc, const dyn_t dyn){
  
  switch(dyn.type){
    case UNINITIALIZED_TYPE:
      return s7_unspecified(radiums7_sc);
    case STRING_TYPE:
      return s7_make_string(radiums7_sc, STRING_get_chars(dyn.string));
    case INT_TYPE:
      return s7_make_integer(radiums7_sc, dyn.int_number);
    case FLOAT_TYPE:
      return s7_make_real(radiums7_sc, dyn.float_number);
    case HASH_TYPE:
      return hash_to_s7(radiums7_sc, dyn.hash);
    case ARRAY_TYPE:
      return dynvec_to_s7(radiums7_sc, *dyn.array);
    case RATIO_TYPE:
      return s7_make_ratio(radiums7_sc, dyn.ratio->numerator, dyn.ratio->denominator);
    case BOOL_TYPE:
      return s7_make_boolean(radiums7_sc, dyn.bool_number);
  }

  return s7_make_boolean(radiums7_sc, false);
}


func_t *s7extra_func(s7_scheme *s7, s7_pointer func){
  return (func_t*)func;
}

void s7extra_callFunc_void_void(func_t *func){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7, 0)
          );
}

void s7extra_callFunc2_void_void(const char *funcname){
  s7extra_callFunc_void_void((func_t*)s7_name_to_value(s7, funcname));
}

double s7extra_callFunc_double_void(func_t *func){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7, 0)
                           );

  if (!s7_is_number(ret)){
    handleError("Callback did not return a double");
    return -1.0;
  }else{
    return s7_number_to_real(s7, ret);
  }
}

double s7extra_callFunc2_double_void(const char *funcname){
  return s7extra_callFunc_double_void((func_t*)s7_name_to_value(s7, funcname));
}


bool s7extra_callFunc_bool_void(func_t *func){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7, 0)
                           );

  if (!s7_is_boolean(ret)){
    handleError("Callback did not return a boolean");
    return false;
  }else{
    return s7_boolean(s7, ret);
  }
}

bool s7extra_callFunc2_bool_void(const char *funcname){
  return s7extra_callFunc_bool_void((func_t*)s7_name_to_value(s7, funcname));
}


dyn_t s7extra_callFunc_dyn_void(func_t *func){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7, 0)
                           );

  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_void(const char *funcname){
  return s7extra_callFunc_dyn_void((func_t*)s7_name_to_value(s7, funcname));
}

dyn_t s7extra_callFunc_dyn_int(func_t *func, int64_t arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   1,
                                   s7_make_integer(s7, arg1)
                                   )
                           );
  

  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_int(const char *funcname, int64_t arg1){
  return s7extra_callFunc_dyn_int((func_t*)s7_name_to_value(s7, funcname), arg1);
}


dyn_t s7extra_callFunc_dyn_int_int_int(func_t *func, int64_t arg1, int64_t arg2, int64_t arg3){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   3,
                                   s7_make_integer(s7, arg1),
                                   s7_make_integer(s7, arg2),
                                   s7_make_integer(s7, arg3)
                                   )
                           );
  

  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_int_int_int(const char *funcname, int64_t arg1, int64_t arg2, int64_t arg3){
  return s7extra_callFunc_dyn_int_int_int((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3);
}


dyn_t s7extra_callFunc_dyn_int_int_int_dyn_dyn_dyn(func_t *func, int64_t arg1, int64_t arg2, int64_t arg3, dyn_t arg4, dyn_t arg5, dyn_t arg6){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   6,
                                   Protect(s7_make_integer(s7, arg1)).v, // Need to protect everything, even integers, since s7extra_make_dyn may allocate over 256 objects.
                                   Protect(s7_make_integer(s7, arg2)).v,
                                   Protect(s7_make_integer(s7, arg3)).v,
                                   Protect(s7extra_make_dyn(s7, arg4)).v,
                                   Protect(s7extra_make_dyn(s7, arg5)).v,
                                   Protect(s7extra_make_dyn(s7, arg6)).v
                                   )
                           );
  

  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_int_int_int_dyn_dyn_dyn(const char *funcname, int64_t arg1, int64_t arg2, int64_t arg3, dyn_t arg4, dyn_t arg5, dyn_t arg6){
  return s7extra_callFunc_dyn_int_int_int_dyn_dyn_dyn((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4, arg5, arg6);
}

dyn_t s7extra_callFunc_dyn_int_int_int_dyn_dyn_dyn_dyn_dyn(func_t *func, int64_t arg1, int64_t arg2, int64_t arg3, dyn_t arg4, dyn_t arg5, dyn_t arg6, dyn_t arg7, dyn_t arg8){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   8,
                                   Protect(s7_make_integer(s7, arg1)).v,
                                   Protect(s7_make_integer(s7, arg2)).v,
                                   Protect(s7_make_integer(s7, arg3)).v,
                                   Protect(s7extra_make_dyn(s7, arg4)).v,
                                   Protect(s7extra_make_dyn(s7, arg5)).v,
                                   Protect(s7extra_make_dyn(s7, arg6)).v,
                                   Protect(s7extra_make_dyn(s7, arg7)).v,
                                   Protect(s7extra_make_dyn(s7, arg8)).v
                                   )
                           );
  
  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_int_int_int_dyn_dyn_dyn_dyn_dyn(const char *funcname, int64_t arg1, int64_t arg2, int64_t arg3, dyn_t arg4, dyn_t arg5, dyn_t arg6, dyn_t arg7, dyn_t arg8){
  return s7extra_callFunc_dyn_int_int_int_dyn_dyn_dyn_dyn_dyn((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}


dyn_t s7extra_callFunc_dyn_dyn_dyn_dyn_int(func_t *func, dyn_t arg1, dyn_t arg2, dyn_t arg3, int64_t arg4){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   4,
                                   Protect(s7extra_make_dyn(s7, arg1)).v,
                                   Protect(s7extra_make_dyn(s7, arg2)).v,
                                   Protect(s7extra_make_dyn(s7, arg3)).v,
                                   Protect(s7_make_integer(s7, arg4)).v
                                   )
                           );
  

  return s7extra_dyn(s7, ret);
}

dyn_t s7extra_callFunc2_dyn_dyn_dyn_dyn_int(const char *funcname, dyn_t arg1, dyn_t arg2, dyn_t arg3, int64_t arg4){
  return s7extra_callFunc_dyn_dyn_dyn_dyn_int((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4);
}


void s7extra_callFunc_void_int_charpointer_dyn(func_t *func, int64_t arg1, const char* arg2, dyn_t arg3){
  ScopedEvalTracker eval_tracker;
    
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  3,
                  Protect(s7_make_integer(s7, arg1)).v,
                  Protect(s7_make_string(s7, arg2)).v,
                  Protect(s7extra_make_dyn(s7, arg3)).v
                  )
          );
}

void s7extra_callFunc2_void_int_charpointer_dyn(const char *funcname, int64_t arg1, const char* arg2, dyn_t arg3){
  s7extra_callFunc_void_int_charpointer_dyn((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3);
}

void s7extra_callFunc_void_int_charpointer_int(func_t *func, int64_t arg1, const char* arg2, int64_t arg3){
  ScopedEvalTracker eval_tracker;
    
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  3,
                  s7_make_integer(s7, arg1),
                  s7_make_string(s7, arg2),
                  s7_make_integer(s7, arg3)
                  )
          );
}

void s7extra_callFunc2_void_int_charpointer_int(const char *funcname, int64_t arg1, const char* arg2, int64_t arg3){
  s7extra_callFunc_void_int_charpointer_int((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3);
}

void s7extra_callFunc_void_int_bool(func_t *func, int64_t arg1, bool arg2){
  ScopedEvalTracker eval_tracker;
    
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  2,
                  s7_make_integer(s7, arg1),
                  s7_make_boolean(s7, arg2)
                  )
          );
}

void s7extra_callFunc2_void_int_bool(const char *funcname, int64_t arg1, bool arg2){
  s7extra_callFunc_void_int_bool((func_t*)s7_name_to_value(s7, funcname), arg1, arg2);
}

void s7extra_callFunc_void_int(func_t *func, int64_t arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7, 1, s7_make_integer(s7, arg1))
          );
}

void s7extra_callFunc2_void_int(const char *funcname, int64_t arg1){
  s7extra_callFunc_void_int((func_t*)s7_name_to_value(s7, funcname), arg1);
}

void s7extra_callFunc_void_double(func_t *func, double arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7, 1, s7_make_real(s7, arg1))
          );
}

void s7extra_callFunc2_void_double(const char *funcname, double arg1){
  s7extra_callFunc_void_double((func_t*)s7_name_to_value(s7, funcname), arg1);
}

void s7extra_callFunc_void_bool(func_t *func, bool arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7, 1, s7_make_boolean(s7, arg1))
          );
}

void s7extra_callFunc2_void_bool(const char *funcname, bool arg1){
  s7extra_callFunc_void_double((func_t*)s7_name_to_value(s7, funcname), arg1);
}

void s7extra_callFunc_void_dyn(func_t *func, dyn_t arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  1,
                  Protect(s7extra_make_dyn(s7, arg1)).v
                  )
          );
}

void s7extra_callFunc2_void_dyn(const char *funcname, dyn_t arg1){
  s7extra_callFunc_void_dyn((func_t*)s7_name_to_value(s7, funcname), arg1);
}

void s7extra_callFunc_void_charpointer(func_t *func, const char* arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  1,
                  s7_make_string(s7, arg1)
                  )
          );
}

void s7extra_callFunc2_void_charpointer(const char *funcname, const char* arg1){
  s7extra_callFunc_void_charpointer((func_t*)s7_name_to_value(s7, funcname), arg1);
}

void s7extra_callFunc_void_int_charpointer(func_t *func, int64_t arg1, const char* arg2){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  2,
                  s7_make_integer(s7, arg1),
                  s7_make_string(s7, arg2)
                  )
          );
}

void s7extra_callFunc2_void_int_charpointer(const char *funcname, int64_t arg1, const char* arg2){
  s7extra_callFunc_void_int_charpointer((func_t*)s7_name_to_value(s7, funcname), arg1, arg2);
}

bool s7extra_callFunc_bool_int_charpointer(func_t *func, int64_t arg1, const char* arg2){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   2,
                                   s7_make_integer(s7, arg1),
                                   s7_make_string(s7, arg2)
                                   )
                           );
  
  if(!s7_is_boolean(ret)){
    handleError("Callback did not return a boolean");
    return false;
  }else{
    return s7_boolean(s7, ret);
  }
}

bool s7extra_callFunc2_bool_int_charpointer(const char *funcname, int64_t arg1, const char* arg2){
  return s7extra_callFunc_bool_int_charpointer((func_t*)s7_name_to_value(s7, funcname), arg1, arg2);
}

void s7extra_callFunc_void_int_charpointer_bool_bool(func_t *func, int64_t arg1, const char* arg2, bool arg3, bool arg4){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  4,
                  s7_make_integer(s7, arg1),
                  s7_make_string(s7, arg2),
                  s7_make_boolean(s7, arg3),
                  s7_make_boolean(s7, arg4)
                  )
          );
}

void s7extra_callFunc2_void_int_charpointer_bool_bool(const char *funcname, int64_t arg1, const char* arg2, bool arg3, bool arg4){
  s7extra_callFunc_void_int_charpointer_bool_bool((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4);
}

void s7extra_callFunc_void_int_int(func_t *func, int64_t arg1, int64_t arg2){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  2,
                  s7_make_integer(s7, arg1),
                  s7_make_integer(s7, arg2)
                  )
          );
}

void s7extra_callFunc2_void_int_int(const char *funcname, int64_t arg1, int64_t arg2){
  s7extra_callFunc_void_int_int((func_t*)s7_name_to_value(s7, funcname), arg1, arg2);
}

void s7extra_callFunc_void_int_float_float(func_t *func, int64_t arg1, float arg2, float arg3){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  3,
                  s7_make_integer(s7, arg1),
                  s7_make_real(s7, arg2),
                  s7_make_real(s7, arg3)
                  )
          );
}

void s7extra_callFunc2_void_int_float_float(const char *funcname, int64_t arg1, float arg2, float arg3){
  s7extra_callFunc_void_int_float_float((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3);
}

void s7extra_callFunc_void_int_int_float_float(func_t *func, int64_t arg1, int64_t arg2, float arg3, float arg4){
  ScopedEvalTracker eval_tracker;
  
  s7_call(s7,
          (s7_pointer)func,
          s7_list(s7,
                  4,
                  s7_make_integer(s7, arg1),
                  s7_make_integer(s7, arg2),
                  s7_make_real(s7, arg3),
                  s7_make_real(s7, arg4)
                  )
          );
}

void s7extra_callFunc2_void_int_int_float_float(const char *funcname, int64_t arg1, int64_t arg2, float arg3, float arg4){
  s7extra_callFunc_void_int_int_float_float((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4);
}

bool s7extra_callFunc_bool_int_int_float_float(func_t *func, int64_t arg1, int64_t arg2, float arg3, float arg4){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   4,
                                   s7_make_integer(s7, arg1),
                                   s7_make_integer(s7, arg2),
                                   s7_make_real(s7, arg3),
                                   s7_make_real(s7, arg4)
                                   )
                           );
  if(!s7_is_boolean(ret)){
    handleError("Callback did not return a boolean");
    return false;
  }else{
    return s7_boolean(s7, ret);
  }
}

bool s7extra_callFunc2_bool_int_int_float_float(const char *funcname, int64_t arg1, int64_t arg2, float arg3, float arg4){
  return s7extra_callFunc_bool_int_int_float_float((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4);
}

int64_t s7extra_callFunc_int_int(func_t *func, int64_t arg1){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   1,
                                   s7_make_integer(s7, arg1)
                                   )
                           );
  if(!s7_is_integer(ret)){
    handleError("Callback did not return an integer");
    return -1;
  }else{
    return s7_integer(ret);
  }
}

int64_t s7extra_callFunc2_int_int(const char *funcname, int64_t arg1){
  return s7extra_callFunc_int_int((func_t*)s7_name_to_value(s7, funcname), arg1);
}

int64_t s7extra_callFunc_int_int_int_int(func_t *func, int64_t arg1, int64_t arg2, int64_t arg3){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   3,
                                   s7_make_integer(s7, arg1),
                                   s7_make_integer(s7, arg2),
                                   s7_make_integer(s7, arg3)
                                   )
                           );
  if(!s7_is_integer(ret)){
    handleError("Callback did not return an integer");
    return -1;
  }else{
    return s7_integer(ret);
  }
}

int64_t s7extra_callFunc2_int_int_int_int(const char *funcname, int64_t arg1, int64_t arg2, int64_t arg3){
  return s7extra_callFunc_int_int_int_int((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3);
}

int64_t s7extra_callFunc_int_int_int_int_bool(func_t *func, int64_t arg1, int64_t arg2, int64_t arg3, bool arg4){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer ret = s7_call(s7,
                           (s7_pointer)func,
                           s7_list(s7,
                                   4,
                                   s7_make_integer(s7, arg1),
                                   s7_make_integer(s7, arg2),
                                   s7_make_integer(s7, arg3),
                                   s7_make_boolean(s7, arg4)
                                   )
                           );
  if(!s7_is_integer(ret)){
    handleError("Callback did not return an integer");
    return -1;
  }else{
    return s7_integer(ret);
  }
}

int64_t s7extra_callFunc2_int_int_int_int_bool(const char *funcname, int64_t arg1, int64_t arg2, int64_t arg3, bool arg4){
  return s7extra_callFunc_int_int_int_int_bool((func_t*)s7_name_to_value(s7, funcname), arg1, arg2, arg3, arg4);
}

void s7extra_protect(void *v){
  s7_gc_protect(s7, (s7_pointer)v);  
}

void s7extra_unprotect(void *v){
  s7_gc_unprotect(s7, (s7_pointer)v);  
}

int placetest(Place dasplacevar,int windownum){
  return dasplacevar.line;
}

Place placetest2(int a, int b, int c){
  Place p = {a,(uint_32)b,(uint_32)c};
  return p;
}


Place *PlaceScale(const Place *x, const Place *x1, const Place *x2, const Place *y1, const Place *y2) {
  ScopedEvalTracker eval_tracker;
  
  static s7_pointer scheme_func = find_and_protect_scheme_func("scale");

  s7_pointer result = s7_call(s7,
                              scheme_func,
                              s7_list(s7,
                                      5,
                                      place_to_ratio(x),
                                      place_to_ratio(x1),
                                      place_to_ratio(x2),
                                      place_to_ratio(y1),
                                      place_to_ratio(y2)
                                      )
                              );

  if (s7_is_ratio(result))
    return ratio_to_place(result);
  else if (s7_is_integer(result))
    return PlaceCreate((int)s7_integer(result), 0, 1);
  else if (s7_is_real(result)) {
    RError("PlaceScale: result was a real (strange): %f (%s %s %s %s %s)",s7_real(result),PlaceToString(x),PlaceToString(x1),PlaceToString(x2),PlaceToString(y1),PlaceToString(y2));
    Place *ret = (Place*)talloc_atomic(sizeof(Place));
    Double2Placement(s7_real(result), ret);
    return ret;
  } else {
    RError("result was not ratio or integer. Returning 0 (%s %s %s %s %s)",PlaceToString(x),PlaceToString(x1),PlaceToString(x2),PlaceToString(y1),PlaceToString(y2));
    return PlaceCreate(0,0,1);
  }
}


bool quantitize_note(const struct Blocks *block, struct Notes *note) {
  ScopedEvalTracker eval_tracker;
  
  s7_pointer scheme_func = s7_name_to_value(s7, "quantitize-note");
  
  Place last_place = p_Last_Pos(block);
  
  s7_pointer result = s7_call(s7,
                              scheme_func,
                              s7_list(s7,
                                      8,
                                      place_to_ratio(&note->l.p),
                                      place_to_ratio(&note->end),
                                      s7_make_ratio(s7, root->quantitize_options.quant.numerator, root->quantitize_options.quant.denominator),
                                      place_to_ratio(&last_place),
                                      s7_make_boolean(s7, root->quantitize_options.quantitize_start),
                                      s7_make_boolean(s7, root->quantitize_options.quantitize_end),
                                      s7_make_boolean(s7, root->quantitize_options.keep_note_length),
                                      s7_make_integer(s7, root->quantitize_options.type)
                                      )
                              );

  if (s7_is_boolean(result))
    return false;

  if (!s7_is_pair(result)) {
    RError("Unknown result after call to quantitize-note");
    return false;
  }

  s7_pointer new_start = s7_car(result);
  s7_pointer new_end = s7_cdr(result);

  note->l.p = number_to_place(new_start);
  note->end = number_to_place(new_end);

  return true;
}


static void place_operation_void_p1_p2(s7_pointer scheme_func, Place *p1,  const Place *p2){
  ScopedEvalTracker eval_tracker;
  
  R_ASSERT(p1->dividor > 0);
  R_ASSERT(p2->dividor > 0);
  
  s7_pointer result = s7_call(s7,
                              scheme_func,
                              s7_list(s7,
                                      2,
                                      place_to_ratio(p1),
                                      place_to_ratio(p2)
                                      )
                              );
  
  if (s7_is_ratio(result))
    PlaceCopy(p1, ratio_to_place(result));
  else if (s7_is_integer(result))
    PlaceCopy(p1, PlaceCreate((int)s7_integer(result), 0, 1));
  else if (s7_is_real(result)) {
    RError("result was a real (strange): %f (%s %s)",s7_real(result),PlaceToString(p1),PlaceToString(p2));
    Double2Placement(s7_real(result), p1);
  }else {
    RError("result was not ratio or integer. Returning 0 (%s %s)",PlaceToString(p1),PlaceToString(p2));
    PlaceCopy(p1, PlaceCreate(0,0,1));
  }
}
                            
void PlaceAdd(Place *p1,  const Place *p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("+");
  place_operation_void_p1_p2(scheme_func, p1,p2);
}

void PlaceSub(Place *p1,  const Place *p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("-");
  place_operation_void_p1_p2(scheme_func, p1,p2);
}

void PlaceMul(Place *p1,  const Place *p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("*");
  place_operation_void_p1_p2(scheme_func, p1,p2);
}

void PlaceDiv(Place *p1,  const Place *p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("/");
  place_operation_void_p1_p2(scheme_func, p1,p2);
}


static Place place_operation_place_p1_p2(s7_pointer scheme_func, const Place p1,  const Place p2){
  ScopedEvalTracker eval_tracker;
  
  s7_pointer result = s7_call(s7,
                              scheme_func,
                              s7_list(s7,
                                      2,
                                      place_to_ratio(&p1),
                                      place_to_ratio(&p2)
                                      )
                              );

  return number_to_place(result);
}

Place p_Add(const Place p1, const Place p2){
  //PrintPlace("p1: ",&p1);
  //PrintPlace("p2: ",&p2);
  static s7_pointer scheme_func = find_and_protect_scheme_func("+");
  return place_operation_place_p1_p2(scheme_func, p1,p2);
}

Place p_Sub(const Place p1, const Place p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("-");
  return place_operation_place_p1_p2(scheme_func, p1,p2);
}

Place p_Mul(const Place p1, const Place p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("*");
  return place_operation_place_p1_p2(scheme_func, p1,p2);
}

Place p_Div(const Place p1, const Place p2){
  static s7_pointer scheme_func = find_and_protect_scheme_func("/");
  return place_operation_place_p1_p2(scheme_func, p1,p2);
}

Place p_Quantitize(const Place p, const Place q){
  static s7_pointer scheme_func = find_and_protect_scheme_func("quantitize");
  return place_operation_place_p1_p2(scheme_func, p, q);
}

void SCHEME_throw(const char *symbol, const char *message){
  //printf("SCHEME_THROW %d\n", g_evals);
  //if(g_evals>0){
    s7_error(s7,
             s7_make_symbol(s7, symbol),
             s7_list(s7,
                     1,
                     s7_make_string(s7, message))
             );
    //}
}


const char *SCHEME_get_backtrace(void){
  ScopedEvalTracker eval_tracker;
  
  SCHEME_eval("(throw \'get-backtrace)"); // Fill in error-lines and so forth into s7.
  
  const char *ret = s7_string(
                              s7_call(s7,
                                      s7_name_to_value(s7, "ow!"),
                                      s7_list(s7, 0)
                                      )
                              );

  printf("Got: %s\n", ret);
  return ret;
}


bool SCHEME_mousepress(int button, float x, float y){
  ScopedEvalTracker eval_tracker;
  
  tevent.x  = x;
  tevent.y  = y;

  return s7_boolean(s7,
                    s7_call(s7, 
                            s7_name_to_value(s7, "radium-mouse-press"), // [1]
                            s7_list(s7,
                                    3,
                                    s7_make_integer(s7, button),
                                    s7_make_real(s7, x),
                                    s7_make_real(s7, y)
                                    )
                            )
                    );
  // [1] Not storing/reusing this value since 's7_name_to_value' is probably ligthing fast anyway, plus that it'll be possible to redefine radium-mouse-press from scheme this way.
}

bool SCHEME_mousemove(int button, float x, float y){
  ScopedEvalTracker eval_tracker;
  
  tevent.x  = x;
  tevent.y  = y;

  return s7_boolean(s7,
                    s7_call(s7, 
                            s7_name_to_value(s7, "radium-mouse-move"), // [1]
                            s7_list(s7,
                                    3,
                                    s7_make_integer(s7, button),
                                    s7_make_real(s7, x),
                                    s7_make_real(s7, y)
                                    )
                            )
                    );
  // [1] Not storing/reusing this value since 's7_name_to_value' is probably ligthing fast anyway, plus that it'll be possible to redefine radium-mouse-press from scheme this way.
}

bool SCHEME_mouserelease(int button, float x, float y){
  ScopedEvalTracker eval_tracker;
  
  tevent.x  = x;
  tevent.y  = y;

  return s7_boolean(s7,
                    s7_call(s7, 
                            s7_name_to_value(s7, "radium-mouse-release"), // [1]
                            s7_list(s7,
                                    3,
                                    s7_make_integer(s7, button),
                                    s7_make_real(s7, x),
                                    s7_make_real(s7, y)
                                    )
                            )
                    );
  // [1] Not storing/reusing this value since 's7_name_to_value' is probably ligthing fast anyway, plus that it'll be possible to redefine radium-mouse-press from scheme this way.
}

void SCHEME_eval(const char *code){
  ScopedEvalTracker eval_tracker;
           
  s7_eval_c_string(s7, code);
}

int SCHEME_get_webserver_port(void){
  return s7webserver_get_portnumber(s7webserver);
}

void SCHEME_init1(void){
  ScopedEvalTracker eval_tracker;
  
  s7 = s7_init();
  if (s7==NULL) {
    RError("Can't start s7 scheme");
    return;
  }
  
  std::string os_path = ""; //OS_get_program_path() + OS_get_directory_separator();
  //printf("%s\n",os_path);

  s7_add_to_load_path(s7,(os_path+"packages"+OS_get_directory_separator()+"s7").c_str()); // bin/packages/s7 . No solution to utf-8 here. s7_add_to_load_path takes char* only.
  s7_add_to_load_path(s7,(os_path+"scheme").c_str()); // bin/scheme

  init_radium_s7(s7);

  s7_load(s7,"init.scm");

  s7webserver = s7webserver_create(s7, 5080, true);
  s7webserver_set_verbose(s7webserver, true);
}

void SCHEME_init2(void){
  SCHEME_eval("(init-step-2)");
}
