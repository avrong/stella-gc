#include <stdlib.h>
#include <stdio.h>

#include "runtime.h"

int total_allocated_fields = 0;

stella_object the_ZERO = { .object_header = TAG_ZERO, .object_fields = {} } ;
stella_object the_UNIT = { .object_header = TAG_UNIT, .object_fields = {} } ;
stella_object the_EMPTY = { .object_header = TAG_EMPTY, .object_fields = {} } ;
stella_object the_EMPTY_TUPLE = { .object_header = TAG_TUPLE, .object_fields = {} } ;
stella_object the_FALSE = { .object_header = TAG_FALSE, .object_fields = {} } ;
stella_object the_TRUE = { .object_header = TAG_TRUE, .object_fields = {} } ;
const int FIELD_COUNT_MASK = (1 << 8) - (1 << 4) ;
const int TAG_MASK         = (1 << 4) - (1 << 0) ;

stella_object* alloc_stella_object(enum TAG tag, int fields_count) {
  stella_object *obj;
  total_allocated_fields += fields_count;
  switch (tag) {
    // do not allocate constant objects
    case TAG_ZERO: return &the_ZERO;
    case TAG_FALSE: return &the_FALSE;
    case TAG_TRUE: return &the_TRUE;
    case TAG_UNIT: return &the_UNIT;
    case TAG_EMPTY: return &the_EMPTY;
    case TAG_TUPLE: if (fields_count == 0) { return &the_EMPTY_TUPLE; }
    // allocate an object with at least one field (or an unknown tag)
    default:
      obj = gc_alloc(sizeof(stella_object) + fields_count * sizeof(void*));
      STELLA_OBJECT_INIT_TAG(obj, tag);
      STELLA_OBJECT_INIT_FIELDS_COUNT(obj, fields_count);
      return obj;
  }
}

stella_object *nat_to_stella_object(int n) {
  stella_object *result, *x;
  gc_push_root((void*)&result);    // it is sufficient to push only result
  result = &the_ZERO;
  for (int i = n; i > 0; i--) {
    x = alloc_stella_object(TAG_SUCC, 1);
    STELLA_OBJECT_INIT_FIELD(x, 0, result);
    result = x;
  }
  gc_pop_root((void*)&result);
  return result;
}

int stella_object_to_nat(stella_object* obj) {
  int result = 0;
  while (STELLA_OBJECT_HEADER_TAG(obj->object_header) == TAG_SUCC) {
    obj = STELLA_OBJECT_SUCC_ARG(obj);
    result += 1;
  }
  return result;
}

stella_object* stella_object_nat_rec(stella_object* n, stella_object* z, stella_object* f) {
  stella_object *g;
#ifdef STELLA_DEBUG
  printf("[debug] call Nat::rec(");
  printf("n = "); print_stella_object(n); printf(", ");
  printf("z = "); print_stella_object(z); printf(", ");
  printf("f = "); print_stella_object(f);
  printf(")\n");
#endif
  gc_push_root((void**)&n);
  gc_push_root((void**)&z);
  gc_push_root((void**)&f);
  while (STELLA_OBJECT_HEADER_TAG(n->object_header) == TAG_SUCC) {
    n = STELLA_OBJECT_SUCC_ARG(n);
    g = STELLA_OBJECT_CLOSURE_CALL(f, n);
    z = STELLA_OBJECT_CLOSURE_CALL(g, z);
  }
  gc_pop_root((void**)&f);
  gc_pop_root((void**)&z);
  gc_pop_root((void**)&n);
  return z;
}

void print_stella_object(stella_object* obj) {
  // printf("[%d]", STELLA_OBJECT_HEADER_TAG(obj->object_header));
  int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
  switch (STELLA_OBJECT_HEADER_TAG(obj->object_header)) {
    case TAG_ZERO:
      printf("0");
      return;
    case TAG_SUCC:
      printf("%d", stella_object_to_nat(obj));
      return;
    case TAG_FALSE:
      printf("false");
      return;
    case TAG_TRUE:
      printf("true");
      return;
    case TAG_FN:
      printf("fn<%p>", STELLA_OBJECT_READ_FIELD(obj, 0));
      return;
    case TAG_REF:
      printf("ref<%p>", STELLA_OBJECT_READ_FIELD(obj, 0));
      return;
    case TAG_UNIT:
      printf("unit");
      return;
    case TAG_INL:
      printf("inl(");
      print_stella_object(STELLA_OBJECT_READ_FIELD(obj, 0));
      printf(")");
      return;
    case TAG_INR:
      printf("inr(");
      print_stella_object(STELLA_OBJECT_READ_FIELD(obj, 0));
      printf(")");
      return;
    case TAG_EMPTY:
      printf("[]");
      return;
    case TAG_CONS:
      printf("[");
      print_stella_object(STELLA_OBJECT_READ_FIELD(obj, 0));
      obj = STELLA_OBJECT_READ_FIELD(obj, 1);
      while (STELLA_OBJECT_HEADER_TAG(obj->object_header) == TAG_CONS) {
        printf(", ");
        print_stella_object(STELLA_OBJECT_READ_FIELD(obj, 0));
        obj = STELLA_OBJECT_READ_FIELD(obj, 1);
      }
      printf("]");
      return;
    case TAG_TUPLE:
      printf("{");
      for (int i = 0; i < fields_count; i++) {
        print_stella_object(obj->object_fields[i]);
        if (i < fields_count - 1) { printf(", "); }
      }
      printf("}");  // TODO: pretty print a tuple
      return;
  }
}

void print_stella_stats() {
  #ifdef STELLA_GC_STATS
  printf("\n------------------------------------------------------------\n");
  printf("Garbage collector (GC) statistics:\n");
  print_gc_alloc_stats();
  #endif
  #ifdef STELLA_RUNTIME_STATS
  printf("\n------------------------------------------------------------\n");
  printf("Stella runtime statistics:\n");
  printf("Total allocated fields in Stella objects: %'d fields\n", total_allocated_fields);
  #endif
}
