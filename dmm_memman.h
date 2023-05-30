// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_MEMMAN_H_
#define DMM_MEMMAN_H_

#include <assert.h>
#include <stdlib.h>

#include "dmm_types.h"

#define DMM_MALLOC(size) malloc(size)
#define DMM_REALLOC(ptr, size) realloc(ptr, size)
#define DMM_FREE(ptr) free(ptr)

static inline void dmm_refinit(volatile dmm_refnum_t *refs)
{
    *refs = 0;
}

static inline void dmm_refacquire(volatile dmm_refnum_t *refs)
{
    assert(*refs < DMM_REFNUM_MAX);
    ++*refs;
}

static inline int dmm_refrelease(volatile dmm_refnum_t *refs)
{
    assert(*refs > 0);
    return --*refs == 0;
}

#endif /* DMM_MEMMAN_H_ */
