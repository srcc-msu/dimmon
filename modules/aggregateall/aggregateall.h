// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef MODULES_AGGREGATEALL_AGGREGATEALL_H_
#define MODULES_AGGREGATEALL_AGGREGATEALL_H_

#include "dmm_types.h"

enum {
    DMM_MSGTYPE_AGGREGATEALL = 0x0d8887d9
};

enum {
    DMM_MSG_AGGREGATEALL_CLEAR = 1,
    DMM_MSG_AGGREGATEALL_SET,
};

enum dmm_aggregateall_sensor_type {
    AGGREGATEALL_INT32,
    AGGREGATEALL_UINT32,
    AGGREGATEALL_INT64,
    AGGREGATEALL_UINT64,
    AGGREGATEALL_FLOAT,
    AGGREGATEALL_DOUBLE,
    AGGREGATEALL_NONE,

    AGGREGATEALL_TYPE_MIN = AGGREGATEALL_INT32,
    AGGREGATEALL_TYPE_MAX = AGGREGATEALL_NONE,
};

struct dmm_aggregateall_sensor_desc {
    dmm_sensorid_t                    src_id;
    enum dmm_aggregateall_sensor_type src_type;
    dmm_sensorid_t                    dst_id;
};

struct dmm_msg_aggregateall_set {
    char                                dummy;
    struct dmm_aggregateall_sensor_desc descs[];
};

struct dmm_aggregateall_data {
    float min;
    float avg;
    float max;
};

#endif /* MODULES_AGGREGATEALL_AGGREGATEALL_H_ */
