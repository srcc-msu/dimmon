// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

#include "prepend.h"

struct pvt_data {
    dmm_datanode_p dn;
    dmm_hook_p     outhook;
};

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    pvt->dn = NULL;
    pvt->outhook = NULL;
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    if (pvt->dn != NULL)
        DMM_FREE(pvt->dn);
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    if (DMM_HOOK_ISIN(hook) && strcmp(DMM_HOOK_NAME(hook), "in") != 0)
        return EINVAL;
    if (DMM_HOOK_ISOUT(hook) && strcmp(DMM_HOOK_NAME(hook), "out") != 0)
        return EINVAL;

    if (DMM_HOOK_ISOUT(hook))
        pvt->outhook = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (DMM_HOOK_ISOUT(hook))
        pvt->outhook = NULL;
}

static int rcvdata(dmm_hook_p hook, dmm_data_p data) {
    dmm_data_p newdata;
    dmm_datanode_p dn;
    int err = 0;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    if (pvt->outhook == NULL) {
        goto finish;
    }
    if (pvt->dn == NULL) {
        DMM_DATA_SEND(data, pvt->outhook);
        goto finish;
    }
    /*
     * Subtract sizeof(struct dmm_datanode) to compensate
     * for terminal node size which is added by DMM_DATA_CREATE_RAW
     */
    newdata = DMM_DATA_CREATE_RAW(0, data->da_len - sizeof(struct dmm_datanode) + DMM_DN_SIZE(pvt->dn));
    if (newdata == NULL) {
        err = ENOMEM;
        goto finish;
    }
    dn = DMM_DATA_NODES(newdata);
    DMM_DN_FILL_ADVANCE(dn, pvt->dn->dn_sensor, pvt->dn->dn_len, pvt->dn->dn_data);
    memcpy(dn, DMM_DATA_NODES(data), data->da_len);
    DMM_DATA_SEND(newdata, pvt->outhook);
    DMM_DATA_UNREF(newdata);

finish:
    DMM_DATA_UNREF(data);
    return err;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    dmm_msg_p resp;
    int err = 0;

#define CREATE_SEND_EMPTY_RESP()                                    \
        do {                                                        \
            resp = DMM_MSG_CREATE_RESP(DMM_NODE_ID(node), msg, 0);  \
            if (resp != NULL) {                                     \
                if (err != 0)                                       \
                    msg->cm_flags |= DMM_MSG_ERR;                   \
                                                                    \
                DMM_MSG_SEND_ID(msg->cm_src, resp);                 \
            } else                                                  \
                err = (err != 0) ? err : ENOMEM;                    \
        } while (0)

    if (msg->cm_flags & DMM_MSG_RESP)
        return 0;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    switch (msg->cm_type) {
    case DMM_MSGTYPE_PREPEND:
        switch (msg->cm_cmd) {
        case DMM_MSG_PREPEND_SET: {
            dmm_datanode_p dn;

            if (pvt->dn != NULL)
                DMM_FREE(pvt->dn);

            dn = &(DMM_MSG_DATA(msg, struct dmm_msg_prepend_set)->dn);
            pvt->dn = (dmm_datanode_p)DMM_MALLOC(DMM_DN_SIZE(dn));
            if (pvt->dn == NULL) {
                err = ENOMEM;
            } else {
                DMM_DN_FILL(pvt->dn, dn->dn_sensor, dn->dn_len, dn->dn_data);
            }
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        case DMM_MSG_PREPEND_CLEAR:
            if (pvt->dn != NULL)
                DMM_FREE(pvt->dn);
            pvt->dn = NULL;
            CREATE_SEND_EMPTY_RESP();
            break;

        default:
            err = ENOTSUP;
            break;
        }
        break;

#undef CREATE_SEND_EMPTY_RESP

    default:
        err = ENOTSUP;
        break;
    }

    DMM_MSG_FREE(msg);
    return err;

}

static struct dmm_type type = {
    "prepend",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
