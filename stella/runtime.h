#ifndef STELLA_RUNTIME_H
#define STELLA_RUNTIME_H

#include <stdio.h>
#include "gc.h"

/** A Stella object with statically unknown number of fields.
 */
typedef struct {
  int    object_header;     /**< Header of the object contains
                              * its TAG (see STELLA_OBJECT_HEADER_TAG) and
                              * the number of fields (see STELLA_OBJECT_HEADER_FIELD_COUNT). */
  void*  object_fields[0];  /**< An array of object fields (0 fields for static objects). */
} stella_object;

/** Read a field from a Stella object. Subject to a read barrier. */
#define STELLA_OBJECT_READ_FIELD(obj, i) GC_READ_BARRIER(obj, i, ((stella_object*)(obj->object_fields[i])))
/** (Over)write a field from a Stella object. Subject to a write barrier.
 * See STELLA_OBJECT_INIT_FIELD for initialization of fields (which does not trigger the write barrier).
 */
#define STELLA_OBJECT_WRITE_FIELD(obj, i, x) GC_WRITE_BARRIER(obj, i, x, (obj->object_fields[i] = (void*)x))

/** Extract the TAG from Stella object's header. */
#define STELLA_OBJECT_HEADER_TAG(header) (header & TAG_MASK)
/** Extract the fields count from Stella object's header. */
#define STELLA_OBJECT_HEADER_FIELD_COUNT(header) ((header & FIELD_COUNT_MASK) >> 4)

/** Extract the n from succ(n). */
#define STELLA_OBJECT_SUCC_ARG(obj) STELLA_OBJECT_READ_FIELD(obj,0)

/** Initialize new Stella object's TAG. */
#define STELLA_OBJECT_INIT_TAG(obj, tag) (obj->object_header = ((obj->object_header & ~((1 << 4) - 1)) | tag))
/** Initialize new Stella object's fields count. */
#define STELLA_OBJECT_INIT_FIELDS_COUNT(obj, count) (obj->object_header = ((obj->object_header & ~(((1 << 4) - 1) << 4)) | count << 4))
/** Initialize new Stella object's field. */
#define STELLA_OBJECT_INIT_FIELD(obj, i, x) (obj->object_fields[i] = (void*)x)

/** Call a Stella function (closure) with a given Stella object as an argument. */
#define STELLA_OBJECT_CLOSURE_CALL(f, x) (*(stella_object *(*)(stella_object *, stella_object *))STELLA_OBJECT_READ_FIELD(f, 0))(f, x)

/** A Stella object dedicated for static objects with one field,
 * which is how closures for top-level definitions are represented by default.
 * This is only supposed to be used for static Stella objects, corresponding
 * to the top-level function definitions (the only field being the address of the function).
 */
typedef struct {
  int    object_header;     /**< Header of the object. Same as in stella_object. */
  void*  object_fields[1];  /**< An array of object fields (1 field for static objects). */
} stella_object_1;

/** An enumeration of possible Stella object tags. */
enum TAG {
  TAG_ZERO,   /**< 0       : Nat */
  TAG_SUCC,   /**< succ(_) : Nat */
  TAG_FALSE,  /**< false   : Bool */
  TAG_TRUE,   /**< true    : Bool */
  TAG_FN,     /**< fn(...){ return ... } */
  TAG_REF,    /**< new(_)  : &T */
  TAG_UNIT,   /**< unit    : Unit */
  TAG_TUPLE,  /**< {_, _, ..., _} */
  TAG_INL,    /**< inl(...) */
  TAG_INR,    /**< inr(...) */
  TAG_EMPTY,  /**< [] */
  TAG_CONS    /**< cons(..., ...) */
  } ;

/** Allocate a new Stella object with a given TAG and number of fields.
 * Note that this function makes use of gc_alloc.
 */
stella_object* alloc_stella_object(enum TAG tag, int fields_count);

/** Convert a natural number (non-negative integer) into a corresponding Stella object. */
stella_object *nat_to_stella_object(int n);
/** Convert a natural number represented as a Stella object to an integer. */
int stella_object_to_nat(stella_object* obj);
/** Pretty-print a Stella object. */
void print_stella_object(stella_object* obj);
/** Print some Stella runtime statistics. */
void print_stella_stats();

/** Builtin implementation for Stella's Nat::rec. */
stella_object* stella_object_nat_rec(stella_object* n, stella_object* z, stella_object* f);

/** The static Stella object for zero. */
extern stella_object the_ZERO;

/** The static Stella object for unit. */
extern stella_object the_UNIT;

/** The static Stella object for the empty list. */
extern stella_object the_EMPTY;

/** The static Stella object for empty tuple (zero-length tuple). */
extern stella_object the_EMPTY_TUPLE;

/** The static Stella object for false. */
extern stella_object the_FALSE;

/** The static Stella object for true. */
extern stella_object the_TRUE;

/** The bitmask for the fields count. */
extern const int FIELD_COUNT_MASK;

/** The bitmask for the Stella object tag. */
extern const int TAG_MASK;

#endif
