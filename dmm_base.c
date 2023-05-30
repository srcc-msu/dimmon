// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>

#include "dmm_base.h"
#include "dmm_event.h"
#include "dmm_log.h"
#include "dmm_memman.h"
#include "dmm_message.h"
#include "dmm_sockevent.h"
#include "dmm_timer.h"
#include "dmm_wave.h"

/*
 * epoll(7) file descriptor to use for timers and socket events
 */
int dmm_epollfd;

int dmm_initialize(void)
{
    int err;
    char errbuf[128], *errmsg;
    struct timespec now;

    if ((err = dmm_log_init()) != 0) {
        /* Use stderr as last resort for complaining as we can;t initialize logs */
        fprintf(stderr, "Can't initialize logs\n");
        return err;
    }
    if ((dmm_epollfd = epoll_create1(EPOLL_CLOEXEC)) < 0) {
            err = errno;
            errmsg = strerror_r(err, errbuf, sizeof(errbuf));
            dmm_emerg("Can't create epoll instance: %s", errmsg);
    }
    /* Check that we have all the necessary clock types */
    if (clock_gettime(CLOCK_REALTIME, &now)) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_emerg("clock_gettime(CLOCK_REALTIME, ...) is not functional: %s", errmsg);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_emerg("clock_gettime(CLOCK_MONOTONIC, ...) is not functional: %s", errmsg);
    }

    return 0;
}

static int dmm_node_create(const char *typenamestr, dmm_node_p *nodep);

/**
 * Create node of given type and send it a STARTUP generic command
 *
 * @param type type name of startup node
 * @param fd file descriptor to read configuration from
 * @param lineno number of lines read from config file so far
 *               may be used to return correct line number for errors
 */
void dmm_startup(const char *type, int fd, int lineno)
{
    dmm_node_p starter;
    dmm_msg_p msg;

    if (dmm_node_create(type, &starter) != 0) {
        dmm_emerg("Cannot create starter node");
    }
    msg = DMM_MSG_CREATE(0, DMM_MSG_STARTUP, DMM_MSGTYPE_GENERIC, 0, 0, sizeof(struct dmm_msg_startup));
    if (msg == NULL) {
        dmm_emerg("Cannot create starter message");
    }
    DMM_MSG_DATA(msg, struct dmm_msg_startup)->fd = fd;
    DMM_MSG_DATA(msg, struct dmm_msg_startup)->lineno = lineno;
    if (dmm_msg_send_id(DMM_NODE_ID(starter), msg) != 0) {
        dmm_emerg("Startup finished with errors");
    }
}

/*
 * Processing hooks
 * */
static void dmm_hook_rm(dmm_hook_p hook);
static int dmm_hook_rmpeer(dmm_hook_p hook, dmm_hook_p peerhook);

static dmm_hook_p dmm_hook_alloc(void)
{
    return (dmm_hook_p)DMM_MALLOC(sizeof(struct dmm_hook));
}

static inline void dmm_hook_refinit(dmm_hook_p hook)
{
    dmm_refinit(&(hook->hk_refs));
}

/*
 * Create unconnected hook. As hook always must be connected,
 * this function must be called right before connecting new hook
 */
