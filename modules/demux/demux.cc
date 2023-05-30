// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "demux.h"

#include <errno.h>
#include <string>
#include <string.h>
#include <unordered_map>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

struct pvt_data {
    dmm_sensorid_t id; // Sensor id to search in data for demux'ing
    std::unordered_map<std::string, dmm_hook_p> map;
};

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    new(pvt) struct pvt_data;
    pvt->id = 0;
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    pvt->~pvt_data();
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    if (DMM_HOOK_ISIN(hook)) {
        if (strcmp(DMM_HOOK_NAME(hook), "in") != 0)
            return EINVAL;
        else
            return 0;
    }
    pvt->map[DMM_HOOK_NAME(hook)] = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (DMM_HOOK_ISOUT(hook)) {
        pvt->map.erase(DMM_HOOK_NAME(hook));
    }
}

static int rcvdata(dmm_hook_p hook, dmm_data_p data) {
    dmm_data_p newdata;
    dmm_datanode_p dn, dn_src, dn_tgt;
    size_t len;
    int err = 0;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    decltype(pvt->map)::iterator el;

    if (pvt->map.empty() || pvt->id == 0)
        goto finish;

    for (dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(dn); DMM_DN_ADVANCE(dn))
        if (dn->dn_sensor == pvt->id)
            break;

    if (DMM_DN_ISEND(dn))
        goto finish;

    len = strnlen(dn->dn_data, dn->dn_len);
    el = pvt->map.find(std::string(dn->dn_data, len));
    if (el == pvt->map.end())
        goto finish;

    /*
     * We subtract additional sizeof(dmm_datanode) to compensate for terminator node size
     * which is added by DMM_DATA_CREATE_RAW
     */
    newdata = DMM_DATA_CREATE_RAW(0, data->da_len - DMM_DN_SIZE(dn) - sizeof(struct dmm_datanode));
    if (newdata == NULL) {
        err = ENOMEM;
        goto finish;
    }

    dn_tgt = DMM_DATA_NODES(newdata);
    for (dn_src = DMM_DATA_NODES(data); dn_src != dn; DMM_DN_ADVANCE(dn_src)) {
        DMM_DN_FILL_ADVANCE(dn_tgt, dn_src->dn_sensor, dn_src->dn_len, dn_src->dn_data);
    }
    /* Skip node on which we do demux'ing */
    DMM_DN_ADVANCE(dn_src);
    /* Copy the remainder of the data */
    for (; !DMM_DN_ISEND(dn_src); DMM_DN_ADVANCE(dn_src)) {
        DMM_DN_FILL_ADVANCE(dn_tgt, dn_src->dn_sensor, dn_src->dn_len, dn_src->dn_data);
    }
    DMM_DN_MKEND(dn_tgt);

    DMM_DATA_SEND(newdata, el->second);
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
    case DMM_MSGTYPE_DEMUX:
        switch (msg->cm_cmd) {
        case DMM_MSG_DEMUX_SET: {
            pvt->id = DMM_MSG_DATA(msg, struct dmm_msg_demux_set)->id;
            CREATE_SEND_EMPTY_RESP();
            break;
        }

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
    "demux",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
