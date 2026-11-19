#include "u_job.h"

int job_comparator(ptr_t job_a_ptr, ptr_t job_b_ptr) {
  job_t* job_a = (job_t*)job_a_ptr;
  job_t* job_b = (job_t*)job_b_ptr;

  if (job_a->job_id < job_b->job_id) {
    return -1;
  } else if (job_a->job_id > job_b->job_id) {
    return 1;
  } else {
    return 0;
  }
}