static int dmm_hook_create(dmm_node_p node, enum DMM_HOOK_DIRECTION dir, const char *name, dmm_hook_p *hookp)
{
    /* Assume that hook name is unique for the given node */
    assert(node != NULL);
    size_t i;
    int err;

    if (!DMM_NODE_ISVALID(node))
        return EINVAL;
    if (name == NULL || name[0] == '\0') {
        dmm_log(DMM_LOG_ERR, "Name \"%s\" is invalid for hook");
        return EINVAL;
    }
    for (i = 0; i < sizeof((*hookp)->hk_name) - 1; ++i)
        if (name[i] == '\0' || name[i] == '[' || name[i] == ']')
            break;

    if (name[i] != '\0') {
        dmm_log(DMM_LOG_ERR, "Name \"%s\" is invalid for hook");
        return EINVAL;
    }

    if ((*hookp = dmm_hook_alloc()) == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for hook");
        return ENOMEM;
    }
    /* Invalidate hook for a while */
    (*hookp)->hk_flags = DMM_HOOK_INVALID_BIT;
    dmm_hook_refinit(*hookp);
    (*hookp)->hk_flags |= dir;
    strncpy((*hookp)->hk_name, name, sizeof((*hookp)->hk_name) - 1);
    (*hookp)->hk_name[sizeof((*hookp)->hk_name) - 1] = '\0';
    (*hookp)->hk_node = node;
    (*hookp)->hk_pvt = NULL;
    (*hookp)->hk_rcvdata = NULL;
    LIST_INIT(&((*hookp)->hk_peers));

    if (node->nd_type->newhook != NULL && (err = (*node->nd_type->newhook)(*hookp))) {
        dmm_debug(DMM_PRIHOOK ": rejected", DMM_HOOKINFO(*hookp));
        dmm_hook_free(*hookp);
        *hookp = NULL;
        return err;
    }
    assert(!(DMM_HOOK_ISOUT(*hookp) && (*hookp)->hk_rcvdata != NULL)); // Out hook cannot have rcvdata function
    /* Hook holds a reference to its node */
    (*hookp)->hk_flags &= ~DMM_HOOK_INVALID_BIT; // Now hook is valid
    switch (dir) {
    case DMM_HOOK_IN:
        LIST_INSERT_HEAD(&(node->nd_inhooks), *hookp, hk_nodehooks);
        break;
    case DMM_HOOK_OUT:
        LIST_INSERT_HEAD(&(node->nd_outhooks), *hookp, hk_nodehooks);
        break;
    default:
        assert(0);
    }
    DMM_NODE_REF(node);
    /*
     * Acquire temporary reference for hook which must be released immediately after peer connection
     */
    DMM_HOOK_REF(*hookp);
    dmm_debug(DMM_PRIHOOK ": created", DMM_HOOKINFO(*hookp));
    return 0;
}

static void dmm_hook_rm(dmm_hook_p hook) {
    struct dmm_hookpeer *p, *tmp;

    /* Prevent hook from premature deletion */
    DMM_HOOK_REF(hook);
    /* Invalidate hook since it is being deleted */
    hook->hk_flags |= DMM_HOOK_INVALID_BIT;
    LIST_FOREACH_SAFE(p, &(hook->hk_peers), hp_peerlist, tmp) {
        dmm_hook_rmpeer(p->hp_peer, hook);
        dmm_hook_rmpeer(hook, p->hp_peer);
    }
    /* Now it;s time to delete */
    DMM_HOOK_UNREF(hook);
}

static dmm_hook_p dmm_hook_find(dmm_node_p node, enum DMM_HOOK_DIRECTION dir, const char *name)
{
    dmm_hook_p hook;

    switch (dir) {
    case DMM_HOOK_IN:
        LIST_FOREACH(hook, &(node->nd_inhooks), hk_nodehooks) {
            if (strncmp(hook->hk_name, name, sizeof(hook->hk_name)) == 0)
                break;
        }
        break;
    case DMM_HOOK_OUT:
        LIST_FOREACH(hook, &(node->nd_outhooks), hk_nodehooks) {
            if (strncmp(hook->hk_name, name, sizeof(hook->hk_name)) == 0)
                break;
        }
        break;
    default:
        assert(0);
        return NULL;
    }
    /*
     * Acquire temporary reference for hook which must be released immediately after processing hook finishes
     */
    if (hook != NULL) {
        if (!DMM_HOOK_ISVALID(hook)) {
            hook = NULL;
        } else {
            DMM_HOOK_REF(hook);
        }
    }
    return hook;
}

/* Find existing or create new hook */
static int dmm_hook_get(dmm_node_p node, enum DMM_HOOK_DIRECTION dir, const char *name, dmm_hook_p *hookp)
{
    *hookp = dmm_hook_find(node, dir, name);
    if (*hookp == NULL) {
        return dmm_hook_create(node, dir,  name, hookp);
    } else if (!DMM_HOOK_ISVALID(*hookp)) {
        *hookp = NULL;
        return EINVAL;
    }
    return 0;
}

