#if !defined(LF_SINGLE_THREADED)
/* Semaphore utility for reactor C. */

/*************
Copyright (c) 2021, The University of Texas at Dallas.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************/

/**
 * Semaphore utility for reactor C.
 *
 * @author{Soroush Bateni <soroush@utdallas.edu>}
 */

#include "lf_semaphore.h"
#include <assert.h>
#include "util.h" // Defines macros LF_MUTEX_LOCK, etc.

/**
 * @brief Create a new semaphore.
 *
 * @param count The count to start with.
 * @return lf_semaphore_t* Can be NULL on error.
 */
lf_semaphore_t* lf_semaphore_new(size_t count) {
  lf_semaphore_t* semaphore = (lf_semaphore_t*)malloc(sizeof(lf_semaphore_t));
  LF_MUTEX_INIT(&semaphore->mutex);
  LF_COND_INIT(&semaphore->cond, &semaphore->mutex);
  semaphore->count = count;
  return semaphore;
}

/**
 * @brief Release the 'semaphore' and add 'i' to its count.
 *
 * @param semaphore Instance of a semaphore
 * @param i The count to add.
 */
void lf_semaphore_release(lf_semaphore_t* semaphore, size_t i) {
  assert(semaphore != NULL);
  LF_MUTEX_LOCK(&semaphore->mutex);
  semaphore->count += i;
  lf_cond_broadcast(&semaphore->cond);
  LF_MUTEX_UNLOCK(&semaphore->mutex);
}

/**
 * @brief Acquire the 'semaphore'. Will block if count is 0.
 *
 * @param semaphore Instance of a semaphore.
 */
void lf_semaphore_acquire(lf_semaphore_t* semaphore) {
  assert(semaphore != NULL);
  LF_MUTEX_LOCK(&semaphore->mutex);
  while (semaphore->count == 0) {
    lf_cond_wait(&semaphore->cond);
  }
  semaphore->count--;
  LF_MUTEX_UNLOCK(&semaphore->mutex);
}

/**
 * @brief Wait on the 'semaphore' if count is 0.
 *
 * @param semaphore Instance of a semaphore.
 */
void lf_semaphore_wait(lf_semaphore_t* semaphore) {
  assert(semaphore != NULL);
  LF_MUTEX_LOCK(&semaphore->mutex);
  while (semaphore->count == 0) {
    lf_cond_wait(&semaphore->cond);
  }
  LF_MUTEX_UNLOCK(&semaphore->mutex);
}

/**
 * @brief Destroy the 'semaphore'.
 *
 * @param semaphore Instance of a semaphore.
 */
void lf_semaphore_destroy(lf_semaphore_t* semaphore) {
  assert(semaphore != NULL);
  free(semaphore);
}
#endif
