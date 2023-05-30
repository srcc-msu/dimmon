// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef MODULES_DERIVATIVE_DERIVATIVE_H_
#define MODULES_DERIVATIVE_DERIVATIVE_H_

#include "dmm_types.h"

enum {
    DMM_MSGTYPE_DERIVATIVE = 0xa2e13a7c
};

enum {
    DMM_MSG_DERIVATIVE_CLEAR = 1,
    DMM_MSG_DERIVATIVE_SET,
};

enum dmm_derivative_sensor_type {
    DERIVATIVE_INT32,
    DERIVATIVE_UINT32,
    DERIVATIVE_INT64,
    DERIVATIVE_UINT64,
    DERIVATIVE_FLOAT,
    DERIVATIVE_DOUBLE,
    DERIVATIVE_NONE,

    DERIVATIVE_TYPE_MIN = DERIVATIVE_INT32,
    DERIVATIVE_TYPE_MAX = DERIVATIVE_NONE,
};

struct dmm_derivative_sensor_desc {
    dmm_sensorid_t                  src_id;
    enum dmm_derivative_sensor_type src_type;
    bool                            monotonic;
    dmm_sensorid_t                  dst_id;
};

struct dmm_msg_derivative_set {
    char                              dummy;
    struct dmm_derivative_sensor_desc descs[];
};

#endif /* MODULES_DERIVATIVE_DERIVATIVE_H_ */