/* Add dsthook to list of peers in srchook. */
static int dmm_hook_addpeer(dmm_hook_p hook, dmm_hook_p peerhook)
{
    struct dmm_hookpeer *p;

    dmm_debug(DMM_PRIPEER ": begin adding", DMM_PEERINFO(hook, peerhook));
    LIST_FOREACH(p, &(hook->hk_peers), hp_peerlist) {
        if (p->hp_peer == peerhook) {
            dmm_debug("Peer already exists%s", "");
            return EEXIST;
        }
    }

    if ((p = (struct dmm_hookpeer *)DMM_MALLOC(sizeof(*p))) == NULL) {
        dmm_debug("No memory%s", "");
        return ENOMEM;
    }
    p->hp_peer = peerhook;
    LIST_INSERT_HEAD(&(hook->hk_peers), p, hp_peerlist);
    dmm_debug(DMM_PRIPEER ": added", DMM_PEERINFO(hook, peerhook));
    /* Hook holds a reference to its hp_peer */
    DMM_HOOK_REF(peerhook);

    return 0;
}

/* Removes peerhook from list of peers in hook */
static int dmm_hook_rmpeer(dmm_hook_p hook, dmm_hook_p peerhook)
{
    struct dmm_hookpeer *p;

    LIST_FOREACH(p, &(hook->hk_peers), hp_peerlist) {
        if (p->hp_peer == peerhook)
            break;
    }
    if (p == NULL) {
        dmm_debug(DMM_PRIPEER ": cannot remove, not peers", DMM_PEERINFO(hook, peerhook));
        return ENOENT;
    }
    dmm_debug(DMM_PRIPEER ": removed", DMM_PEERINFO(hook, peerhook));
    LIST_REMOVE(p, hp_peerlist);
    DMM_FREE(p);
    DMM_HOOK_UNREF(peerhook);

    return 0;
}

/*
 * Processing nodes
 */

/* Node list */
static LIST_HEAD(, dmm_node) nodelist = LIST_HEAD_INITIALIZER(dmm_node);
static dmm_id_t lastnodeid = 0;

static dmm_node_p dmm_node_alloc(void) {
    return (dmm_node_p)DMM_MALLOC(sizeof(struct dmm_node));
}

static inline void dmm_node_refinit(dmm_node_p node)
{
    dmm_refinit(&(node->nd_refs));
}

static int dmm_node_create(const char *typenamestr, dmm_node_p *nodep)
{
    dmm_type_p type;
    int err;

    *nodep = NULL;

    type = dmm_type_find(typenamestr);
    if (type == NULL) {
        dmm_log(DMM_LOG_ERR, "Cannot find type %s", typenamestr);
        return EINVAL;
    }
    if ((*nodep = dmm_node_alloc()) == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for node");
        return ENOMEM;
    }
    /* Invalidate node till end of construction */
    (*nodep)->nd_flags = DMM_NODE_INVALID;
    /* Initialize reference count */
    dmm_node_refinit(*nodep);

    (*nodep)->nd_id = ++lastnodeid;
    (*nodep)->nd_name[0] = '\0';
    (*nodep)->nd_type = type;
    (*nodep)->nd_rcvmsg = NULL;
    (*nodep)->nd_pvt = NULL;
    LIST_INIT(&((*nodep)->nd_inhooks));
    LIST_INIT(&((*nodep)->nd_outhooks));
    LIST_INIT(&((*nodep)->nd_events));

    if (type->ctor != NULL && (err = type->ctor(*nodep)) != 0) {
        // Error in constructor
        dmm_node_free(*nodep);
        *nodep = NULL;
        return err;
    }
    DMM_NODE_REF(*nodep);
    (*nodep)->nd_flags &= ~DMM_NODE_INVALID;
    LIST_INSERT_HEAD(&nodelist, *nodep, nd_nodes);
    dmm_debug(DMM_PRINODE" of type \"%s\": created", DMM_NODEINFO(*nodep), typenamestr);
    return 0;
}

