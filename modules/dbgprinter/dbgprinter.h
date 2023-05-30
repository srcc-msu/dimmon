// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef MODULES_DBGPRINTER_DBGPRINTER_H_
#define MODULES_DBGPRINTER_DBGPRINTER_H_

#include "dmm_types.h"

enum {
    DMM_MSGTYPE_DBGPRINTER = 0xe5a6cb18
};

enum {
    DMM_MSG_DBGPRINTER_CLEAR = 1,
    DMM_MSG_DBGPRINTER_SET,
};

enum dmm_dbgprinter_sensor_type {
    DBGPRINTER_CHAR,
    DBGPRINTER_STRING,
    DBGPRINTER_INT32,
    DBGPRINTER_UINT32,
    DBGPRINTER_INT64,
    DBGPRINTER_UINT64,
    DBGPRINTER_FLOAT,
    DBGPRINTER_DOUBLE,
    DBGPRINTER_NONE,
    DBGPRINTER_HEXDUMP,

    DBGPRINTER_TYPE_MIN = DBGPRINTER_CHAR,
    DBGPRINTER_TYPE_MAX = DBGPRINTER_HEXDUMP,

    DBGPRINTER_DEFAULT = DBGPRINTER_HEXDUMP
};

struct dmm_dbgprinter_sensor_desc {
    dmm_sensorid_t id;
    enum dmm_dbgprinter_sensor_type type;
};

struct dmm_msg_dbgprinter_set {
    char dummy;
    struct dmm_dbgprinter_sensor_desc descs[];
};

#endif /* MODULES_DBGPRINTER_DBGPRINTER_H_ */
