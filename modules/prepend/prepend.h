// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef PREPEND_H_
#define PREPEND_H_

#include "dmm_message.h"

enum {
    DMM_MSGTYPE_PREPEND = 0x8ed9b58c
};

enum {
    DMM_MSG_PREPEND_SET = 1,
    DMM_MSG_PREPEND_GET,
    DMM_MSG_PREPEND_CLEAR,
};

struct dmm_msg_prepend_set {
    struct dmm_datanode dn;
};

#endif /* PREPEND_H_ */
