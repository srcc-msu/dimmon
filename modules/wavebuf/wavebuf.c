// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

struct datalist {
    dmm_data_p  data;
    /* Length of datanodes in data without end node. Filled when needed */
    size_t      dnsumlen;
    STAILQ_ENTRY(datalist) datas;
};

struct pvt_data {
    dmm_hook_p  hook;
    STAILQ_HEAD(, datalist) databuf;
};

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;

    pvt->hook = NULL;
    STAILQ_INIT(&pvt->databuf);
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct datalist *dl;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    while (!STAILQ_EMPTY(&pvt->databuf)) {
        /* XXX maybe we should send buffered data */
        dl = STAILQ_FIRST(&pvt->databuf);
        DMM_DATA_UNREF(dl->data);
        STAILQ_REMOVE_HEAD(&pvt->databuf, datas);
        DMM_FREE(dl);
    }
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    if (DMM_HOOK_ISIN(hook))
        return 0;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (strcmp(DMM_HOOK_NAME(hook), "out") != 0)
        return EINVAL;

    pvt->hook = hook;
    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = NULL;
}

static int rcvdata(dmm_hook_p hook, dmm_data_p data) {
    struct datalist *dl;
    dmm_node_p node;
    struct pvt_data *pvt;

    dl = (struct datalist *)DMM_MALLOC(sizeof(*dl));
    if (dl == NULL)
        goto enomem_err;
    dl->data = data;
    dl->dnsumlen = 0;

    node = DMM_HOOK_NODE(hook);
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    if (STAILQ_EMPTY(&pvt->databuf)) {
        dmm_msg_p msg;
        msg = DMM_MSG_CREATE(DMM_NODE_ID(node),
                             DMM_MSG_WAVEFINISHSUBSCRIBE,
                             DMM_MSGTYPE_GENERIC,
                             0, 0, 0
                            );
        if (msg == NULL)
            goto enomem_err;

        DMM_MSG_SEND_ID(DMM_NODE_ID(node), msg);
    }
    STAILQ_INSERT_TAIL(&pvt->databuf, dl, datas);
    return 0;

enomem_err:
    DMM_DATA_UNREF(data);
    return ENOMEM;

}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    struct datalist *dl;
    dmm_datanode_p dn;
    dmm_data_p data;
    size_t len;
    int err = 0;

    if (msg->cm_flags & DMM_MSG_RESP) {
        if (msg->cm_flags & DMM_MSG_ERR)
            dmm_log(DMM_LOG_ERR, DMM_PRINODE "received error response", DMM_NODEINFO(node));
        goto out;
    }
    if (msg->cm_type != DMM_MSGTYPE_GENERIC || msg->cm_cmd != DMM_MSG_WAVEFINISH) {
        err = ENOTSUP;
        goto out;
    }
    /* Only DMM_MSG_WAVEFINISH generic msg is supported */
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    len = 0;
    STAILQ_FOREACH(dl, &pvt->databuf, datas) {
        for (dn = DMM_DATA_NODES(dl->data); !DMM_DN_ISEND(dn); DMM_DN_ADVANCE(dn))
            ;
        dl->dnsumlen = (char *)dn - (char *)DMM_DATA_NODES(dl->data);
        len += dl->dnsumlen;
    }
    data = NULL;
    /*
     * If hook == NULL we have no way to send buffered data,
     * so no need to alloc and prepare it
     */
    if (pvt->hook != NULL)
        data = DMM_DATA_CREATE_RAW(0, len);
    if (data != NULL)
        dn = DMM_DATA_NODES(data);
    else {
        err = ENOMEM;
    }
    while (!STAILQ_EMPTY(&pvt->databuf)) {
        dl = STAILQ_FIRST(&pvt->databuf);
        if (data != NULL) {
            memcpy(dn, DMM_DATA_NODES(dl->data), dl->dnsumlen);
            dn = (dmm_datanode_p)((char *)dn + dl->dnsumlen);
        }
        DMM_DATA_UNREF(dl->data);
        STAILQ_REMOVE_HEAD(&pvt->databuf, datas);
        DMM_FREE(dl);
    }
    if (data != NULL) {
        DMM_DN_MKEND(dn);
        DMM_DATA_SEND(data, pvt->hook);
        DMM_DATA_UNREF(data);
    }

out:
    DMM_MSG_FREE(msg);
    return err;
}

static struct dmm_type type = {
    "wavebuf",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
