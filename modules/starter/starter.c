// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "dmm_wave.h"
#include "modules/net/ip/recv.h"
#include "modules/net/ip/send.h"
#include "queue.h"

struct pvt_data {
    int is_waiting;    // true when we are waiting for a response
    dmm_msg_p cur_msg; // msg for which we are waiting response

    STAILQ_HEAD(, command) commandlist; // List of messages to send

    // Results of commands
    dmm_id_t ifdata_id, ifdata_id1;
    dmm_id_t cpuload_id;
    dmm_id_t timer_id;
    dmm_id_t wavebuf_id;
    dmm_id_t netsend_id;
    dmm_id_t netrecv_id;
    uint32_t netrecv_flags;
    dmm_id_t dbgprint_id;
    // Number of times timer triggered
    int num_tt;
};

struct command {
    dmm_msg_p (*create_msg)(struct command *);  // Function to create message
    void *arg;                                  // Arguments for creating message
    dmm_id_t rcv;                               // Whom to send message to
    int (*process_resp)(dmm_msg_p resp,         // Function to process response
                        struct command *);
    void *res;
    STAILQ_ENTRY(command) cmds;
};

static int process_command(dmm_node_p node, struct command *cmd)
{
    struct pvt_data *pvt;
    dmm_msg_p msg;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    assert(!pvt->is_waiting);
    if ((msg = cmd->create_msg(cmd)) == NULL)
        return ENOMEM;

    if ((pvt->cur_msg = DMM_MSG_COPY(msg)) == NULL) {
        DMM_MSG_FREE(msg);
        return ENOMEM;
    };

    pvt->is_waiting = 1;
    msg->cm_src = DMM_NODE_ID(node);
    return DMM_MSG_SEND_ID(cmd->rcv, msg);
}

static int process_commands(dmm_node_p node)
{
    struct pvt_data *pvt;
    struct command *cmd, *temp_cmd;
    int err;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    assert(!pvt->is_waiting);

    STAILQ_FOREACH_SAFE(cmd, &pvt->commandlist, cmds, temp_cmd) {
        err = process_command(node, cmd);
        /*
         * Command processing is serialized, so exit if we are still waiting for
         * previous command processing to be finished.
         * This is an error in single-threaded program, but may be
         * valid in multi-threaded.
         * process_response will restart this function after receiving
         * awaited response.
         */
        if (err || pvt->is_waiting)
            return err;
    }
    return 0;
}

static int add_command(dmm_node_p node, struct command *cmd)
{
    struct pvt_data *pvt;
    bool empty;

    assert(cmd);
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    empty = STAILQ_EMPTY(&pvt->commandlist);
    STAILQ_INSERT_TAIL(&pvt->commandlist, cmd, cmds);

    /* If commandlist was empty, start processing new command immediately */
    if (empty)
        return process_commands(node);
    return 0;
}

static void process_error()
{
    dmm_emerg("Can't process startup message, exiting");
    exit(1);
}

