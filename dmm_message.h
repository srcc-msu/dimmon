// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_MESSAGE_H_
#define DMM_MESSAGE_H_

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_memman.h"
#include "dmm_message_decl.h"
#include "dmm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dmm_data {
    char *da_nodes; // Sequence of dmm_datanode objects
    dmm_size_t  da_len; // size of da_nodes allocated memory

    /* Internal DMM structures */
    dmm_refnum_t    da_refs;
};

/* Public interface for dmm_data */

/*
 * Create struct dmm_data and allocates memory enough for
 * num_nodes dmm_datanode's and additional data_len bytes for
 * dmm_datanode's data fields
 * !!! datalen is overall length of data in all nodes !!!
 * !!! except terminator node                         !!!
 * !!! not length of a single node                    !!!
 * !!! this is the reason it's _RAW                   !!!
 * DOES NOT INITIALIZE ALLOCATED MEMORY!
 */
#define DMM_DATA_CREATE_RAW(numnodes, datalen)  \
    dmm_data_create_raw((numnodes), (datalen))

/*
 * Create struct dmm_data and allocates memory enough for
 * num_nodes dmm_datanode's and additional data_len bytes for
 * each of dmm_datanode's data fields
 * Thus effectively creates num_nodes with length
 * data_len each
 */
#define DMM_DATA_CREATE(numnodes, single_dn_datalen) \
    DMM_DATA_CREATE_RAW((numnodes), ((numnodes) * (single_dn_datalen)))

#define DMM_DATA_RESIZE(data, numnodes, datalen)        \
    dmm_data_resize((data), (numnodes), (datalen))
#if 0
#define DMM_DATA_RM(data)   \
    dmm_data_rm(data)
#endif

#define DMM_DATA_REF(data)      dmm_data_ref((data))
#define DMM_DATA_UNREF(data)    dmm_data_unref((data))

#define DMM_DATA_NODES(data)    ((dmm_datanode_p)((data)->da_nodes + 0))
// Return data length w/o terminating node
// This value is suitable for passing to
// DMM_DATA_CREATE_RAW
#define DMM_DATA_SIZE(data)     ((data)->da_len - sizeof(dmm_datanode))
// Return FULL data length INCLUDING terminating node
#define DMM_DATA_FULLSIZE(data) ((data)->da_len + 0)

/*
 * Internal functions, not for direct use
 */
dmm_data_p dmm_data_create_raw(size_t numnodes, size_t datalen);
int dmm_data_resize(dmm_data_p data, size_t numnodes, size_t datalen);

static inline void dmm_data_ref(dmm_data_p data)
{
    dmm_refacquire(&(data->da_refs));
}

static inline void dmm_data_free(dmm_data_p data)
{
    DMM_FREE(data);
}

static inline void dmm_data_unref(dmm_data_p data)
{
    if (dmm_refrelease(&(data->da_refs))) {
        DMM_FREE(data->da_nodes);
        dmm_data_free(data);
    }
}

struct dmm_datanode {
    dmm_sensorid_t  dn_sensor;
    dmm_size_t      dn_len; /* Length of data field */
    char            dn_data[]; /* The data itself */
};

/* Public interface for datanodes */
#define DMM_DN_SIZE(node)   \
    (sizeof(struct dmm_datanode) + (node)->dn_len)
#define DMM_DN_LEN(node)   \
    ((node)->dn_len + 0)
#define DMM_DN_NEXT(node)     \
    ((dmm_datanode_p)(((char *)(node)) + DMM_DN_SIZE(node)))
#define DMM_DN_ADVANCE(nodevar) \
    (nodevar = DMM_DN_NEXT((nodevar)))
#define DMM_DN_ISEND(node)   \
    ((node)->dn_sensor == 0 && (node)->dn_len == 0)
#define DMM_DN_DATA(node, type) \
    ((type *)(node)->dn_data)
#define DMM_DN_VECTOR(node, type) \
    ((type *)(node)->dn_data)