static void dmm_node_rm(dmm_node_p node)
{
    dmm_hook_p hook, tmphook;

    node->nd_flags |= DMM_NODE_INVALID;
        /* Remove all hooks */
    LIST_FOREACH_SAFE(hook, &(node->nd_inhooks), hk_nodehooks, tmphook)
        dmm_hook_rm(hook);
    LIST_FOREACH_SAFE(hook, &(node->nd_outhooks), hk_nodehooks, tmphook)
        dmm_hook_rm(hook);

    dmm_node_unsubscribeallevents(node);

    /* Release reference to node to launch garbage collection */
    DMM_NODE_UNREF(node);
}

/*
 * Implementation for public functions
 */
int dmm_node_setname(dmm_node_p node, const char *name)
{
    assert(node != NULL);
    size_t i;

    if (!DMM_NODE_ISVALID(node))
        return EINVAL;

    /* NULL or empty name means to reset node name */
    if (name == NULL || name[0] == '\0') {
        node->nd_name[0] = '\0';
        return 0;
    }
    for (i = 0; i < sizeof(node->nd_name) - 1; ++i)
        if (name[i] == '\0' || name[i] == '[' || name[i] == ']')
            break;
    if (name[i] != '\0') {
        dmm_log(DMM_LOG_ERR, "Name \"%s\" is invalid for node");
        return EINVAL;
    }
    strncpy(node->nd_name, name, sizeof(node->nd_name) - 1);
    node->nd_name[sizeof(node->nd_name) - 1] = '\0';
    return 0;
}

/*
 * Implementation for public functions
 */
int dmm_node_unname(dmm_node_p node)
{
    return dmm_node_setname(node, NULL);
}

/*
 * Connect two nodes via given hooks (create if necessary)
 * Connection is made between outhook srchookname of node srcnode to
 *      inhook dsthookname of dstnode
 * Works only for valid nodes
 * */
static int dmm_node_connect(dmm_node_p srcnode, const char *srchookname, dmm_node_p dstnode, const char * dsthookname)
{
    dmm_hook_p srchook, dsthook;
    int err;

    if ((err = dmm_hook_get(srcnode, DMM_HOOK_OUT, srchookname, &srchook)) != 0) {
        return err;
    }
    /*
     * dmm_hook_get returns hook with reference acquired
     * so it is enough to DMM_HOOK_UNREF to delete it
     */
    if ((err = dmm_hook_get(dstnode, DMM_HOOK_IN, dsthookname, &dsthook)) != 0) {
        DMM_HOOK_UNREF(srchook);
        return err;
    }

    err = dmm_hook_addpeer(srchook, dsthook);
    if (err == 0) {
        err = dmm_hook_addpeer(dsthook, srchook);
        if (err != 0) {
            dmm_hook_rmpeer(srchook, dsthook);
        }
    }
    DMM_HOOK_UNREF(dsthook);
    DMM_HOOK_UNREF(srchook);
    return err;
}

/*
 * Disconnect connection made between outhook srchookname of node srcnode to
 *      inhook dsthookname of dstnode
 * Works only for valid nodes.
 * */
static int dmm_node_disconnect(dmm_node_p srcnode, const char *srchookname, dmm_node_p dstnode, const char * dsthookname)
{
    dmm_hook_p srchook, dsthook;
    int err;

    if (!DMM_NODE_ISVALID(srcnode) || !DMM_NODE_ISVALID(dstnode))
        return EINVAL;

    if ((srchook = dmm_hook_find(srcnode, DMM_HOOK_OUT, srchookname)) == NULL) {
        return ENOENT;
    } else if ((dsthook = dmm_hook_find(dstnode, DMM_HOOK_IN, dsthookname)) == NULL) {
        DMM_HOOK_UNREF(srchook);
        return ENOENT;
    }
    err = dmm_hook_rmpeer(srchook, dsthook);
    err = dmm_hook_rmpeer(dsthook, srchook);
    DMM_HOOK_UNREF(dsthook);
    DMM_HOOK_UNREF(srchook);
    return err;
}

dmm_node_p dmm_node_id2ref(dmm_id_t id)
{
    dmm_node_p node;

    LIST_FOREACH(node, &nodelist, nd_nodes) {
        if (DMM_NODE_ID(node) == id)
            break;
    }
    if (node != NULL)
        DMM_NODE_REF(node);
    return node;
}