static int process_response(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    struct command *cur_cmd;
    dmm_msg_p cur_msg;
    int err = 0;

    assert((msg->cm_flags & DMM_MSG_RESP) != 0);
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    if (!pvt->is_waiting) {
        // We are not waiting for a response, skip
        dmm_log(DMM_LOG_WARN,
                "Node " DMM_PRINODE "received unexpected (not waiting for any) response for command"
                        "type %" PRIu32 " cmd %" PRIu32,
                DMM_NODEINFO(node),
                msg->cm_type,
                msg->cm_cmd
               );
        goto skip;
    }
    cur_cmd = STAILQ_FIRST(&pvt->commandlist);
    assert(cur_cmd != NULL);
    cur_msg = pvt->cur_msg;
    if (msg->cm_cmd != cur_msg->cm_cmd || msg->cm_type != cur_msg->cm_type || msg->cm_token != cur_msg->cm_token) {
        dmm_log(DMM_LOG_WARN,
                "Node " DMM_PRINODE "received unexpected response for command  type %" PRIu32 " cmd %" PRIu32
                " while wating for command  type %" PRIu32 " cmd %" PRIu32,
                DMM_NODEINFO(node),
                msg->cm_type,
                msg->cm_cmd,
                cur_msg->cm_type,
                cur_msg->cm_cmd
               );
        goto skip;
    }

    if (!(msg->cm_flags & DMM_MSG_ERR)) {
        if (cur_cmd->process_resp)
            err = cur_cmd->process_resp(msg, cur_cmd);
    } else {
        dmm_log(DMM_LOG_ALERT,
                "Node " DMM_PRINODE "received error response for command  type %" PRIu32 " cmd %" PRIu32,
                DMM_NODEINFO(node),
                msg->cm_type,
                msg->cm_cmd
               );
        process_error();
    }
    STAILQ_REMOVE_HEAD(&pvt->commandlist, cmds);
    DMM_FREE(cur_cmd);
    pvt->is_waiting = 0;

    if(STAILQ_EMPTY(&pvt->commandlist))
        goto skip;

    /*
     * As process_commands may recursively call process_response (this function)
     * again, we do not use err = process_commands(node) and proceed to skip label,
     * but rather free(msg) and return right now to free memory as early as possible.
     */
    DMM_MSG_FREE(msg);
    return process_commands(node);

skip:
    DMM_MSG_FREE(msg);
    return err;
}

static uint32_t last_token = 0;

#define GET_TOKEN() (++last_token)

/*
 * Just DMM_FREE(cmd->arg)
 * Often it's all that is needed
 */
static int generic_process_resp(dmm_msg_p msg, struct command *cmd)
{
    (void)msg;
    DMM_FREE(cmd->arg);
    return 0;
}

static dmm_msg_p create_nodecreate_msg(struct command *cmd)
{
    dmm_msg_p msg;
    msg = DMM_MSG_CREATE(0, DMM_MSG_NODECREATE, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_nodecreate));
    if (msg != NULL) {
        strncpy(DMM_MSG_DATA(msg, struct dmm_msg_nodecreate)->type, (char *)cmd->arg, DMM_TYPENAMESIZE);
    }
    return msg;
}

static int nodecreate_process_resp(dmm_msg_p msg, struct command *cmd)
{
    *(dmm_id_t *)cmd->res = msg->cm_src;
    DMM_FREE(cmd->arg);
    return 0;
}

static struct command *create_nodecreate_command(dmm_id_t here, const char *typenamestr, dmm_id_t *res)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(DMM_TYPENAMESIZE)) != NULL) {
            cmd->create_msg = create_nodecreate_msg;
            strncpy((char *)cmd->arg, typenamestr, DMM_TYPENAMESIZE);
            cmd->rcv = here;
            cmd->process_resp = nodecreate_process_resp;
            cmd->res = res;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_nodeconnect_msg(struct command *cmd)
{
    dmm_msg_p msg;

    msg = DMM_MSG_CREATE(0, DMM_MSG_NODECONNECT, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_nodeconnect));
    if (msg != NULL) {
        memcpy(msg->cm_data, cmd->arg, msg->cm_len);
    }
    return msg;
}

static struct command *create_nodeconnect_command(dmm_id_t srcnode, const char *srchook, dmm_id_t dstnode, const char *dsthook)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(sizeof(struct dmm_msg_nodeconnect))) != NULL) {
            struct dmm_msg_nodeconnect *nc = (struct dmm_msg_nodeconnect *)cmd->arg;
            strncpy(nc->srchook, srchook, DMM_HOOKNAMESIZE);
            snprintf(nc->dstnode, DMM_ADDRSIZE, "[%" PRIdid "]", dstnode);
            strncpy(nc->dsthook, dsthook, DMM_HOOKNAMESIZE);
            cmd->create_msg = create_nodeconnect_msg;
            cmd->rcv = srcnode;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_nodedisconnect_msg(struct command *cmd)
{
    dmm_msg_p msg;

    msg = DMM_MSG_CREATE(0, DMM_MSG_NODEDISCONNECT, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_nodedisconnect));
    if (msg != NULL) {
        memcpy(msg->cm_data, cmd->arg, msg->cm_len);
    }
    return msg;
}

