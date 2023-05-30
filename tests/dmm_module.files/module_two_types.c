// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "dmm_base.h"

static struct dmm_type type1 = {
    "type_one",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    {},
};

static struct dmm_type type2 = {
    "type_two",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type1, &type2);