dmm_node_p dmm_node_name2ref(const char *name)
{
    dmm_node_p node;

    LIST_FOREACH(node, &nodelist, nd_nodes) {
        if (strncmp(DMM_NODE_NAME(node), name, DMM_NODENAMESIZE) == 0)
            break;
    }
    if (node != NULL)
        DMM_NODE_REF(node);
    return node;
}

/*
 * addr is a text string which is
 *  - '[id]', where id is textual node id (decimal number)
 *  - 'name', where name is node name (cannot contain '[' and ']')
 */
dmm_node_p dmm_node_addr2ref(const char *addr)
{
    dmm_node_p node;
    dmm_id_t id;

    if (addr[0] == '[') {
        if (sscanf(addr, "[%" SCNid "]", &id) > 0) {
            node = dmm_node_id2ref(id);
        } else {
            node = NULL;
        }
    } else {
        node = dmm_node_name2ref(addr);
    }
    return node;
}

/*
 * Data management
 **/
const struct dmm_datanode dmm_empty_datanode = {
        .dn_sensor = 0,
        .dn_len = 0,
        .dn_data = {},
};

static inline dmm_data_p dmm_data_alloc(void)
{
    return (dmm_data_p)DMM_MALLOC(sizeof(struct dmm_data));
}

static inline void dmm_data_refinit(dmm_data_p data)
{
    dmm_refinit(&(data->da_refs));
}

dmm_data_p dmm_data_create_raw(size_t numnodes, size_t datalen)
{
    dmm_data_p data;
    data = dmm_data_alloc();
    if (data == NULL) {
        return NULL;
    }

    // + 1 is for terminator datanode
    data->da_len = (numnodes + 1) * sizeof(struct dmm_datanode) + datalen;
    data->da_nodes = (char *)DMM_MALLOC(data->da_len);
    if (data->da_nodes == NULL) {
        dmm_data_free(data);
        return NULL;
    }

    dmm_data_refinit(data);

    DMM_DATA_REF(data);
    return data;
}

int dmm_data_resize(dmm_data_p data, size_t numnodes, size_t datalen)
{
    size_t newlen;
    char *oldnodes;

    oldnodes = data->da_nodes;
    newlen = (numnodes + 1) * sizeof(struct dmm_datanode) + datalen;
    data->da_nodes = (char *)realloc(data->da_nodes, newlen);
    if (data->da_nodes == NULL) {
        data->da_nodes = oldnodes;
        return ENOMEM;
    } else {
        data->da_len = newlen;
        return 0;
    }
}

/*
 * Sending and receiving data
 * */
/*
 * Pass data to inhook hook.
 */
static int dmm_data_passtohook(dmm_data_p data, dmm_hook_p hook)
{
    dmm_rcvdata_t rcvfunc;
    int res;

    assert(DMM_HOOK_ISIN(hook));
    // Skip invalid receiver
    if (!DMM_HOOK_ISVALID(hook))
        return EINVAL;

    if (hook->hk_rcvdata != NULL) {
        rcvfunc = hook->hk_rcvdata;
    } else {
        rcvfunc = hook->hk_node->nd_type->rcvdata;
    }

    if (rcvfunc != NULL) {
        DMM_HOOK_REF(hook);
        res = rcvfunc(hook, data);
        DMM_HOOK_UNREF(hook);
    } else {
        res = ENOTSUP;
    }
    return res;
}

/*
 * Send data via outhook hook.
 * Must be called by node which owns hook via
 * DMM_DATA_SEND macro
 */
void dmm_data_send(dmm_data_p data, dmm_hook_p hook)
{
    struct dmm_hookpeer *hp;

    assert(hook != NULL);
    assert(DMM_HOOK_ISOUT(hook));

    if (!DMM_HOOK_ISVALID(hook))
        return;

    // Avoid deleting hook while here
    DMM_HOOK_REF(hook);

    LIST_FOREACH(hp, &(hook->hk_peers), hp_peerlist) {
        DMM_DATA_REF(data);
        dmm_data_passtohook(data, hp->hp_peer);
    }

    DMM_HOOK_UNREF(hook);
}

/*
 * Control messages processing
 */

/*
 * Create control message
 */