struct command *create_nodedisconnect_command(dmm_id_t srcnode, const char *srchook, dmm_id_t dstnode, const char *dsthook)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(sizeof(struct dmm_msg_nodedisconnect))) != NULL) {
            struct dmm_msg_nodedisconnect *nd = (struct dmm_msg_nodedisconnect *)cmd->arg;
            strncpy(nd->srchook, srchook, DMM_HOOKNAMESIZE);
            snprintf(nd->dstnode, DMM_ADDRSIZE, "[%" PRIdid "]", dstnode);
            strncpy(nd->dsthook, dsthook, DMM_HOOKNAMESIZE);
            cmd->create_msg = create_nodedisconnect_msg;
            cmd->rcv = srcnode;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_timercreate_msg(struct command *cmd)
{
    (void)cmd;
    return DMM_MSG_CREATE(0, DMM_MSG_TIMERCREATE, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, 0);
}

static int timercreate_process_resp(dmm_msg_p msg, struct command *cmd)
{
    *(dmm_id_t *)cmd->res = DMM_MSG_DATA(msg, struct dmm_msg_timercreate_resp)->id;
    return 0;
}

static struct command *create_timercreate_command(dmm_id_t here, dmm_id_t *tm_id)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->create_msg = create_timercreate_msg;
        cmd->rcv = here;
        cmd->process_resp = timercreate_process_resp;
        cmd->res = tm_id;
    }
    return cmd;
}

static dmm_msg_p create_timerset_msg(struct command *cmd)
{
    dmm_msg_p msg;

    msg = DMM_MSG_CREATE(0, DMM_MSG_TIMERSET, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_timerset));
    if (msg != NULL) {
        memcpy(msg->cm_data, cmd->arg, msg->cm_len);
    }
    return msg;
}

static struct command *create_timerset_command(dmm_id_t node, dmm_id_t tm_id, struct timespec next, struct timespec interval, uint32_t flags)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(sizeof(struct dmm_msg_timerset))) != NULL) {
            struct dmm_msg_timerset *ts = (struct dmm_msg_timerset *)cmd->arg;
            ts->id = tm_id;
            ts->next = next;
            ts->interval = interval;
            ts->flags = flags;
            cmd->create_msg = create_timerset_msg;
            cmd->rcv = node;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_timersubscribe_msg(struct command *cmd)
{
    dmm_msg_p msg;

    msg = DMM_MSG_CREATE(0, DMM_MSG_TIMERSUBSCRIBE, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_timersubscribe));
    if (msg != NULL) {
        memcpy(msg->cm_data, cmd->arg, msg->cm_len);
    }
    return msg;
}

static struct command *create_timersubscribe_command(dmm_id_t node, dmm_id_t tm_id)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(sizeof(struct dmm_msg_timersubscribe))) != NULL) {
            struct dmm_msg_timersubscribe *ts = (struct dmm_msg_timersubscribe *)cmd->arg;
            ts->id = tm_id;
            cmd->create_msg = create_timersubscribe_msg;
            cmd->rcv = node;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_timerrm_msg(struct command *cmd)
{
    dmm_msg_p msg;

    msg = DMM_MSG_CREATE(0, DMM_MSG_TIMERRM, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, sizeof(struct dmm_msg_timerrm));
    if (msg != NULL) {
        memcpy(msg->cm_data, cmd->arg, msg->cm_len);
    }
    return msg;
}

static struct command *create_timerrm_command(dmm_id_t node, dmm_id_t tm_id)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((cmd->arg = DMM_MALLOC(sizeof(struct dmm_msg_timerrm))) != NULL) {
            struct dmm_msg_timerrm *tr = (struct dmm_msg_timerrm *)cmd->arg;
            tr->id = tm_id;
            cmd->create_msg = create_timerrm_msg;
            cmd->rcv = node;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_wavefinishsubscribe_msg(struct command *cmd)
{
    (void)cmd;
    return DMM_MSG_CREATE(0, DMM_MSG_WAVEFINISHSUBSCRIBE, DMM_MSGTYPE_GENERIC, GET_TOKEN(), 0, 0);
}

static struct command *create_wavefinishsubscribe_command(dmm_id_t here)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->create_msg = create_wavefinishsubscribe_msg;
        cmd->rcv = here;
        cmd->process_resp = NULL;
        cmd->res = NULL;
    }
    return cmd;
}

