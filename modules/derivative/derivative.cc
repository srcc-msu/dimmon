// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

/*
 * TODO Test memory leaks when vector size changes
 */

#include <errno.h>
#include <memory>
#include <time.h>
#include <type_traits>
#include <unordered_map>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "timespec.h"

#include "derivative.h"

struct lastval {
    struct timespec ts;
    size_t vector_size;
    std::unique_ptr<char> values;
};

// Function that takes two elements (by pointers) and returns their
// difference converted to double
typedef double (*diff_func_t)(const void *, const void *);

struct sensor_data {
    size_t         elem_size;
    diff_func_t    func;
    bool           monotonic;
    dmm_sensorid_t dst_id;
};

struct pvt_data {
    dmm_hook_p outhook;
    std::unordered_map<dmm_sensorid_t, sensor_data> sensors;
    std::unordered_map<dmm_sensorid_t, lastval> last_values;
    dmm_size_t last_data_size;
};

template <typename T>
double difference(const void *lhs, const void *rhs)
{
    auto tmp = *reinterpret_cast<const T *>(lhs) - *reinterpret_cast<const T *>(rhs);
    return static_cast<double>(tmp);
}

static diff_func_t type2func[] = {
    [DERIVATIVE_INT32]  = difference<int32_t>,
    [DERIVATIVE_UINT32] = difference<uint32_t>,
    [DERIVATIVE_INT64]  = difference<int64_t>,
    [DERIVATIVE_UINT64] = difference<uint64_t>,
    [DERIVATIVE_FLOAT]  = difference<float>,
    [DERIVATIVE_DOUBLE] = difference<double>,
    [DERIVATIVE_NONE]   = NULL,
};

static size_t type2size[] = {
    [DERIVATIVE_INT32]  = sizeof(int32_t),
    [DERIVATIVE_UINT32] = sizeof(uint32_t),
    [DERIVATIVE_INT64]  = sizeof(int64_t),
    [DERIVATIVE_UINT64] = sizeof(uint64_t),
    [DERIVATIVE_FLOAT]  = sizeof(float),
    [DERIVATIVE_DOUBLE] = sizeof(double),
    [DERIVATIVE_NONE]   = 0,
};

static diff_func_t find_diff_func(enum dmm_derivative_sensor_type type)
{
    diff_func_t func;

    assert(DERIVATIVE_TYPE_MIN <= type && type <= DERIVATIVE_TYPE_MAX && type != DERIVATIVE_NONE);
    func = type2func[type];
    return func;
}

static size_t find_elem_size(enum dmm_derivative_sensor_type type)
{
    size_t size;

    assert(DERIVATIVE_TYPE_MIN <= type && type <= DERIVATIVE_TYPE_MAX && type != DERIVATIVE_NONE);
    size = type2size[type];
    return size;
}