#define DMM_DN_VECSIZE(node, type) \
    ((node)->dn_len / sizeof(type))
#define DMM_DN_MKEND(node)  \
    memcpy((node), &dmm_empty_datanode, sizeof(struct dmm_datanode))
#define DMM_DN_CREATE(node, sensor, len)    \
    do {                                    \
        (node)->dn_sensor = (sensor);       \
        (node)->dn_len = (len);             \
    } while (0)
#define DMM_DN_FILL(node, sensor, len, data)    \
    do {                                        \
        DMM_DN_CREATE((node), (sensor), (len)); \
        memcpy((node)->dn_data, (data), (len)); \
    } while (0)
#define DMM_DN_CREATE_ADVANCE(nodevar, sensor, len) \
    do {                                            \
        DMM_DN_CREATE((nodevar), (sensor), (len));  \
        DMM_DN_ADVANCE((nodevar));                  \
    } while (0)
#define DMM_DN_FILL_ADVANCE(nodevar, sensor, len, data) \
    do {                                                \
        DMM_DN_FILL((nodevar), (sensor), (len), (data));\
        DMM_DN_ADVANCE(nodevar);                        \
    } while (0)

extern const struct dmm_datanode dmm_empty_datanode;

/*
 * Control message struct
 * cm_ prefix stands for Control Message
 */
struct dmm_msg {
    dmm_id_t    cm_src;        // Source node, 0 means system response
    uint32_t    cm_cmd;        // Command identifier
    uint32_t    cm_type;       // Command type, use uuidgen | tail -c 9 to generate it in hex
    uint32_t    cm_token;      // Token for matching query and response
    uint32_t    cm_flags;
    dmm_size_t  cm_len;        // Length of cm_data field in bytes

    char        cm_data[];     // Message content itself

};

enum {
    DMM_MSG_RESP  = 0x00000001,  // Message is a response
/*
 * Action requested in original message failed (response is error)
 * Should be set on responses only
 */
    DMM_MSG_ERR   = 0x00000002
};

/*
 * Public interface for control messages
 * */

/* Return pointer to data part of message cast to  type * */
#define DMM_MSG_DATA(msg, type) ((type *)((msg)->cm_data))

#define DMM_MSG_CREATE(src, cmd, type, token, flags, len)      \
    dmm_msg_create((src), (cmd), (type), (token), (flags), (len))

#define DMM_MSG_FREE(msg)   \
    dmm_msg_free(msg)

/*
 * Create a response to message msg with len bytes of content
 *  - src is source of response
 *  */
#define DMM_MSG_CREATE_RESP(src, msg, len)   \
    dmm_msg_create_resp((src), (msg), (len))

/*
 * Create a copy of message
 */
#define DMM_MSG_COPY(msg)   \
    dmm_msg_copy(msg)

/* Send control message */
#define DMM_MSG_SEND_ID(dst, msg)  \
    dmm_msg_send_id((dst), (msg))
/* Send control message via node name */
#define DMM_MSG_SEND_ADDR(addr, msg)    \
    dmm_msg_send_addr((addr), (msg))

/*
 * Internal functions, not for direct use
 */
dmm_msg_p dmm_msg_create(dmm_id_t src, uint32_t cmd, uint32_t type, uint32_t token, uint32_t flags, dmm_size_t len);
dmm_msg_p dmm_msg_create_resp(dmm_id_t src, dmm_msg_p msg, dmm_size_t len);
dmm_msg_p dmm_msg_copy(dmm_msg_p msg);

static inline void dmm_msg_free(dmm_msg_p msg)
{
    DMM_FREE(msg);
}

int dmm_msg_send_ref(dmm_node_p node, dmm_msg_p msg);
static inline int dmm_msg_send_id(dmm_id_t dst, dmm_msg_p msg)
{
    dmm_node_p node;
    if ((node = dmm_node_id2ref(dst)) == NULL)
        return ENOENT;
    return dmm_msg_send_ref(node, msg);
}

