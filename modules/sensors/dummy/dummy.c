// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

/*
 * This is dummy sensor which sends empty data on each timer trigger message
 */

#include <errno.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

struct pvt_data {
    dmm_hook_p hook;
};

static int process_timer_msg(dmm_node_p node)
{
    struct pvt_data *pvt;
    dmm_hook_p hook;
    dmm_data_p data;
    dmm_datanode_p dn;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    hook = pvt->hook;
    if (hook != NULL) {
        data = DMM_DATA_CREATE(0, 0);

        dn = DMM_DATA_NODES(data);
        DMM_DN_MKEND(dn);
        DMM_DATA_SEND(data, hook);
        DMM_DATA_UNREF(data);
    }

    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);
    pvt->hook = NULL;

    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    if (DMM_HOOK_ISIN(hook))
        return EINVAL;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (pvt->hook != NULL)
        return EEXIST;

    pvt->hook = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = NULL;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    int err = 0;

    /* Accept only generic messages */
    if (msg->cm_type != DMM_MSGTYPE_GENERIC) {
        err = ENOTSUP;
        goto err;
    }
    /* Accept only TIMERTRIGGER messages */
    if (msg->cm_cmd != DMM_MSG_TIMERTRIGGER) {
        err = ENOTSUP;
        goto err;
    }
    err = process_timer_msg(node);

err:
    DMM_MSG_FREE(msg);
    return err;
}

static struct dmm_type type = {
    "dummy",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
