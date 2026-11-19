#include "heap.h"

Heap heap_new(size_t initial_capacity, int (*cmp)(ptr_t, ptr_t)) {
  Heap heap;
  heap.vec = vec_new(initial_capacity, NULL);
  heap.cmp = cmp;
  return heap;
}

void heap_swap(Heap* heap, size_t index1, size_t index2) {
  ptr_t temp = vec_get(&heap->vec, index1);
  vec_set(&heap->vec, index1, vec_get(&heap->vec, index2));
  vec_set(&heap->vec, index2, temp);
}

void heap_sift_up(Heap* heap, size_t index) {
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (heap->cmp(vec_get(&heap->vec, index), vec_get(&heap->vec, parent)) >=
        0) {
      break;
    }
    heap_swap(heap, index, parent);
    index = parent;
  }
}

void heap_sift_down(Heap* heap, size_t index) {
  size_t length = vec_len(&heap->vec);
  while (2 * index + 1 < length) {
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;
    size_t smallest = left;

    if (right < length &&
        heap->cmp(vec_get(&heap->vec, right), vec_get(&heap->vec, left)) < 0) {
      smallest = right;
    }

    if (heap->cmp(vec_get(&heap->vec, index), vec_get(&heap->vec, smallest)) <=
        0) {
      break;
    }

    heap_swap(heap, index, smallest);
    index = smallest;
  }
}

void heap_push(Heap* heap, ptr_t element) {
  vec_push_back(&heap->vec, element);
  heap_sift_up(heap, vec_len(&heap->vec) - 1);
}

ptr_t heap_pop(Heap* heap) {
  if (vec_is_empty(&heap->vec)) {
    return NULL;
  }

  ptr_t min_element = vec_get(&heap->vec, 0);
  ptr_t last_element = vec_get(&heap->vec, vec_len(&heap->vec) - 1);
  vec_set(&heap->vec, 0, last_element);
  vec_pop_back(&heap->vec);
  heap_sift_down(heap, 0);
  return min_element;
}

ptr_t heap_peek(Heap* heap) {
  if (vec_is_empty(&heap->vec)) {
    return NULL;
  }
  return vec_get(&heap->vec, 0);
}

void heap_destroy(Heap* heap) {
  vec_destroy(&heap->vec);
}

bool heap_is_empty(Heap* heap) {
  return vec_is_empty(&heap->vec);
}

size_t heap_size(Heap* heap) {
  return vec_len(&heap->vec);
}