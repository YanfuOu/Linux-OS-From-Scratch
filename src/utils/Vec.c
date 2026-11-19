#include "./Vec.h"
#include <stdlib.h>
#include "Vec.h"
#include "panic.h"

Vec vec_new(size_t initial_capacity, ptr_dtor_fn ele_dtor_fn) {
  Vec vec;
  vec.data = (ptr_t*)malloc(initial_capacity * sizeof(ptr_t));
  if (vec.data == NULL) {
    panic("Initialization Failed\n");
  }
  vec.capacity = initial_capacity;
  vec.length = 0;
  vec.ele_dtor_fn = ele_dtor_fn;
  return vec;
}

ptr_t vec_get(Vec* self, size_t index) {
  if (index < 0 || self == NULL || index >= self->length) {
    vec_destroy(self);
    panic("Index out of bounds\n");
  }
  return self->data[index];
}

void vec_set(Vec* self, size_t index, ptr_t new_ele) {
  if (index < 0 || self == NULL || index >= self->length) {
    vec_destroy(self);
    panic("Index out of bounds\n");
  }
  if (self->ele_dtor_fn != NULL) {
    self->ele_dtor_fn(self->data[index]);
  }
  self->data[index] = new_ele;
}

bool vec_expand(Vec* self, size_t new_capacity) {
  ptr_t* newData = (ptr_t*)realloc(self->data, sizeof(ptr_t) * new_capacity);
  if (newData == NULL) {
    return false;
  }
  self->data = newData;
  self->capacity = new_capacity;
  return true;
}

void vec_push_back(Vec* self, ptr_t new_ele) {
  if (self == NULL) {
    panic("Vec is not initialized\n");
  }

  // Expand the vector when it is full
  if (self->length == self->capacity) {
    if (!vec_expand(self, self->capacity == 0 ? 1 : self->capacity * 2)) {
      vec_destroy(self);
      panic("Memory Allocation Failed\n");
    }
  }
  self->data[self->length] = new_ele;
  self->length++;
}

bool vec_pop_back(Vec* self) {
  if (self == NULL || self->length == 0) {
    return false;
  }

  // When there is a destructor function, call it on the last element
  if (self->ele_dtor_fn != NULL) {
    self->ele_dtor_fn(self->data[self->length - 1]);
  }
  self->length--;
  return true;
}

void vec_insert(Vec* self, size_t index, ptr_t new_ele) {
  if (self == NULL) {
    panic("Vec is not initialized\n");
  }

  if (index > self->length) {
    vec_destroy(self);
    panic("Index out of bounds\n");
  }

  // Expand the vector when it is full
  if (self->length == self->capacity) {
    if (!vec_expand(self, self->capacity == 0 ? 1 : self->capacity * 2)) {
      vec_destroy(self);
      panic("Memory Allocation Failed\n");
    }
  }

  // Shifting elements, ranging from the end to the index, to its right by one
  for (size_t i = self->length; i > index; i--) {
    self->data[i] = self->data[i - 1];
  }

  self->data[index] = new_ele;
  self->length++;
}

void vec_erase(Vec* self, size_t index) {
  if (self == NULL) {
    panic("Vec is not initialized\n");
  }
  if (index >= self->length) {
    vec_destroy(self);
    panic("Index out of bounds\n");
  }

  if (self->ele_dtor_fn != NULL) {
    self->ele_dtor_fn(self->data[index]);
  }

  // Shifting elements, ranging from the index to the end, to its left by one
  for (size_t i = index; i < self->length - 1; i++) {
    self->data[i] = self->data[i + 1];
  }
  self->length--;
}

void vec_resize(Vec* self, size_t new_capacity) {
  if (self == NULL) {
    panic("Vec is not initialized\n");
  }

  if (new_capacity <= self->length) {
    return;
  }

  if (!vec_expand(self, new_capacity)) {
    vec_destroy(self);
    panic("Reallocation Failed\n");
  }
}

void vec_clear(Vec* self) {
  if (self == NULL) {
    panic("Vec is not initialized\n");
  }

  if (self->ele_dtor_fn != NULL) {
    for (size_t index = 0; index < self->length; index++) {
      self->ele_dtor_fn(self->data[index]);
    }
  }

  self->length = 0;
}

void vec_destroy(Vec* self) {
  if (self == NULL) {
    return;
  }

  vec_clear(self);
  free(self->data);
  self->data = NULL;
  self->capacity = 0;
}

ptr_t vec_back(Vec* self){
  if(self == NULL || vec_len(self) == 0){
    return NULL;
  }

  return vec_get(self, vec_len(self)-1);
}