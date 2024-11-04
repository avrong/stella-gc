#include <stdlib.h>
#include <stdio.h>

#include "runtime.h"
#include "gc.h"

// Total allocated number of bytes (over the entire duration of the program)
int total_allocated_bytes = 0;

// Total allocated number of objects (over the entire duration of the program)
int total_allocated_objects = 0;

int max_allocated_bytes = 0;
int max_allocated_objects = 0;

int total_reads = 0;
int total_writes = 0;

#define MAX_GC_ROOTS 1024
#define MAX_ALLOC_SIZE 4096
#define G1_ALLOC_SIZE (MAX_ALLOC_SIZE * 2)
#define MAX_CHANGED_NODES 4096

int gc_roots_max_size = 0;
int gc_roots_top = 0;
void **gc_roots[MAX_GC_ROOTS];

int changed_nodes_pointer = 0;
void *changed_nodes[MAX_CHANGED_NODES];

GC gc;

typedef struct {
 void* forwarded;
 stella_object object;
} ObjectWrapper;

ObjectWrapper* gc_alloc_in_heap(Heap* heap, size_t size);
void* gc_try_alloc(const Generation* gen, size_t size);

int gc_chase(const Generation* gen, ObjectWrapper* wp_ptr);
void* gc_forward(Generation* gen, void* ptr);
void gc_collect(Generation* gen);

void gc_gen_init(Generation* gen, size_t alloc_size, size_t gen_number, void* to_space) {
  size_t heap_size = sizeof(Heap);

  gen->cycles = 0;
  gen->scan = 0;

  if (to_space == NULL) {
    gen->to_space = malloc(heap_size);

    gen->to_space->generation_number = gen_number;
    gen->to_space->size = alloc_size;
    gen->to_space->start = malloc(alloc_size);
    gen->to_space->next = gen->to_space->start;

  } else {
    gen->to_space = to_space;
  }

  gen->from_space = malloc(heap_size);

  gen->from_space->generation_number = gen_number;
  gen->from_space->size = alloc_size;
  gen->from_space->start = malloc(alloc_size);
  gen->from_space->next = gen->from_space->start;
}

void gc_init(GC* gc) {
  size_t generation_size = sizeof(Generation);

  gc->g0 = malloc(generation_size);
  gc->g1 = malloc(generation_size);

  gc_gen_init(gc->g1, G1_ALLOC_SIZE, 1, NULL);
  gc_gen_init(gc->g0, MAX_ALLOC_SIZE, 0, gc->g1->from_space);

  gc->g0->to_space = gc->g1->from_space;
}

void* gc_alloc(size_t size_in_bytes) {
  if (gc.g0 == NULL) {
    gc_init(&gc);
  }

  size_t wrapper_size = size_in_bytes + sizeof(void*);

  void* result = gc_try_alloc(gc.g0, wrapper_size);
  if (result == NULL) {
    gc_collect(gc.g0);

    printf("After GC:\n");
    print_gc_state();
    print_gc_alloc_stats();

    result = gc_try_alloc(gc.g0, wrapper_size);
  }

  if (result != NULL) {
    total_allocated_bytes += size_in_bytes;
    total_allocated_objects += 1;

    max_allocated_bytes = total_allocated_bytes;
    max_allocated_objects = total_allocated_objects;
  } else {
    printf("Out of memory\n");
    exit(137);
  }

  // print_gc_state();
  // print_gc_alloc_stats();

  return result;
}

void print_gc_generation(const Generation* generation) {
  printf("Generation G%zu, %zu cycles\n", generation->from_space->generation_number, generation->cycles);

  printf("From space. ");
  print_heap(generation->from_space);
  printf("\n");

  printf("To space. ");
  print_heap(generation->to_space);
  printf("\n");

  printf("Allocated: %ld/%ld\n", (generation->from_space->next - generation->from_space->start), generation->from_space->size);
  printf("Free: %ld/%ld\n", generation->from_space->size - (generation->from_space->next - generation->from_space->start), generation->from_space->size);}

void print_gc_roots() {
  printf("ROOTS: ");
  for (int i = 0; i < gc_roots_top; i++) {
    printf("%p ", gc_roots[i]);
  }
  printf("\n");
}

void print_gc_alloc_stats() {
  printf("Total memory allocation: %'d bytes (%'d objects)\n", total_allocated_bytes, total_allocated_objects);
  printf("Maximum residency:       %'d bytes (%'d objects)\n", max_allocated_bytes, max_allocated_objects);
  printf("Total memory use:        %'d reads and %'d writes\n", total_reads, total_writes);
  printf("Max GC roots stack size: %'d roots\n", gc_roots_max_size);
}

size_t gc_wrapper_size(const ObjectWrapper *object) {
  const int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(object->object.object_header);
  return sizeof(ObjectWrapper) + fields_count * sizeof(void*);
}

// Print heap memory
void print_heap(const Heap* heap) {
  void* heap_end = heap->start + heap->size;
  printf("Heap: %zu bytes, %p..%p.\n", heap->size, heap->start, heap_end);

  for (void* start = heap->start; start < heap->next; start += gc_wrapper_size(start)) {
    ObjectWrapper *object = start;
    printf("%p :", object);

    // Print fields
    size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(object->object.object_header);
    for (size_t i = 0; i < field_count; i++) {
      void* field_ptr = object->object.object_fields[i];
      printf(" %p", field_ptr);
    }

    printf("\n");
  }
}

