#ifndef PROC_QUEUE_H
#define PROC_QUEUE_H

#include "../kernel/k_proc.h"

/**
 * @brief A structure defined for each node in the process queue.
 */
typedef struct proc_node_st {
  /**
   * @brief A pointer to the process control block of the process
   */
  pcb_t* proc;

  /**
   * @brief A pointer to the next node in the queue
   */
  struct proc_node_st* next;

  /**
   * @brief A pointer to the previous node in the queue
   */
  struct proc_node_st* prev;
} proc_node_t;

/**
 * @brief A structure defined for the process queue, which is a doubly linked
 * list.
 */
typedef struct proc_queue_st {
  /**
   * @brief A pointer to the head of the queue
   */
  proc_node_t* head;

  /**
   * @brief A pointer to the tail of the queue
   */
  proc_node_t* tail;

  /**
   * @brief The size of the queue
   */
  size_t size;
} proc_queue_t;

/**
 * @brief Create a new process queue.
 *
 * This function initializes a new process queue by allocating memory for it,
 * the head and tail are set to be NULL by default.
 */
proc_queue_t* create_proc_queue();

/**
 * @brief A function to create a new process node from a process control block.
 *
 * @param proc A pointer to the process control block.
 * @return A pointer to the newly created process node.
 */
proc_node_t* create_node_from_proc(pcb_t* proc);

/**
 * @brief Push a process node to the front of the queue.
 *
 * @param queue A pointer to the process queue.
 * @param node A pointer to the process node to be pushed.
 * @return true if the operation is successful, false otherwise.
 */
bool push_front(proc_queue_t* queue, proc_node_t* node);

/**
 * @brief Push a process node to the back of the queue.
 *
 * @param queue A pointer to the process queue.
 * @param node A pointer to the process node to be pushed.
 * @return true if the operation is successful, false otherwise.
 */
bool push_back(proc_queue_t* queue, proc_node_t* node);

/**
 * @brief Get the first node in the queue.
 *
 * @param queue A pointer to the process queue.
 * @return A pointer to the first node in the queue.
 */
proc_node_t* front(proc_queue_t* queue);

/**
 * @brief Get the last node in the queue.
 *
 * @param queue A pointer to the process queue.
 * @return A pointer to the last node in the queue.
 */
proc_node_t* back(proc_queue_t* queue);

/**
 * @brief Get and pop the first node in the queue.
 *
 * @param queue A pointer to the process queue.
 * @return A pointer to the first node in the queue, or NULL if the queue is
 * empty.
 */
proc_node_t* pop_front(proc_queue_t* queue);

/**
 * @brief Get and pop the last node in the queue.
 *
 * @param queue A pointer to the process queue.
 * @return A pointer to the last node in the queue, or NULL if the queue is
 * empty.
 */
proc_node_t* pop_back(proc_queue_t* queue);

/**
 * @brief Find a node in the queue by its process ID.
 *
 * @param queue A pointer to the process queue.
 * @param pid The process ID to search for.
 * @return A pointer to the node with the specified process ID, or NULL if not
 * found.
 */
proc_node_t* find_node_by_pid(proc_queue_t* queue, pid_t pid);

/**
 * @brief Remove a node from the queue by its process ID.
 *
 * @param queue A pointer to the process queue.
 * @param pid The process ID of the node to be removed.
 * @return A pointer to the removed node, or NULL if not found.
 */
proc_node_t* remove_node_by_pid(proc_queue_t* queue, pid_t pid);

/**
 * @brief Free the memory allocated for the process queue.
 *
 * @param queue A pointer to the process queue to be freed.
 */
void free_proc_queue(proc_queue_t* queue);

/**
 * @brief Free the memory allocated for a process node. Notice that the pcb in
 * this node won't be freed.
 *
 * @param node A pointer to the process node to be freed.
 */
void free_proc_node(proc_node_t* node);

#endif  // PROC_QUEUE_H