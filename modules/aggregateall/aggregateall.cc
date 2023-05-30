// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include "aggregateall.h"

#include <algorithm>
#include <errno.h>
#include <limits>
#include <unordered_map>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

struct agg_data_t {
    double min, sum, max;
    size_t num;
    agg_data_t() :
        min(std::numeric_limits<decltype(this->min)>::max()),
        sum(0),
        max(std::numeric_limits<decltype(this->max)>::lowest()),
        num(0)
    {};
    void update_data(decltype(min) d)
    {
        min = std::min(min, d);
        sum += d;
        max = std::max(max, d);
        num++;
    };
};

// Function that takes an element (by pointers) and returns
// it converted to double
typedef double (*cast_func_t)(const void *);

struct sensor_data {
    size_t          elem_size;
    cast_func_t     cast_func;
    // The resulting sensor id
    dmm_sensorid_t  dst_id;
};

struct pvt_data {
    dmm_hook_p outhook;
    std::unordered_map<dmm_sensorid_t, sensor_data> sensors;
    std::unordered_map<dmm_sensorid_t, agg_data_t>  agg_data;
};

static int process_timer_msg(dmm_node_p node)
{
    struct pvt_data* pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    size_t num_elem = pvt->agg_data.size();
    if (pvt->outhook != NULL && num_elem > 0) {
        dmm_data_p data = DMM_DATA_CREATE(num_elem, sizeof(dmm_aggregateall_data));
        dmm_datanode_p dn = DMM_DATA_NODES(data);
        for (auto && it : pvt->agg_data) {
            auto &&src_id = it.first;
            auto &&agg_data = it.second;
            auto &&dst_id = pvt->sensors[src_id].dst_id;
            DMM_DN_CREATE(dn, dst_id, sizeof(dmm_aggregateall_data));
            DMM_DN_DATA(dn, dmm_aggregateall_data)->min = agg_data.min;
            DMM_DN_DATA(dn, dmm_aggregateall_data)->avg = agg_data.sum / agg_data.num;
            DMM_DN_DATA(dn, dmm_aggregateall_data)->max = agg_data.max;
            DMM_DN_ADVANCE(dn);
        }
        DMM_DN_MKEND(dn);
        DMM_DATA_SEND(data, pvt->outhook);
        DMM_DATA_UNREF(data);
    }
    pvt->agg_data.clear();
    return 0;
}

template <typename T>
double cast_to_double(const void *elem)
{
    return static_cast<double>(*reinterpret_cast<const T *>(elem));
}

static cast_func_t type2func[] = {
    [AGGREGATEALL_INT32]  = cast_to_double<int32_t>,
    [AGGREGATEALL_UINT32] = cast_to_double<uint32_t>,
    [AGGREGATEALL_INT64]  = cast_to_double<int64_t>,
    [AGGREGATEALL_UINT64] = cast_to_double<uint64_t>,
    [AGGREGATEALL_FLOAT]  = cast_to_double<float>,
    [AGGREGATEALL_DOUBLE] = cast_to_double<double>,
    [AGGREGATEALL_NONE]   = NULL,
};

static size_t type2size[] = {
    [AGGREGATEALL_INT32]  = sizeof(int32_t),
    [AGGREGATEALL_UINT32] = sizeof(uint32_t),
    [AGGREGATEALL_INT64]  = sizeof(int64_t),
    [AGGREGATEALL_UINT64] = sizeof(uint64_t),
    [AGGREGATEALL_FLOAT]  = sizeof(float),
    [AGGREGATEALL_DOUBLE] = sizeof(double),
    [AGGREGATEALL_NONE]   = 0,
};

static cast_func_t find_cast_func(enum dmm_aggregateall_sensor_type type)
{
    cast_func_t func;

    assert(AGGREGATEALL_TYPE_MIN <= type && type <= AGGREGATEALL_TYPE_MAX && type != AGGREGATEALL_NONE);
    func = type2func[type];
    return func;
}