static int merge_sensor_desc(struct pvt_data *pvt, struct dmm_derivative_sensor_desc *desc)
{
    if (desc->src_id == 0)
        return EINVAL;
    if (desc->src_type != DERIVATIVE_NONE) {
        pvt->sensors[desc->src_id].elem_size = find_elem_size(desc->src_type);
        pvt->sensors[desc->src_id].func = find_diff_func(desc->src_type);
        pvt->sensors[desc->src_id].monotonic = desc->monotonic;
        pvt->sensors[desc->src_id].dst_id = desc->dst_id;
    } else {
        pvt->sensors.erase(desc->src_id);
        pvt->last_values.erase(desc->src_id);
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
    pvt->last_data_size = 0;
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
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
    bool step_back_reported = false;
    dmm_datanode_p src_dn, dst_dn;
    dmm_data_p dst_data;
    size_t cur_data_size, used_data_size;
    struct timespec cur_time;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    if (pvt->last_data_size > 0) {
        cur_data_size = pvt->last_data_size;
    } else {
        cur_data_size = DMM_DATA_SIZE(data);
    }
    dst_data = DMM_DATA_CREATE_RAW(0, cur_data_size);
    if (dst_data == NULL) {
        err = ENOMEM;
        goto out;
    }
    used_data_size = 0;
    dst_dn = DMM_DATA_NODES(dst_data);

    clock_gettime(CLOCK_MONOTONIC, &cur_time);
    for (src_dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(src_dn); DMM_DN_ADVANCE(src_dn)) {
        auto sd_it = pvt->sensors.find(src_dn->dn_sensor);
        if (sd_it == pvt->sensors.end())
            continue;
        size_t vector_size = DMM_DN_LEN(src_dn) / sd_it->second.elem_size;
        auto lv_it = pvt->last_values.find(src_dn->dn_sensor);
        if (lv_it != pvt->last_values.end() && lv_it->second.vector_size == vector_size) {
            used_data_size =   reinterpret_cast<char *>(dst_dn)
                             - reinterpret_cast<char *>(DMM_DATA_NODES(dst_data))
                             + sizeof(float) * vector_size
                             + sizeof(dmm_datanode)
                             ;
            if (used_data_size > cur_data_size) {
                cur_data_size *= 2;
                if (used_data_size > cur_data_size)
                    cur_data_size = used_data_size;

                size_t dst_off =   reinterpret_cast<char *>(dst_dn)
                                 - reinterpret_cast<char *>(DMM_DATA_NODES(dst_data));
                if (DMM_DATA_RESIZE(dst_data, 0, cur_data_size) != 0) {
                        /* We have not enough memory to send data
                         * but we can still record last values
                         * and send data collected till this moment
                         */
                        continue;
                }
                // Restore dst_dn after possible data relocation in DMM_DATA_RESIZE
                dst_dn = reinterpret_cast<dmm_datanode_p>(
                             reinterpret_cast<char *>(DMM_DATA_NODES(dst_data)) + dst_off
                         );
            }
            double time_delta = TIMESPEC_DIFF(&cur_time, &lv_it->second.ts);
            if (!step_back_reported && time_delta < 0.0) {
                dmm_log(DMM_LOG_WARN,
                        "Time steps backward, prev time: %ld.%09ld, cur time: %ld.%09ld, delta: %f",
                        (long)cur_time.tv_sec, (long)cur_time.tv_nsec,
                        (long)lv_it->second.ts.tv_sec, (long)lv_it->second.ts.tv_nsec,
                        time_delta
                       );
                /* Report step back only once per data message */
                step_back_reported = true;
            }
            DMM_DN_CREATE(dst_dn, sd_it->second.dst_id, sizeof(float) * vector_size);
            for (size_t i = 0; i < vector_size; ++i) {
                void *cur_val, *last_val;
                double diff;
                cur_val  = DMM_DN_DATA(src_dn, char)  + i * sd_it->second.elem_size;
                last_val = lv_it->second.values.get() + i * sd_it->second.elem_size;
                diff = sd_it->second.func(cur_val, last_val);
                if (diff < 0.0 && sd_it->second.monotonic) {
                    dmm_log(DMM_LOG_WARN,
                            "Data for monotonic sensor #%" PRIdsensorid
                            " decreases, difference: %f"
                            ", time delta: %f"
                            ", derivative: %f",
                            sd_it->first,
                            diff,
                            time_delta,
                            diff / time_delta
                           );
                }
                DMM_DN_DATA(dst_dn, float)[i] = diff / time_delta;
            }
            DMM_DN_ADVANCE(dst_dn);
        } else {
            // No previous last values or vector_size differ
            pvt->last_values[src_dn->dn_sensor] = {
                cur_time,
                vector_size,
                std::unique_ptr<char>(new char[DMM_DN_LEN(src_dn)]),
            };
        }
        pvt->last_values[src_dn->dn_sensor].ts = cur_time;
        memcpy(pvt->last_values[src_dn->dn_sensor].values.get(),
               DMM_DN_DATA(src_dn, char),
               DMM_DN_LEN(src_dn)
              );
    }
    DMM_DN_MKEND(dst_dn);
    cur_data_size =   reinterpret_cast<char *>(dst_dn)
                    - reinterpret_cast<char *>(DMM_DATA_NODES(dst_data))
                      ;
    if (cur_data_size > 0 && cur_data_size < DMM_DATA_SIZE(dst_data)) {
        DMM_DATA_RESIZE(dst_data, 0, cur_data_size);
    }
    if (pvt->outhook != NULL && cur_data_size > 0)
        DMM_DATA_SEND(dst_data, pvt->outhook);
    if (cur_data_size > pvt->last_data_size)
        pvt->last_data_size = cur_data_size;
    DMM_DATA_UNREF(dst_data);

out:
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
    case DMM_MSGTYPE_DERIVATIVE:
        switch (msg->cm_cmd) {
        case DMM_MSG_DERIVATIVE_CLEAR: {
            pvt->sensors.clear();
            pvt->last_values.clear();
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        case DMM_MSG_DERIVATIVE_SET: {
            struct dmm_msg_derivative_set *s = DMM_MSG_DATA(msg, struct dmm_msg_derivative_set);
            dmm_size_t num_descs = (msg->cm_len - sizeof(struct dmm_msg_derivative_set)) / sizeof(struct dmm_derivative_sensor_desc);
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
    "derivative",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
