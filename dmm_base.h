// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_BASE_H_
#define DMM_BASE_H_

#include "dmm_base_decl.h"
#include "dmm_log.h"
#include "dmm_memman.h"
#include "dmm_message_decl.h"
#include "dmm_types.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
enum {DMM_DEBUG_BUILD = 1};
#else /* !defined(DEBUG) */
enum {DMM_DEBUG_BUILD = 0};
#endif

#define DMM_ABIVERSION 0

enum { DMM_NODENAMESIZE = 32 };
enum { DMM_HOOKNAMESIZE = 32 };
enum { DMM_TYPENAMESIZE = 32 };
enum { DMM_ADDRSIZE     = 32 };

/*
 * Maximum number of types in a single module
 */
#define DMM_MAXNUMTYPES     16

/* Methods for processing modules */
#define MKSTR_EXPAND(a) #a
#define MKSTR(a) MKSTR_EXPAND(a)

#define DMM_MODDESCSYMBOL dmm_module_desc
#define DMM_MODINITSYMBOL dmm_module_init
#define DMM_MODDESCSYMBOL_STR MKSTR(DMM_MODDESCSYMBOL)
#define DMM_MODINITSYMBOL_STR MKSTR(DMM_MODINITSYMBOL)

/* Macros to use in modules */

/* Declare module.
 *
 * @param ... - list of pointers to dmm_type, types which this module declares
 *
 * Declares module with list of types
 */
#define DMM_MODULE_DECLARE(...)             \
    __attribute__ ((visibility ("default"))) struct dmm_module DMM_MODDESCSYMBOL = { \
        DMM_ABIVERSION,       \
         __FILE__,            \
        {__VA_ARGS__, NULL}   \
    }

typedef int (*dmm_moduleinit_t)(void);

/*
 * Declare module initialization function
 */
#define DMM_MODULEINIT_DECLARE(func)        \
    __attribute__ ((visibility ("default"))) dmm_moduleinit_t DMM_MODINITSYMBOL = func

struct dmm_module {
    uint32_t    abiversion;
    const char *srcfile;
    dmm_type_p  types[DMM_MAXNUMTYPES];
};

/* Type method definitions */
typedef int (*dmm_ctor_t)(dmm_node_p node); // Constructor receives allocated memory and constructs a node
typedef void (*dmm_dtor_t)(dmm_node_p node); // Destructor should deallocate only private node resources
typedef int (*dmm_rcvdata_t)(dmm_hook_p hook, dmm_data_p data); // Data arrived to node via inhook hook
typedef int (*dmm_rcvmsg_t)(dmm_node_p node, dmm_msg_p msg);
typedef int (*dmm_newhook_t)(dmm_hook_p hook); // Make new hook
typedef void (*dmm_rmhook_t)(dmm_hook_p hook); // Destroy hook

struct dmm_type {
    char        tp_name[DMM_TYPENAMESIZE]; /* Unique type name */
    dmm_ctor_t  ctor;
    dmm_dtor_t  dtor;
    dmm_rcvdata_t rcvdata;
    dmm_rcvmsg_t  rcvmsg;
    dmm_newhook_t newhook;
    dmm_rmhook_t  rmhook;

    /* Internal DMM structures */
    SLIST_ENTRY(dmm_type)  alltypes;
};

/* Public methods for types */

/* Functions to implement type public methods */
dmm_type_p  dmm_type_find(const char *name);

struct dmm_node {
    dmm_id_t        nd_id;
    char            nd_name[DMM_NODENAMESIZE]; // Optional node name. May be empty, nonempty must be unique
    uint32_t        nd_flags;
    dmm_type_p      nd_type;
    dmm_rcvmsg_t    nd_rcvmsg; // Optional per node function to receive control messages
    void           *nd_pvt; // Private data for node

    /* Internal DMM structures */
    LIST_HEAD(, dmm_hook) nd_inhooks;
    LIST_HEAD(, dmm_hook) nd_outhooks;
    LIST_HEAD(, dmm_nodeevent) nd_events; // Events the node is subscribed to

    LIST_ENTRY(dmm_node)  nd_nodes; // List of all nodes

    dmm_refnum_t nd_refs;
};

/* Node flags bits */
#define DMM_NODE_INVALID 0x00000001

/* Public methods for node */
#define DMM_NODE_REF(node)  dmm_node_ref(node);
#define DMM_NODE_UNREF(node) dmm_node_unref(node)

#define DMM_NODE_ID(node) ((node)->nd_id + 0)
#define DMM_NODE_PRIVATE(node) ((void *)((char *)(node)->nd_pvt + 0))
#define DMM_NODE_SETPRIVATE(node, pvt) do {(node)->nd_pvt = (pvt);} while (0)
#define DMM_NODE_HASNAME(node) ((node)->nd_name[0] != '\0')
#define DMM_NODE_NAME(node) ((node)->nd_name + 0)
#define DMM_NODE_SETNAME(node, name) dmm_node_setname((node), (name))
#define DMM_NODE_ISVALID(node) (!((node)->nd_flags & DMM_NODE_INVALID))

/* fprintf helpers */
#define DMM_PRINODE "<node #%" PRIuid "(%s) of type %s>"
#define DMM_NODEINFO(node) DMM_NODE_ID(node), DMM_NODE_NAME(node), ((node)->nd_type->tp_name)

