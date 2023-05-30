// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>

#include "dmm_base.h"
#include "dmm_message.h"

/* Accept any IN hook */
static int newhook(dmm_hook_p hook)
{
    if (DMM_HOOK_ISOUT(hook))
        return EINVAL;
    else
        return 0;
}

/* Discard any incoming data */
static int rcvdata(dmm_hook_p hook, dmm_data_p data) {
    (void)hook;
    DMM_DATA_UNREF(data);
    return 0;
}

static struct dmm_type type = {
    "blackhole",
    NULL,
    NULL,
    rcvdata,
    NULL,
    newhook,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);