dmm_msg_p dmm_msg_create(dmm_id_t src, uint32_t cmd, uint32_t type, uint32_t token, uint32_t flags, dmm_size_t len)
{
    dmm_msg_p msg;

    msg = (dmm_msg_p)DMM_MALLOC(sizeof(struct dmm_msg) + len);
    if (msg != NULL) {
        msg->cm_src = src;
        msg->cm_cmd = cmd;
        msg->cm_type = type;
        msg->cm_token = token;
        msg->cm_flags = flags;
        msg->cm_len = len;
    }
    return msg;
}

/*
 * Create a response to control message
 */
dmm_msg_p dmm_msg_create_resp(dmm_id_t src, dmm_msg_p msg, dmm_size_t len)
{
    return dmm_msg_create(src, msg->cm_cmd, msg->cm_type, msg->cm_token, DMM_MSG_RESP, len);
}

/*
 * Create a copy of message
 */
dmm_msg_p dmm_msg_copy(dmm_msg_p msg)
{
    dmm_msg_p copy;
    copy = (dmm_msg_p)DMM_MALLOC(sizeof(*msg) + msg->cm_len);
    if (copy != NULL) {
        memcpy(copy, msg, sizeof(*msg) + msg->cm_len);
    }
    return copy;
}

/*
 * Apply generic control message
 */