struct netip_alias {
    const char *name;
    int value;
};

static const struct netip_alias domain_aliases[] = {
        { "inet",   AF_INET     },
        { "inet6",  AF_INET6    },
        { NULL,     -1          },
};

static const struct netip_alias type_aliases[] = {
        { "stream", SOCK_STREAM },
        { "dgram",  SOCK_DGRAM  },
        { NULL,     -1          },
};

static const struct netip_alias protocol_aliases[] = {
        { NULL,     -1          },
};

static int parse_triplet_part(const char *part, const struct netip_alias *aliases)
{
    char *s;
    int res;

    for(; aliases->name; ++aliases)
        if (strncmp(part, aliases->name, DMM_HOOKNAMESIZE) == 0)
            return aliases->value;

    errno = 0;
    res = strtol(part, &s, 10);
    if (errno != 0 || *s != '\0' || res < 0)
        return -1;
    return res;

}

/*
 * Parse name in form "domain/type/protocol"
 * domain, type and protocol may be decimal or aliases like 'inet'
 */
static int parse_sock_triplet(int *domain, int *type, int *protocol, const char *hook_name)
{
    char *s, *s1;
    char name[DMM_HOOKNAMESIZE];

    strncpy(name, hook_name, sizeof(name));

    s1 = name;
    s = strchr(s1, '/');
    if (s == NULL)
        return EINVAL;
    *s++ = '\0';
    if ((*domain = parse_triplet_part(s1, domain_aliases))< 0)
        return EINVAL;

    s1 = s;
    s = strchr(s1, '/');
    if (s == NULL)
        return EINVAL;
    *s++ = '\0';
    if ((*type = parse_triplet_part(s1, type_aliases))< 0)
        return EINVAL;

    s1 = s;
    if ((*protocol = parse_triplet_part(s1, protocol_aliases))< 0)
        return EINVAL;

    return 0;
}

static dmm_msg_p create_netiprecvcreatesock_msg(struct command *cmd)
{
    dmm_msg_p msg;
    struct dmm_msg_netip_createsock *nc;

    msg = DMM_MSG_CREATE(0, DMM_MSG_NETIPRECV_CREATESOCK, DMM_MSGTYPE_NETIPRECV, GET_TOKEN(), 0, sizeof(struct dmm_msg_netip_createsock));
    if (msg != NULL) {
        nc = DMM_MSG_DATA(msg, struct dmm_msg_netip_createsock);
        if (parse_sock_triplet(&nc->domain, &nc->type, &nc->protocol, (char *)cmd->arg) != 0) {
            DMM_MSG_FREE(msg);
            msg = NULL;
        }
    }
    return msg;
}

/*
 * @param triplet - "domain/type/protocol"
 * domain, type and protocol may be decimal or aliases like 'inet'
 */
static struct command *create_netiprecvcreatesock_command(dmm_id_t node, const char *triplet) __attribute__ ((unused));
static struct command *create_netiprecvcreatesock_command(dmm_id_t node, const char *triplet)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->arg = (void *)triplet;
        cmd->create_msg = create_netiprecvcreatesock_msg;
        cmd->rcv = node;
        cmd->process_resp = NULL;
        cmd->res = NULL;
    }
    return cmd;
}

static dmm_msg_p create_netipsendcreatesock_msg(struct command *cmd)
{
    dmm_msg_p msg;
    struct dmm_msg_netip_createsock *nc;

    msg = DMM_MSG_CREATE(0, DMM_MSG_NETIPSEND_CREATESOCK, DMM_MSGTYPE_NETIPSEND, GET_TOKEN(), 0, sizeof(struct dmm_msg_netip_createsock));
    if (msg != NULL) {
        nc = DMM_MSG_DATA(msg, struct dmm_msg_netip_createsock);
        if (parse_sock_triplet(&nc->domain, &nc->type, &nc->protocol, (char *)cmd->arg) != 0) {
            DMM_MSG_FREE(msg);
            msg = NULL;
        }
    }
    return msg;
}