/* Implementation functions, not for use directly in modules */
int dmm_node_setname(dmm_node_p node, const char *name);
dmm_node_p dmm_node_id2ref(dmm_id_t id);
dmm_node_p dmm_node_addr2ref(const char *addr);

static inline void dmm_node_free(dmm_node_p node)
{
    DMM_FREE(node);
}

static inline void dmm_node_ref(dmm_node_p node) {
    dmm_refacquire(&(node->nd_refs));
}

static inline void dmm_node_unref(dmm_node_p node) {
    if (dmm_refrelease(&(node->nd_refs))) {
        dmm_debug(DMM_PRINODE ": last reference released, removing", DMM_NODEINFO(node));
        // No references to node exists, so no hooks
        assert(LIST_EMPTY(&(node->nd_inhooks)));
        assert(LIST_EMPTY(&(node->nd_outhooks)));
        node->nd_flags |= DMM_NODE_INVALID;
        if (node->nd_type->dtor != NULL) {
            (node->nd_type->dtor)(node);
        }
        LIST_REMOVE(node, nd_nodes);
        dmm_node_free(node);
    }
}

struct dmm_hookpeer;

struct dmm_hook {
    char          hk_name[DMM_HOOKNAMESIZE];
    uint32_t      hk_flags;
    dmm_node_p    hk_node;       // Owner node
    dmm_rcvdata_t hk_rcvdata;    // Optional per hook function to receive monitoring data
    void         *hk_pvt;        // Private data for hook

    /* Internal DMM structures */
    LIST_ENTRY(dmm_hook) hk_nodehooks; // List of all hooks for node
    LIST_HEAD(, dmm_hookpeer) hk_peers; // List of all connected peers

    dmm_refnum_t hk_refs;
};

struct dmm_hookpeer {
    dmm_hook_p hp_peer;
    LIST_ENTRY(dmm_hookpeer) hp_peerlist;
};

/* Hook flags bits */
#define DMM_HOOK_INVALID_BIT 0x00000001

enum DMM_HOOK_DIRECTION { DMM_HOOK_IN = 0x00000002, DMM_HOOK_OUT = 0x0 };
#define DMM_HOOK_DIRECTION_BIT (DMM_HOOK_IN)
#define DMM_HOOK_DIRECTION_STRING(dir) ((dir) == DMM_HOOK_IN?"IN":"OUT")

/* Public methods for hooks */
#define DMM_HOOK_REF(hook)  dmm_hook_ref(hook)
#define DMM_HOOK_UNREF(hook) dmm_hook_unref(hook)

#define DMM_HOOK_PRIVATE(hook) ((void *)((char *)(hook)->hk_pvt + 0))
#define DMM_HOOK_SETPRIVATE(hook, pvt) do {(hook)->hk_pvt = (pvt);} while (0)
#define DMM_HOOK_NAME(hook) ((hook)->hk_name + 0)
#define DMM_HOOK_ISVALID(hook) (!((hook)->hk_flags & DMM_HOOK_INVALID_BIT))
#define DMM_HOOK_NODE(hook) ((hook)->hk_node + 0)
/* Useful shorcut */
#define DMM_HOOK_NODE_PRIVATE(hook) (DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook)))
#define DMM_HOOK_ISIN(hook) ((hook)->hk_flags & DMM_HOOK_IN)
#define DMM_HOOK_ISOUT(hook) (!DMM_HOOK_ISIN(hook))

/* fprintf helpers */
#define DMM_PRIHOOK "<hook %s direction %s of " DMM_PRINODE ">"
#define DMM_HOOKINFO(hook) DMM_HOOK_NAME(hook), DMM_HOOK_DIRECTION_STRING((hook)->hk_flags & DMM_HOOK_DIRECTION_BIT), DMM_NODEINFO(DMM_HOOK_NODE(hook))
#define DMM_PRIPEER DMM_PRIHOOK " as peer of " DMM_PRIHOOK
#define DMM_PEERINFO(hook, peerhook) DMM_HOOKINFO(peerhook), DMM_HOOKINFO(hook)

/* Implementation functions, not for use directly in modules */
static inline void dmm_hook_ref(dmm_hook_p hook) {
    dmm_refacquire(&(hook->hk_refs));
}

static inline void dmm_hook_free(dmm_hook_p hook)
{
    DMM_FREE(hook);
}

static inline void dmm_hook_unref(dmm_hook_p hook)
{
    dmm_node_p node;

    if (dmm_refrelease(&(hook->hk_refs))) {
        dmm_debug(DMM_PRIHOOK ": removed", DMM_HOOKINFO(hook));
        hook->hk_flags |= DMM_HOOK_INVALID_BIT;
        assert(LIST_EMPTY(&(hook->hk_peers)));
        if (hook->hk_node->nd_type->rmhook != NULL)
            (hook->hk_node->nd_type->rmhook)(hook);
        node = hook->hk_node;
        LIST_REMOVE(hook, hk_nodehooks);
        dmm_hook_free(hook);
        DMM_NODE_UNREF(node);
    }
}

/*
 * Data message passing public interface
 * */
#define DMM_DATA_SEND(data, hook)   dmm_data_send(data, hook)

/*
 * Data message passing private interface
 */
void dmm_data_send(dmm_data_p data, dmm_hook_p hook);

/*
 * Main loop
 */
int dmm_main_loop(void);

#ifdef __cplusplus
} // End of extern "C" {
#endif

#endif /* DMM_BASE_H_ */