static int dmm_msg_process_generic(dmm_node_p node, dmm_msg_p msg)
{
    dmm_msg_p resp = NULL;
    int err = 0;
    int msg_freed = 0;

/*
 * Some generic messages should be
 * passed for processing to the node.
 */
#define PASS_MSG_TO_NODE()                              \
    do {                                                \
        if (node->nd_type->rcvmsg != NULL) {            \
            err = (node->nd_type->rcvmsg(node, msg));   \
            msg_freed = 1;                              \
        } else {                                        \
            err = EINVAL;                               \
        }                                               \
    } while (0)

    assert(msg->cm_type == DMM_MSGTYPE_GENERIC);
    switch(msg->cm_cmd) {
    case DMM_MSG_NODECREATE: {
        struct dmm_msg_nodecreate *create_data;
        dmm_node_p newnode;

        assert(msg->cm_len == sizeof(*create_data));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }
        create_data = DMM_MSG_DATA(msg, struct dmm_msg_nodecreate);

        if ((err = dmm_node_create(create_data->type, &newnode)) == 0) {
            /* Main result of NODECREATE (id of newly created node)
             * is the source of the response message.
             */
            resp->cm_src = DMM_NODE_ID(newnode);
        }
        break;
    }

    case DMM_MSG_NODERM: {
        assert(msg->cm_len == 0);

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }
        dmm_node_rm(node);
        break;
    }

    case DMM_MSG_NODECONNECT: {
        struct dmm_msg_nodeconnect *connect_data;
        dmm_node_p dstnode;

        assert(msg->cm_len == sizeof(*connect_data));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        connect_data = DMM_MSG_DATA(msg, struct dmm_msg_nodeconnect);
        dstnode = dmm_node_addr2ref(connect_data->dstnode);
        if (dstnode != NULL) {
            err = dmm_node_connect(node, connect_data->srchook, dstnode, connect_data->dsthook);
            DMM_NODE_UNREF(dstnode);
        } else {
            err = EINVAL;
        }
        break;
    }

    case DMM_MSG_NODEDISCONNECT: {
        struct dmm_msg_nodedisconnect *disconnect_data;
        dmm_node_p dstnode;

        assert(msg->cm_len == sizeof(*disconnect_data));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        disconnect_data = DMM_MSG_DATA(msg, struct dmm_msg_nodedisconnect);
        dstnode = dmm_node_addr2ref(disconnect_data->dstnode);
        if (dstnode != NULL) {
            err = dmm_node_disconnect(node, disconnect_data->srchook, dstnode, disconnect_data->dsthook);
            DMM_NODE_UNREF(dstnode);
        } else {
            err = EINVAL;
        }
        break;
    }

    case DMM_MSG_STARTUP:
        assert (msg->cm_len == sizeof(struct dmm_msg_startup));

        PASS_MSG_TO_NODE();
        break;

    case DMM_MSG_TIMERCREATE: {
        struct dmm_msg_timercreate_resp *tc_resp_data;
        dmm_timer_p timer;

        assert (msg->cm_len == 0);

        resp = dmm_msg_create_resp(0, msg, sizeof(struct dmm_msg_timercreate_resp));
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }
        tc_resp_data = DMM_MSG_DATA(resp, struct dmm_msg_timercreate_resp);
        err = dmm_timer_create(&timer);
        tc_resp_data->id = (err == 0) ? DMM_TIMER_ID(timer) : 0;
        break;
    }

    case DMM_MSG_TIMERSET: {
    	struct dmm_msg_timerset *ts_data;
    	dmm_timer_p timer;

        assert (msg->cm_len == sizeof(struct dmm_msg_timerset));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        ts_data = DMM_MSG_DATA(msg, struct dmm_msg_timerset);
        timer = dmm_timer_id2ref(ts_data->id);
        err = dmm_timer_set(timer, &(ts_data->next), &(ts_data->interval), ts_data->flags);
        DMM_TIMER_UNREF(timer);
        break;
    }

    case DMM_MSG_TIMERSUBSCRIBE: {
    	struct dmm_msg_timersubscribe *ts_data;
    	dmm_timer_p timer;

        assert (msg->cm_len == sizeof(struct dmm_msg_timersubscribe));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        ts_data = DMM_MSG_DATA(msg, struct dmm_msg_timersubscribe);
        timer = dmm_timer_id2ref(ts_data->id);
        err = dmm_timer_subscribe(timer, node);
        DMM_TIMER_UNREF(timer);
        break;
    }

    case DMM_MSG_TIMERUNSUBSCRIBE: {
        struct dmm_msg_timerunsubscribe *tus_data;
        dmm_timer_p timer;

        assert (msg->cm_len == sizeof(struct dmm_msg_timerunsubscribe));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        tus_data = DMM_MSG_DATA(msg, struct dmm_msg_timerunsubscribe);
        timer = dmm_timer_id2ref(tus_data->id);
        err = dmm_timer_unsubscribe(timer, node);
        DMM_TIMER_UNREF(timer);
        break;
    }

    case DMM_MSG_TIMERTRIGGER:
    	assert (msg->cm_len == sizeof(struct dmm_msg_timertrigger));

        PASS_MSG_TO_NODE();
        break;

    case DMM_MSG_TIMERRM: {
        struct dmm_msg_timerrm *tr_data;
        dmm_timer_p timer;

        assert (msg->cm_len == sizeof(struct dmm_msg_timerrm));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        tr_data = DMM_MSG_DATA(msg, struct dmm_msg_timerrm);
        if ((timer = dmm_timer_id2ref(tr_data->id)) == NULL) {
            err = ENOENT;
        } else {
            /* Forcibly unref timer before releasing reference
             * to allow dmm_timer_rm to release the last reference itself.
             * Swear not to use timer after return from dmm_timer_rm
             */
            DMM_TIMER_UNREF(timer);
            dmm_timer_rm(timer);
        }
        break;
    }

    case DMM_MSG_SOCKEVENTSUBSCRIBE: {
        struct dmm_msg_sockeventsubscribe *ses_data;

        assert (msg->cm_len == sizeof(struct dmm_msg_sockeventsubscribe));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        ses_data = DMM_MSG_DATA(msg, struct dmm_msg_sockeventsubscribe);
        err = dmm_sockevent_subscribe(ses_data->fd, ses_data->events, node);
        break;
    }

    case DMM_MSG_SOCKEVENTUNSUBSCRIBE: {
        struct dmm_msg_sockeventunsubscribe *seu_data;

        assert (msg->cm_len == sizeof(struct dmm_msg_sockeventunsubscribe));

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        seu_data = DMM_MSG_DATA(msg, struct dmm_msg_sockeventunsubscribe);
        err = dmm_sockevent_unsubscribe(seu_data->fd, node);
        break;
    }

    case DMM_MSG_SOCKEVENTTRIGGER:
        assert (msg->cm_len == sizeof(struct dmm_msg_sockeventtrigger));

        PASS_MSG_TO_NODE();
        break;

    case DMM_MSG_WAVEFINISHSUBSCRIBE:
        assert (msg->cm_len == 0);

        resp = dmm_msg_create_resp(0, msg, 0);
        if (resp == NULL) {
            err = ENOMEM;
            break;
        }

        err = dmm_wavefinish_subscribe(node);
        break;

    case DMM_MSG_WAVEFINISH:
        assert (msg->cm_len == 0);

        PASS_MSG_TO_NODE();
        break;

    default:
        dmm_log(DMM_LOG_ERR, "Unknown generic message %" PRIu32, msg->cm_cmd);
        err = EINVAL;
    }

    if (resp != NULL) {
        if (err != 0) {
            resp->cm_flags |= DMM_MSG_ERR;
            /* Since we send a response, signal error in the response only,
               not in the return code
            */
            err = 0;
        }
        dmm_msg_send_id(msg->cm_src, resp);
    }
    if (!msg_freed)
        dmm_msg_free(msg);

    return err;