/*
 * @param triplet - "domain/type/protocol"
 * domain, type and protocol may be decimal or aliases like 'inet'
 */
static struct command *create_netipsendcreatesock_command(dmm_id_t node, const char *triplet) __attribute__ ((unused));
static struct command *create_netipsendcreatesock_command(dmm_id_t node, const char *triplet)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->arg = (void *)triplet;
        cmd->create_msg = create_netipsendcreatesock_msg;
        cmd->rcv = node;
        cmd->process_resp = NULL;
        cmd->res = NULL;
    }
    return cmd;
}

static dmm_msg_p create_netiprecvbind_msg(struct command *cmd)
{
    dmm_msg_p msg;
    struct addrinfo hints;
    struct addrinfo *result;
    int res;
    const char *port;

    port = (const char *)cmd->arg;
    res = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_protocol = 0;

    if ((res = getaddrinfo(NULL, port, &hints, &result)) != 0) {
        dmm_debug("Cannot parse bind port %s: %s", port, gai_strerror(res));
        return NULL;
    }

    /*
     * XXX - we use only the first address return by getaddrinfo
     * Maybe other should be tried as well
     * This seems not to be a problem right now, as we support only
     * numeric IP addresses, which should not give many variants,
     * but note for the future.
     */
    msg = DMM_MSG_CREATE(0, DMM_MSG_NETIPRECV_BIND, DMM_MSGTYPE_NETIPRECV, GET_TOKEN(), 0, result->ai_addrlen);
    if (msg != NULL) {
        memcpy(DMM_MSG_DATA(msg, void *), result->ai_addr, result->ai_addrlen);
    }
    freeaddrinfo(result);
    return msg;
}

static struct command *create_netiprecvbind_command(dmm_id_t node, const char *port) __attribute__ ((unused));
static struct command *create_netiprecvbind_command(dmm_id_t node, const char *port)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->arg = (void *)port;
        cmd->create_msg = create_netiprecvbind_msg;
        cmd->rcv = node;
        cmd->process_resp = NULL;
        cmd->res = NULL;
    }
    return cmd;
}

static dmm_msg_p create_netipsendconnect_msg(struct command *cmd)
{
    dmm_msg_p msg;
    struct addrinfo hints;
    struct addrinfo *result;
    int res;
    const char **args;

    args = (const char **)cmd->arg;
    res = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_protocol = 0;

    if ((res = getaddrinfo(args[0], args[1], &hints, &result)) != 0) {
        dmm_debug("Cannot parse connect host %s port %s: %s", args[0], args[1], gai_strerror(res));
        return NULL;
    }

    /*
     * XXX - we use only the first address return by getaddrinfo
     * Maybe other should be tried as well
     * This seems not to be a problem right now, as we support only
     * numeric IP addresses, which should not give many variants,
     * but note for the future.
     */
    msg = DMM_MSG_CREATE(0, DMM_MSG_NETIPSEND_CONNECT, DMM_MSGTYPE_NETIPSEND, GET_TOKEN(), 0, result->ai_addrlen);
    if (msg != NULL) {
        memcpy(DMM_MSG_DATA(msg, void *), result->ai_addr, result->ai_addrlen);
    }
    freeaddrinfo(result);
    return msg;
}

static struct command *create_netipsendconnect_command(dmm_id_t node, const char *host, const char *port) __attribute__ ((unused));
static struct command *create_netipsendconnect_command(dmm_id_t node, const char *host, const char *port)
{
    struct command *cmd;
    const char **args;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        if ((args = (const char **)DMM_MALLOC(sizeof(const char *[2]))) != NULL) {
            args[0] = host;
            args[1] = port;
            cmd->arg = args;
            cmd->create_msg = create_netipsendconnect_msg;
            cmd->rcv = node;
            cmd->process_resp = generic_process_resp;
            cmd->res = NULL;
        } else {
            /* cmd->arg = DMM_MALLOC failed */
            DMM_FREE(cmd);
            cmd = NULL;
        }
    }
    return cmd;
}

