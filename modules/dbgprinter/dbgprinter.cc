// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>

#include <cctype>
#include <cxxabi.h>
#include <iomanip>
#include <sstream>
#include <typeinfo>
#include <unordered_map>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

#include "dbgprinter.h"

typedef void (*handler_func_t)(dmm_datanode_p);
typedef std::unordered_map<dmm_sensorid_t, handler_func_t> handlers_map;

struct pvt_data {
    handlers_map handlers;
};

static void handler_hexdump(dmm_datanode_p dn)
{
    std::ostringstream buf, ascii_part;
    unsigned int i;

    for (i = 0; i < dn->dn_len; ++i) {
        if (i % 16 == 0) {
            buf << ascii_part.str() << std::endl << "  "
                << std::hex << std::setw(4) << std::setfill('0')
                << i << ": ";
            ascii_part.str("");
        }
        /*
         * (+c & 0xff) is a way to print char as int
         */
        buf << std::hex << std::setw(2) << std::setfill('0')
            << ((+dn->dn_data[i]) & 0xff) << ' ';
        ascii_part << (std::isprint(static_cast<unsigned char>(dn->dn_data[i])) ? dn->dn_data[i] : '.');
    }
    buf << ascii_part.str();

    dmm_log(DMM_LOG_INFO, "DBGPRINT Sensor %" PRIuid ", len %zu (hexdump):%s",
                dn->dn_sensor, (size_t)(dn->dn_len), buf.str().c_str()
             );
}

static void handler_char(dmm_datanode_p dn)
{
    std::ostringstream buf;
    unsigned int i, len;
    char c;

    len = DMM_DN_SIZE(dn);

    for (i = 0; i < len; ++i) {
        c = DMM_DN_DATA(dn, char)[i];
        buf << (std::isprint(c) ? c : '.');
    }

    dmm_log(DMM_LOG_INFO, "DBGPRINT Sensor %" PRIuid ", len %zu (char [%zu]): %s",
                dn->dn_sensor, (size_t)(dn->dn_len), (size_t)(dn->dn_len), buf.str().c_str()
             );
}

static void handler_string(dmm_datanode_p dn)
{
    int len;

    len = DMM_DN_SIZE(dn);

    dmm_log(DMM_LOG_INFO, "DBGPRINT Sensor %" PRIuid ", len %zu (string): %.*s",
                dn->dn_sensor, (size_t)(dn->dn_len), len, dn->dn_data
             );
}

/**
 * @tparam S type of data in sensor
 * @tparam w width of field for each value
 */
template <typename S, int w = 0>
static void handler_type(dmm_datanode_p dn)
{
    std::ostringstream buf;
    unsigned int i, len;
    int status;
    char *demangled_name;

    len = DMM_DN_VECSIZE(dn, S);

    buf << std::left;
    for (i = 0; i < len; ++i) {
        if (w > 0) {
            buf << std::setw(w);
        }
        buf << DMM_DN_DATA(dn, S)[i];
        if (w == 0 && i < len - 1) {
            buf << ' ';
        }
    }

    demangled_name = abi::__cxa_demangle(typeid(S).name(), 0, 0, &status);
    dmm_log(DMM_LOG_INFO, "DBGPRINT Sensor %" PRIuid ", len %zu (%s [%u]): %s",
                dn->dn_sensor, (size_t)(dn->dn_len), demangled_name, len, buf.str().c_str()
             );
    free(demangled_name);
}

static void handler_none(dmm_datanode_p dn)
{
    (void)dn;
}

static handler_func_t find_sensor_handler (struct pvt_data *pvt, dmm_sensorid_t id)
{
    handler_func_t hdl = NULL;

    auto hdl_it = pvt->handlers.find(id);
    if (hdl_it != pvt->handlers.end())
        hdl = hdl_it->second;
    return hdl;
}

static handler_func_t handlers[] = {
    [DBGPRINTER_CHAR]    = handler_char,
    [DBGPRINTER_STRING]  = handler_string,
    [DBGPRINTER_INT32]   = handler_type<int32_t>,
    [DBGPRINTER_UINT32]  = handler_type<uint32_t>,
    [DBGPRINTER_INT64]   = handler_type<int64_t>,
    [DBGPRINTER_UINT64]  = handler_type<uint64_t>,
    [DBGPRINTER_FLOAT]   = handler_type<float, 9>,
    [DBGPRINTER_DOUBLE]  = handler_type<double, 9>,
    [DBGPRINTER_NONE]    = handler_none,
    [DBGPRINTER_HEXDUMP] = handler_hexdump,
};

static handler_func_t find_handler_func(enum dmm_dbgprinter_sensor_type type)
{
    handler_func_t func;

    assert(DBGPRINTER_TYPE_MIN <= type && type <= DBGPRINTER_TYPE_MAX);
    func = (handlers[type] != NULL) ? handlers[type] : handlers[DBGPRINTER_DEFAULT];
    return func;
}

static void process_dn(struct pvt_data *pvt, dmm_datanode_p dn)
{
    handler_func_t func;

    func = find_sensor_handler(pvt, dn->dn_sensor);
    if (func == NULL)
        func = find_handler_func(DBGPRINTER_DEFAULT);

    assert(func != NULL);
    func(dn);
}

static int merge_sensor_desc(struct pvt_data *pvt, struct dmm_dbgprinter_sensor_desc *desc)
{
    if (desc->id == 0)
        return EINVAL;

    pvt->handlers[desc->id] = find_handler_func(desc->type);

    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);
    new(&pvt->handlers) handlers_map;

    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    pvt->handlers.~handlers_map();

    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    if (DMM_HOOK_ISOUT(hook))
        return EINVAL;

    return 0;
}

static int rcvdata(dmm_hook_p hook, dmm_data_p data)
{
    dmm_datanode_p dn;

    dmm_log(DMM_LOG_INFO, "DBGPRINT Packet len %zu (data size %zu)",
            (size_t)data->da_len, (size_t)DMM_DATA_SIZE(data)
           );
    for (dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(dn); DMM_DN_ADVANCE(dn))
        process_dn((struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook)), dn);

    DMM_DATA_UNREF(data);
    return 0;
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
    case DMM_MSGTYPE_DBGPRINTER:
        switch (msg->cm_cmd) {
        case DMM_MSG_DBGPRINTER_CLEAR: {
            pvt->handlers.clear();
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        case DMM_MSG_DBGPRINTER_SET: {
            struct dmm_msg_dbgprinter_set *s = DMM_MSG_DATA(msg, struct dmm_msg_dbgprinter_set);
            dmm_size_t num_descs = (msg->cm_len - sizeof(struct dmm_msg_dbgprinter_set)) / sizeof(struct dmm_dbgprinter_sensor_desc);
            for (dmm_size_t i = 0; i < num_descs && s->descs[i].id != 0; ++i )
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
    "dbgprinter",
    ctor,
    dtor,
    rcvdata,
    rcvmsg,
    newhook,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);
