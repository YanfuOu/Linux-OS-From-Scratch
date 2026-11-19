#include "proc_queue.h"

proc_node_t* create_node_from_proc(pcb_t* proc) {
  proc_node_t* node = (proc_node_t*)malloc(sizeof(proc_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->proc = proc;
  node->next = NULL;
  node->prev = NULL;
  return node;
}

void free_proc_node(proc_node_t* node) {
  if (node == NULL) {
    return;
  }
  node->prev = NULL;
  node->next = NULL;
  free(node);
  node = NULL;
}

proc_queue_t* create_proc_queue() {
  proc_queue_t* queue = (proc_queue_t*)malloc(sizeof(proc_queue_t));
  if (queue == NULL) {
    return NULL;
  }
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
  return queue;
}

bool push_front(proc_queue_t* queue, proc_node_t* node) {
  if (queue == NULL || node == NULL) {
    return false;
  }

  if (queue->size == 0) {
    queue->head = node;
    queue->tail = node;
  } else {
    queue->head->prev = node;
    node->next = queue->head;
    queue->head = node;
  }

  queue->size++;
  return true;
}

bool push_back(proc_queue_t* queue, proc_node_t* node) {
  if (queue == NULL || node == NULL) {
    return false;
  }

  if (queue->size == 0) {
    queue->head = node;
    queue->tail = node;
  } else {
    queue->tail->next = node;
    node->prev = queue->tail;
    node->next = NULL;
    queue->tail = node;
  }

  queue->size++;
  return true;
}

proc_node_t* pop_front(proc_queue_t* queue) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }

  proc_node_t* node = queue->head;
  queue->head = queue->head->next;

  if (queue->head != NULL) {
    queue->head->prev = NULL;
  } else {
    queue->tail = NULL;
  }

  queue->size--;
  return node;
}

proc_node_t* pop_back(proc_queue_t* queue) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }

  proc_node_t* node = queue->tail;
  queue->tail = queue->tail->prev;

  if (queue->tail != NULL) {
    queue->tail->next = NULL;
  } else {
    queue->head = NULL;
  }

  queue->size--;
  return node;
}

proc_node_t* front(proc_queue_t* queue) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }
  return queue->head;
}

proc_node_t* back(proc_queue_t* queue) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }
  return queue->tail;
}

proc_node_t* find_node_by_pid(proc_queue_t* queue, pid_t proc_id) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }

  proc_node_t* cursor = queue->head;
  while (cursor != NULL) {
    if (cursor->proc->proc_id == proc_id) {
      return cursor;
    }
    cursor = cursor->next;
  }
  return NULL;
}

proc_node_t* remove_node_by_pid(proc_queue_t* queue, pid_t proc_id) {
  if (queue == NULL || queue->size == 0) {
    return NULL;
  }

  proc_node_t* cursor = queue->head;
  while (cursor != NULL) {
    if (cursor->proc->proc_id == proc_id) {
      proc_node_t* curr = cursor;
      if (cursor->next != NULL) {
        cursor->next->prev = cursor->prev;
      } else {
        queue->tail = cursor->prev;
      }
      if (curr->prev != NULL) {
        cursor->prev->next = cursor->next;
      } else {
        queue->head = cursor->next;
      }
      curr->prev = NULL;
      curr->next = NULL;
      queue->size--;
      return curr;
    }
    cursor = cursor->next;
  }
  return NULL;
}

void free_proc_queue(proc_queue_t* queue) {
  if (queue == NULL) {
    return;
  }

  proc_node_t* cursor = queue->head;
  while (cursor != NULL) {
    proc_node_t* temp = cursor;
    cursor = cursor->next;
    free_proc_node(temp);
  }
  free(queue);
}