static dmm_msg_p create_netiprecvgetflags_msg(struct command *cmd)
{
    (void)cmd;
    dmm_msg_p msg;
    msg = DMM_MSG_CREATE(0, DMM_MSG_NETIPRECV_GETFLAGS, DMM_MSGTYPE_NETIPRECV, GET_TOKEN(), 0, 0);
    return msg;
}

static int netiprecvgetflags_process_resp(dmm_msg_p msg, struct command *cmd)
{
    *(uint32_t *)cmd->res = DMM_MSG_DATA(msg, struct dmm_msg_netiprecv_getflags_resp)->flags;
    return 0;
}

static struct command *create_netiprecvgetflags_command(dmm_id_t rcv, uint32_t *flags) __attribute__ ((unused));
static struct command *create_netiprecvgetflags_command(dmm_id_t rcv, uint32_t *flags)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->arg = NULL;
        cmd->create_msg = create_netiprecvgetflags_msg;
        cmd->rcv = rcv;
        cmd->process_resp = netiprecvgetflags_process_resp;
        cmd->res = flags;
    }
    return cmd;
}

static dmm_msg_p create_netiprecvsetflags_msg(struct command *cmd)
{
    (void)cmd;
    dmm_msg_p msg;
    msg = DMM_MSG_CREATE(0,
                         DMM_MSG_NETIPRECV_SETFLAGS,
                         DMM_MSGTYPE_NETIPRECV,
                         GET_TOKEN(),
                         0,
                         sizeof(struct dmm_msg_netiprecv_setflags)
                        );
    if (msg != NULL)
        DMM_MSG_DATA(msg, struct dmm_msg_netiprecv_setflags)->flags = *(uint32_t *)cmd->arg;
    return msg;
}

static struct command *create_netiprecvsetflags_command(dmm_id_t rcv, uint32_t *flags) __attribute__ ((unused));
static struct command *create_netiprecvsetflags_command(dmm_id_t rcv, uint32_t *flags)
{
    struct command *cmd;

    if ((cmd = (struct command *)DMM_MALLOC(sizeof *cmd)) != NULL) {
        cmd->arg = flags;
        cmd->create_msg = create_netiprecvsetflags_msg;
        cmd->rcv = rcv;
        cmd->process_resp = NULL;
        cmd->res = NULL;
    }
    return cmd;
}

static int process_startup_message(dmm_node_p node)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    dmm_id_t node_id = DMM_NODE_ID(node);

    add_command(node, create_nodecreate_command(node_id,
                                                "ifdata",
                                                &pvt->ifdata_id
                                               )
               );
#if 0
    add_command(node, create_nodecreate_command(node_id,
                                                "ifdata",
                                                &pvt->ifdata_id1
                                               )
               );
#endif

    add_command(node, create_nodecreate_command(node_id,
                                                "cpuload",
                                                &pvt->cpuload_id
                                               )
               );
#if 0
    add_command(node, create_nodecreate_command(node_id,
                                                "wavebuf",
                                                &pvt->wavebuf_id
                                               )
               );
    add_command(node, create_nodecreate_command(node_id,
                                                  "net/ip/send",
                                                  &pvt->netsend_id
                                                 )
                 );
    add_command(node, create_netipsendcreatesock_command(pvt->netsend_id,
                                                            "inet/dgram/0"
                                                        )
               );
    add_command(node, create_nodeconnect_command(pvt->ifdata_id,
                                                 "out",
                                                 pvt->wavebuf_id,
                                                 "in"
                                                )
               );
    add_command(node, create_nodeconnect_command(pvt->ifdata_id1,
                                                 "out",
                                                 pvt->wavebuf_id,
                                                 "in"
                                                )
               );
    add_command(node, create_nodeconnect_command(pvt->cpuload_id,
                                                 "out",
                                                 pvt->wavebuf_id,
                                                 "in"
                                                )
               );
    add_command(node, create_nodeconnect_command(pvt->wavebuf_id,
                                                 "out",
                                                 pvt->netsend_id,
                                                 "in"
                                                )
               );
    add_command(node, create_nodecreate_command(node_id,
                                                  "net/ip/recv",
                                                  &pvt->netrecv_id
                                               )
                 );
    add_command(node, create_netiprecvgetflags_command(pvt->netrecv_id, &pvt->netrecv_flags));
    /* XXX - this should be done in some callback, not here */
    pvt->netrecv_flags |= DMM_NETIPRECV_PREPENDADDR | DMM_NETIPRECV_PREPENDTIMESTAMP;

    add_command(node, create_netiprecvsetflags_command(pvt->netrecv_id, &pvt->netrecv_flags));
    add_command(node, create_netiprecvcreatesock_command(pvt->netrecv_id,
                                                            "inet/dgram/0"
                                                        )
               );