#undef PASS_MSG_TO_NODE
}

/*
 * Really apply control message for node
 */
static int dmm_msg_apply(dmm_node_p node, dmm_msg_p msg)
{
    int err;

    if (!DMM_NODE_ISVALID(node)) {
        dmm_msg_free(msg);
        DMM_NODE_UNREF(node);
        return EINVAL;
    }
    if (msg->cm_type == DMM_MSGTYPE_GENERIC && ((msg->cm_flags & DMM_MSG_RESP) == 0)) {
        /* Response should be processed by node itself */
        err = dmm_msg_process_generic(node, msg);
    } else if (node->nd_type->rcvmsg != NULL) {
        err = node->nd_type->rcvmsg(node, msg);
    } else {
        err = EINVAL;
    }
    DMM_NODE_UNREF(node);
    return err;
}

/*
 * Send control message
 */
int dmm_msg_send_ref(dmm_node_p node, dmm_msg_p msg)
 {
     assert(msg != NULL);
     assert(node != NULL);

     return dmm_msg_apply(node, msg);
 }

/*
 * Perform main loop
 */
int dmm_main_loop(void)
{
    struct timespec now, next;
    int err = 0;
    char errbuf[128], *errmsg;
    int ret;
    int timeout_ms;
    struct epoll_event ev;

    for (;;) {
        if (clock_gettime(CLOCK_REALTIME, &now)) {
            err = errno;
            dmm_debug("clock_gettime returned with error%s", "");
            break;
        }
        err = dmm_timers_next(&next);
        if (err == 0) {
            timeout_ms = (next.tv_sec - now.tv_sec) * 1000 +
                         (next.tv_nsec - now.tv_nsec) / 1000000;
            if (timeout_ms < 0)
                timeout_ms = 0;
        } else if (err == ENOENT) {
            /* No timers, wait indefinitely for sockets etc. */
            /* XXX - check if there is something to wait for (sockets etc.) */
            timeout_ms = -1;
        } else {
            dmm_debug("dmm_timers_next returned with error%s", "");
            break;
        }

        if ((ret = epoll_wait(dmm_epollfd, &ev, 1, timeout_ms)) < 0) {
            err = errno;
            if (err == EINTR) {
                dmm_debug("epoll_wait interrupted by signal, continuing%s", "");
                continue;
            } else {
                errmsg = strerror_r(err, errbuf, sizeof(errbuf));
                dmm_debug("epoll_wait returned with error %s", errmsg);
                break;
            }
        }

        if ((err = dmm_wave_start()) != 0) {
            dmm_debug("dmm_wave_start returned with error%s", "");
            break;
        }

        /* ret == 0 means that epol_wait returned due to timeout */
        if ((ret > 0) && (err = dmm_sockevent_process(&ev)) != 0) {
            dmm_debug("dmm_sockevent_process returned with error%s", "");
            break;
        }

        /*
         * epoll_wait returned due to timeout,
         * at least one timer should trigger
         */
        if ((err = dmm_timers_trigger(ret == 0)) != 0) {
            dmm_debug("dmm_timers_trigger returned with error%s", "");
            break;
        }

        if ((err = dmm_wave_finish()) != 0) {
            dmm_debug("dmm_wave_finish returned with error%s", "");
            break;
        }
    }

    return err;
}