static size_t find_elem_size(enum dmm_aggregateall_sensor_type type)
{
    size_t size;

    assert(AGGREGATEALL_TYPE_MIN <= type && type <= AGGREGATEALL_TYPE_MAX && type != AGGREGATEALL_NONE);
    size = type2size[type];
    return size;
}

static int merge_sensor_desc(struct pvt_data *pvt, struct dmm_aggregateall_sensor_desc *desc)
{
    if (desc->src_id == 0)
        return EINVAL;
    if (desc->src_type != AGGREGATEALL_NONE) {
        pvt->sensors[desc->src_id].elem_size = find_elem_size(desc->src_type);
        pvt->sensors[desc->src_id].cast_func = find_cast_func(desc->src_type);
        pvt->sensors[desc->src_id].dst_id = desc->dst_id;
    } else {
        pvt->sensors.erase(desc->src_id);
        pvt->agg_data.erase(desc->src_id);
    }

    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    if ((pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt))) == NULL) {
        return ENOMEM;
    }
    new(pvt) struct pvt_data;
    pvt->outhook = NULL;
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
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_HOOK_NODE_PRIVATE(hook);

    if (DMM_HOOK_ISOUT(hook)) {
        if (pvt->outhook != NULL)
            return EEXIST;
        else
            pvt->outhook = hook;
    }

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_HOOK_NODE_PRIVATE(hook);
    if (DMM_HOOK_ISOUT(hook)) {
        pvt->outhook = NULL;
    }
}

static int rcvdata(dmm_hook_p hook, dmm_data_p data)
{
    int err = 0;
    dmm_datanode_p dn;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    for (dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(dn); DMM_DN_ADVANCE(dn)) {
        auto sensor_it = pvt->sensors.find(dn->dn_sensor);
        if (sensor_it == pvt->sensors.end())
            continue;
        auto agg_it = pvt->agg_data.find(dn->dn_sensor);
        if (agg_it == pvt->agg_data.end()) {
            // No previous aggregated data for the sensor_id
            pvt->agg_data[dn->dn_sensor] = agg_data_t{};
        }
        size_t vector_size = DMM_DN_LEN(dn) / sensor_it->second.elem_size;
        for (size_t i = 0; i < vector_size; ++i) {
            auto cur_val  = DMM_DN_DATA(dn, char)  + i * sensor_it->second.elem_size;
            auto data_elem = sensor_it->second.cast_func(cur_val);
            pvt->agg_data[dn->dn_sensor].update_data(data_elem);
        }
    }

    DMM_DATA_UNREF(data);
    return err;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    int err = 0;
    dmm_msg_p resp;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);

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

    switch (msg->cm_type) {
    case DMM_MSGTYPE_GENERIC:
        switch(msg->cm_cmd) {
        case DMM_MSG_TIMERTRIGGER:
            err = process_timer_msg(node);
            break;

        default:
            err = ENOTSUP;
            break;
        }
        break;

    case DMM_MSGTYPE_AGGREGATEALL:
        switch (msg->cm_cmd) {
        case DMM_MSG_AGGREGATEALL_CLEAR: {
            pvt->sensors.clear();
            pvt->agg_data.clear();
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        case DMM_MSG_AGGREGATEALL_SET: {
            struct dmm_msg_aggregateall_set *s = DMM_MSG_DATA(msg, struct dmm_msg_aggregateall_set);
            dmm_size_t num_descs = (msg->cm_len - sizeof(struct dmm_msg_aggregateall_set)) / sizeof(struct dmm_aggregateall_sensor_desc);
            for (dmm_size_t i = 0; i < num_descs && s->descs[i].src_id != 0; ++i )
                if ((err = merge_sensor_desc(pvt, s->descs + i)))
                    break;
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        default:
            err = ENOTSUP;
            break;
        }
        break;

    default:
        err = ENOTSUP;
        break;
    }

#undef CREATE_SEND_EMPTY_RESP

    DMM_MSG_FREE(msg);
    return err;
}

static struct dmm_type type = {
    "aggregateall",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