static inline int dmm_msg_send_addr(const char * addr, dmm_msg_p msg)
{
    dmm_node_p node;
    if ((node = dmm_node_addr2ref(addr)) == NULL)
        return ENOENT;
    return dmm_msg_send_ref(node, msg);
}

enum {
    DMM_MSGTYPE_GENERIC = 0x0ddfe6d5
};

enum {
    DMM_MSG_STARTUP = 1,    // Sent by runtime to the first created node to create other nodes etc.
    DMM_MSG_NODECREATE = 10,
    DMM_MSG_NODERM,
    DMM_MSG_NODECONNECT,
    DMM_MSG_NODEDISCONNECT,
    DMM_MSG_NODESETNAME,
    DMM_MSG_TIMERCREATE = 30,
//    DMM_MSG_TIMERSETNAME, // As of now, timers do not have names
    DMM_MSG_TIMERSET,
    DMM_MSG_TIMERRM,
    DMM_MSG_TIMERSUBSCRIBE,
    DMM_MSG_TIMERUNSUBSCRIBE,
    DMM_MSG_TIMERTRIGGER,       // Timer trigger
    DMM_MSG_SOCKEVENTSUBSCRIBE = 40,
    DMM_MSG_SOCKEVENTUNSUBSCRIBE,
    DMM_MSG_SOCKEVENTTRIGGER,
    DMM_MSG_WAVEFINISH = 100,
    DMM_MSG_WAVEFINISHSUBSCRIBE,
};

struct dmm_msg_startup {
    // File descriptor to read configuration from
    int fd;
    // Number of lines read from configuration file before
    // creating startup node
    int lineno;
};

struct dmm_msg_nodecreate {
    char type[DMM_TYPENAMESIZE];
};

/*
 * dmm_msg_nodecreate does not need resp struct as
 * response is src of response message
 */

/*
 * DMM_MSG_NODERM does not need message struct as it destroys message receiver
 * and no additional info is required
 */

/*
 * dmm_msg_nodeconnect instructs to connect out hook srchook of message receiver to
 * in hook dsthook of node dstnode.
 */
struct dmm_msg_nodeconnect {
    char srchook[DMM_HOOKNAMESIZE];
    char dstnode[DMM_ADDRSIZE];
    char dsthook[DMM_HOOKNAMESIZE];
};

/*
 * dmm_msg_nodedisconnect instructs to break connection between
 * out hook srchook of message receiver and
 * in hook dsthook of node dstnode.
 * */
struct dmm_msg_nodedisconnect {
    char srchook[DMM_HOOKNAMESIZE];
    char dstnode[DMM_ADDRSIZE];
    char dsthook[DMM_HOOKNAMESIZE];
};

struct dmm_msg_nodesetname {
    char name[DMM_NODENAMESIZE];
};

/*
 * DMM_MSG_TIMERCREATE doesn't need message struct as timer is created with no parameters
 */

struct dmm_msg_timercreate_resp {
    dmm_id_t id;
};

struct dmm_msg_timerrm {
    dmm_id_t id;
};

struct dmm_msg_timerset {
	dmm_id_t		id; /* id of timer to set */
    struct timespec next;
    struct timespec interval;
    int 			flags; /* Same flags as in dmm_timer_set */
};

struct dmm_msg_timersubscribe {
    dmm_id_t id; /* Receiver node will be subscribed to timer id */
};

struct dmm_msg_timerunsubscribe {
    dmm_id_t id; /* Receiver node will be unsubscribed from timer id */
};

struct dmm_msg_timertrigger {
    dmm_id_t id;
};

struct dmm_msg_sockeventsubscribe {
    int         fd;
    uint32_t    events;
};

struct dmm_msg_sockeventunsubscribe {
    int fd;
};

struct dmm_msg_sockeventtrigger {
    int fd;

    /* Bitfiels of DMM_SOCKEVENT_{IN, OUT, ERR} constants */
    uint32_t events;
};

#ifdef __cplusplus
} // End of extern "C" {
#endif

#endif /* DMM_MESSAGE_H_ */
