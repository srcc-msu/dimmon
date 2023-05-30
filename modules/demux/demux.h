// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DEMUX_H_
#define DEMUX_H_

#include "dmm_types.h"

enum {
    DMM_MSGTYPE_DEMUX = 0x30f09177
};

enum {
    DMM_MSG_DEMUX_SET = 1,
    DMM_MSG_DEMUX_GET,
};

struct dmm_msg_demux_set {
    dmm_id_t id;
};

#endif /* DEMUX_H_ */
