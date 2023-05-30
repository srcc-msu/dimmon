// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "dmm_base.h"

extern struct dmm_type recv_type;
extern struct dmm_type send_type;

DMM_MODULE_DECLARE(&recv_type, &send_type);