#endif

    add_command(node, create_nodecreate_command(node_id,
                                                  "dbgprinter",
                                                  &pvt->dbgprint_id
                                                 )
                 );
    add_command(node, create_nodeconnect_command(pvt->ifdata_id,
                                                 "out",
                                                 pvt->dbgprint_id,
                                                 "in"
                                                )
               );
    add_command(node, create_nodeconnect_command(pvt->cpuload_id,
                                                 "out",
                                                 pvt->dbgprint_id,
                                                 "in"
                                                )
               );

#if 0
    add_command(node, create_netiprecvbind_command(pvt->netrecv_id, "34567"));
    add_command(node, create_netipsendconnect_command(pvt->netsend_id, "127.0.0.1", "34567"));
#endif

    add_command(node, create_timercreate_command(node_id, &pvt->timer_id));
    add_command(node, create_timerset_command(node_id,
                                              pvt->timer_id,
                                              (struct timespec){1, 0},
                                              (struct timespec){1, 0},
                                              0
                                             )
               );

#if 0
    add_command(node, create_timersubscribe_command(pvt->ifdata_id1, pvt->timer_id));
#endif

    add_command(node, create_timersubscribe_command(pvt->ifdata_id, pvt->timer_id));
    add_command(node, create_timersubscribe_command(pvt->cpuload_id, pvt->timer_id));
    add_command(node, create_timersubscribe_command(DMM_NODE_ID(node), pvt->timer_id));

    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);
    STAILQ_INIT(&pvt->commandlist);
    pvt->is_waiting = 0;
    pvt->num_tt = 0;
    return 0;
}

static void dtor(dmm_node_p node)
{
    DMM_FREE(DMM_NODE_PRIVATE(node));
}

static int newhook(dmm_hook_p hook)
{
    (void)hook;
    return EINVAL;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    int err = 0;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);

	if ((msg->cm_flags & DMM_MSG_RESP) != 0)
        return process_response(node, msg);

    /* Accept only generic messages */
    if (msg->cm_type != DMM_MSGTYPE_GENERIC) {
    	err = ENOTSUP;
    	goto error;
    }
    switch(msg->cm_cmd) {
    case DMM_MSG_STARTUP:
    	err = process_startup_message(node);
    	break;

    case DMM_MSG_TIMERTRIGGER:
        pvt->num_tt++;
        dmm_debug("Timer trigger event received%s", "");
        if (pvt->num_tt % 2 && 0) {
            add_command(node,
                    create_wavefinishsubscribe_command(DMM_NODE_ID(node)));
            dmm_debug("Subscribed to wavefinish wave #%" PRIdid,
                    DMM_CURRENT_WAVE());
        }
        if (pvt->num_tt >= 5) {
            add_command(node,
                    create_timerrm_command(DMM_NODE_ID(node), pvt->timer_id));
        }
        break;

    case DMM_MSG_WAVEFINISH:
        dmm_debug("Wavefinish event for wave #%" PRIdid " received", DMM_CURRENT_WAVE());
        break;

    default:
    	err = ENOTSUP;
    	break;
    }

error:
    DMM_MSG_FREE(msg);

    return err;
}

static struct dmm_type type = {
    "starter",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);
