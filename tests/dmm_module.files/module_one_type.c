// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "dmm_base.h"

static struct dmm_type type = {
    "type_one",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);

