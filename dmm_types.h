// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_TYPES_H_
#define DMM_TYPES_H_

#include <inttypes.h>

typedef uint32_t    dmm_id_t;
#define PRIdid  PRIu32
#define PRIuid  PRIu32
#define SCNid   SCNu32


typedef volatile uint32_t    dmm_refnum_t;
#define DMM_REFNUM_MAX  UINT32_MAX
#define PRIdrefnum  PRIu32
#define PRIurefnum  PRIu32

typedef uint32_t    dmm_size_t;
typedef uint32_t    dmm_sensorid_t;
#define PRIdsensorid  PRIu32
#define PRIusensorid  PRIu32
#define SCNsensorid   SCNu32

#endif /* DMM_TYPES_H_ */