void print_gc_state() {
  print_gc_generation(gc.g0);
  print_gc_generation(gc.g1);
  print_gc_roots();
  printf("\n");
}

void gc_read_barrier(void *object, int field_index) {
  total_reads += 1;
}

void gc_write_barrier(void *object, int field_index, void *contents) {
  total_writes += 1;

  changed_nodes[changed_nodes_pointer] = object;
  changed_nodes_pointer++;
}

void gc_push_root(void **ptr){
  gc_roots[gc_roots_top++] = ptr;
  if (gc_roots_top > gc_roots_max_size) { gc_roots_max_size = gc_roots_top; }
}

void gc_pop_root(void **ptr){
  gc_roots_top--;
}

ObjectWrapper* gc_alloc_in_heap(Heap* heap, size_t size) {
  void* heap_end = heap->start + heap->size;

  if (heap->next + size <= heap_end) {
    ObjectWrapper *result = heap->next;
    result->forwarded = NULL;
    result->object.object_header = 0;
    heap->next += size;
    return result;
  }

  return NULL;
}

void* gc_try_alloc(const Generation* gen, size_t size) {
  void* allocated = gc_alloc_in_heap(gen->from_space, size);

  if (allocated == NULL) {
    return NULL;
  }

  return allocated + sizeof(void*);
}

int gc_ptr_in_heap(const Heap* heap, const void* ptr) {
  void* heap_end = heap->start + heap->size;
  return ptr >= heap->start && ptr < heap_end;
}

// Copying garbage collector

int gc_chase(const Generation* gen, ObjectWrapper* wp_ptr) {
  printf("GC chase\n");

  do {
    ObjectWrapper* q = gc_alloc_in_heap(gen->to_space, gc_wrapper_size(wp_ptr));
    if (q == NULL) return 0;

    void* r = NULL;
    q->forwarded = NULL;
    q->object.object_header = wp_ptr->object.object_header;

    // Object copy
    size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(wp_ptr->object.object_header);
    for (size_t fi = 0; fi < field_count; fi++) {
      q->object.object_fields[fi] = wp_ptr->object.object_fields[fi];

      if (gc_ptr_in_heap(gen->from_space, q->object.object_fields[fi])) {
        ObjectWrapper* forwarded_ptr = q->object.object_fields[fi] - sizeof(void*);

        if (!gc_ptr_in_heap(gen->to_space, forwarded_ptr->forwarded)) {
          r = forwarded_ptr;
        }
      }
    }

    wp_ptr->forwarded = q;
    wp_ptr = r;
  } while (wp_ptr != NULL);

  return 1;
}

void* gc_forward(Generation* gen, void* ptr) {
  printf("GC forward\n");
  
  if (!gc_ptr_in_heap(gen->from_space, ptr)) return ptr;

  ObjectWrapper* object = ptr - sizeof(void*);

  if (gc_ptr_in_heap(gen->to_space, object->forwarded)) {
    return &(((ObjectWrapper*) object->forwarded)->object);
  }

  int chased = gc_chase(gen, object);
  if (!chased) {
    if (gen->to_space->generation_number == gen->from_space->generation_number) {
      printf("Out of memory");
      exit(137);
    }
  }

  return &(((ObjectWrapper*) object->forwarded)->object);
}

void gc_collect(Generation* gen) {
  gen->cycles += 1;

  printf("GC collect\n");
  print_gc_state();  

  gen->scan = gen->to_space->next;

  // Forward root objects
  for (size_t i = 0; i < gc_roots_top; i++) {
    void** root_ptr = gc_roots[i];
    *root_ptr = gc_forward(gen, *root_ptr);
  }

  // Do G1
  if (gen->from_space->generation_number == 1) {
    for (void* ptr = gc.g0->from_space->start; ptr < gc.g0->from_space->next; ptr += gc_wrapper_size(ptr)) {
      ObjectWrapper* object = ptr;
      size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(object->object.object_header);

      for (size_t fi = 0; fi < field_count; fi++) {
        object->object.object_fields[fi] = gc_forward(gen, object->object.object_fields[fi]);
      }
    }
  }

  for (size_t i = 0; i < changed_nodes_pointer; i++) {
    stella_object* object = changed_nodes[i];
    size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(object->object_header);

    for (size_t fi = 0; fi < field_count; fi++) {
      object->object_fields[fi] = gc_forward(gen, object->object_fields[fi]);
    }

    changed_nodes_pointer = 0;
  }

  while (gen->scan < gen->to_space->next) {
    ObjectWrapper* object = gen->scan;
    size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(object->object.object_header);

    for (size_t fi = 0; fi < field_count; fi++) {
      object->object.object_fields[fi] = gc_forward(gen, object->object.object_fields[fi]);
    }

    gen->scan += gc_wrapper_size(object);
  }

  if (gen->from_space->generation_number == gen->to_space->generation_number) {
    void* buffer = gen->from_space;
    gen->from_space = gen->to_space;
    gen->to_space = buffer;

    gen->to_space->next = gen->to_space->start;

    gc.g0->to_space = gen->from_space;
    gc.g0->scan = gen->from_space->start;
  } else {
    Generation* current = gen->from_space->generation_number == 0 ? gc.g0 : gc.g1;
    Generation* next = gen->to_space->generation_number == 0 ? gc.g0 : gc.g1;

    current->from_space->next = gen->from_space->start;
    current->to_space = next->from_space;
  }
}

