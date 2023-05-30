// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <stdint.h>
#include "dmm_base.h"

// Hope DMM_ABIVERSION will never reach UINT32_MAX
#undef DMM_ABIVERSION
#define DMM_ABIVERSION UINT32_MAX

static struct dmm_type type = {
    "wrong_abi",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);

