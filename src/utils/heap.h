#ifndef PENNOS_HEAP_H
#define PENNOS_HEAP_H

#include "Vec.h"

/**
 * A priority queue implemmented using the vector as a binary heap.
 * The heap is a complete binary tree, and the elements are stored in
 * a vector. The relative order is determined by the comparison function.
 */
typedef struct heap_st {
  Vec vec;                   // A vector used as a container for the heap
  int (*cmp)(ptr_t, ptr_t);  // A pointer to the comparator function
} Heap;

/**
 * @brief Init a new heap by calling the vector constructor and setting the
 * comparator function.
 *
 * @param initial_cap The initial capacity of the heap.
 * @param cmp A pointer to the comparison function.
 * @return A new heap.
 */
Heap heap_new(size_t initial_cap, int (*cmp)(ptr_t, ptr_t));

/**
 * @brief Swap two elements in the heap.
 *
 * @param heap A pointer to the heap.
 * @param index1 The index of the first element.
 * @param index2 The index of the second element.
 */
void heap_swap(Heap* heap, size_t index1, size_t index2);

/**
 * @brief Sift up the element at the given index in the heap.
 *
 * This function moves the element at the given index up the heap until
 * it is in the correct position according to the comparison function.
 *
 * @param heap A pointer to the heap.
 * @param index The index of the element to sift up.
 * @return void
 */
void heap_sift_up(Heap* heap, size_t index);

/**
 * @brief Sift down the element at the given index in the heap.
 *
 * This function moves the element at the given index down the heap until
 * it is in the correct position according to the comparison function.
 *
 * @param heap A pointer to the heap.
 * @param index The index of the element to sift down.
 * @return void
 */
void heap_sift_down(Heap* heap, size_t index);

/**
 * @brief Push an element into the heap.
 *
 * This function adds an element to the heap and maintains the heap property.
 *
 * @param heap A pointer to the heap.
 * @param element The element to be added to the heap.
 */
void heap_push(Heap* heap, ptr_t element);

/**
 * @brief Pop the top element from the heap.
 *
 * This function removes and returns the top element from the heap and
 * maintains the heap property.
 *
 * @param heap A pointer to the heap.
 * @return The top element of the heap.
 */
ptr_t heap_pop(Heap* heap);

/**
 * @brief Peek at the top element of the heap without removing it.
 *
 * This function returns the top element of the heap without modifying
 * the heap.
 *
 * @param heap A pointer to the heap.
 * @return The top element of the heap.
 */
ptr_t heap_peek(Heap* heap);

/**
 * @brief Destroy the heap and free its resources.
 *
 * This function frees the memory used by the heap and its elements.
 *
 * @param heap A pointer to the heap.
 */
void heap_destroy(Heap* heap);

/**
 * @brief Check if the heap is empty.
 *
 * This function returns true if the heap is empty, false otherwise.
 *
 * @param heap A pointer to the heap.
 * @return true if the heap is empty, false otherwise.
 */
bool heap_is_empty(Heap* heap);

/**
 * @brief Get the size of the heap.
 *
 * This function returns the number of elements in the heap.
 *
 * @param heap A pointer to the heap.
 * @return The size of the heap.
 */
size_t heap_size(Heap* heap);

#endif  // PENNOS_HEAP_H