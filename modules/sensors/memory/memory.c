// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <stdbool.h>
#include <stdio.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "sensors.h"

#define DATAFILE "/proc/meminfo"

struct pvt_data {
    FILE *f;
    dmm_hook_p hook;
};

struct search_item_t {
    const char *header;
    dmm_id_t    sensor_id;
    bool        convert_from_k;
} search_list[] = {
    { "MemTotal",           MEMORY_MEMTOTAL,            true },
    { "MemFree",            MEMORY_MEMFREE,             true },
    { "MemAvailable",       MEMORY_MEMAVAILABLE,        true },
    { "Buffers",            MEMORY_BUFFERS,             true },
    { "Cached",             MEMORY_CACHED,              true },
//    { "SwapCached",         MEMORY_SWAPCACHED,          true },
    { "Active",             MEMORY_ACTIVE,              true },
    { "Inactive",           MEMORY_INACTIVE,            true },
//    { "Active(anon)",       MEMORY_ACTIVE_ANON,         true },
//    { "Inactive(anon)",     MEMORY_INACTIVE_ANON,       true },
//    { "Active(file)",       MEMORY_ACTIVE_FILE,         true },
//    { "Inactive(file)",     MEMORY_INACTIVE_FILE,       true },
//    { "Unevictable",        MEMORY_UNEVICTABLE,         true },
    { "Mlocked",            MEMORY_MLOCKED,             true },
//    { "SwapTotal",          MEMORY_SWAPTOTAL,           true },
//    { "SwapFree",           MEMORY_SWAPFREE,            true },
//    { "Dirty",              MEMORY_DIRTY,               true },
//    { "Writeback",          MEMORY_WRITEBACK,           true },
    { "AnonPages",          MEMORY_ANONPAGES,           true },
    { "Mapped",             MEMORY_MAPPED,              true },
    { "Shmem",              MEMORY_SHMEM,               true },
//    { "Slab",               MEMORY_SLAB,                true },
//    { "SReclaimable",       MEMORY_SRECLAIMABLE,        true },
//    { "SUnreclaim",         MEMORY_SUNRECLAIM,          true },
//    { "KernelStack",        MEMORY_KERNELSTACK,         true },
//    { "PageTables",         MEMORY_PAGETABLES,          true },
//    { "NFS_Unstable",       MEMORY_NFS_UNSTABLE,        true },
//    { "Bounce",             MEMORY_BOUNCE,              true },
//    { "WritebackTmp",       MEMORY_WRITEBACKTMP,        true },
//    { "CommitLimit",        MEMORY_COMMITLIMIT,         true },
//    { "Committed_AS",       MEMORY_COMMITTED_AS,        true },
//    { "VmallocTotal",       MEMORY_VMALLOCTOTAL,        true },
//    { "VmallocUsed",        MEMORY_VMALLOCUSED,         true },
//    { "VmallocChunk",       MEMORY_VMALLOCCHUNK,        true },
//    { "HardwareCorrupted",  MEMORY_HARDWARECORRUPTED,   true },
//    { "AnonHugePages",      MEMORY_ANONHUGEPAGES,       true },
//    { "CmaTotal",           MEMORY_CMATOTAL,            true },
//    { "CmaFree",            MEMORY_CMAFREE,             true },
//    { "HugePages_Total",    MEMORY_HUGEPAGES_TOTAL,     false },
//    { "HugePages_Free",     MEMORY_HUGEPAGES_FREE,      false },
//    { "HugePages_Rsvd",     MEMORY_HUGEPAGES_RSVD,      false },
//    { "HugePages_Surp",     MEMORY_HUGEPAGES_SURP,      false },
//    { "Hugepagesize",       MEMORY_HUGEPAGESIZE,        true },
//    { "DirectMap",          MEMORY_DIRECTMAP4K,         true },
//    { "DirectMap",          MEMORY_DIRECTMAP2M,         true },
    { NULL,                 0,                          false }
};

const int num_sensors = sizeof(search_list) / sizeof(*search_list) - 1;

typedef uint64_t sensor_type;
#define SCNsensor SCNu64

static int process_timer_msg(dmm_node_p node)
{
    struct pvt_data *pvt;
    FILE *f;
    char buf[64];
    dmm_datanode_p dn;
    dmm_data_p data;
    int sensors_found;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    data = NULL;
    if (pvt->hook == NULL)
        goto out;

    f = pvt->f;
    fflush(f);
    rewind(f);

    data = DMM_DATA_CREATE(num_sensors, sizeof(sensor_type));
    if (data == NULL) {
        return ENOMEM;
    }
    dn = DMM_DATA_NODES(data);

    sensors_found = 0;

    while ((sensors_found < num_sensors) && !feof(f)) {
        int retval;
        sensor_type value;
        struct search_item_t *s;

        retval = fscanf(f, "%60[^:]: %" SCNsensor "%*[^\n]%*c", buf, &value);
        if (retval < 2 || retval == EOF) {
            assert(fscanf(f, "%*[^\n]%*c") == 0);
            break;
        }
        for (s = search_list; s->header != NULL; ++s) {
            if (strncmp(buf, s->header, sizeof(buf)) == 0)
                break;
        }
        if (s->header == NULL)
            continue;
        DMM_DN_CREATE(dn, s->sensor_id, sizeof(sensor_type));
        *DMM_DN_DATA(dn, sensor_type) = value * (s->convert_from_k ? 1024 : 1);
        DMM_DN_ADVANCE(dn);
        ++sensors_found;
    }
    DMM_DN_MKEND(dn);

    if (sensors_found > 0)
        DMM_DATA_SEND(data, pvt->hook);

    DMM_DATA_UNREF(data);

out:
    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    int err;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);

    pvt->f = fopen(DATAFILE, "re");
    if (pvt->f == NULL) {
        char errbuf[128], *errmsg;
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot open %s for reading: %s", DATAFILE, errmsg);
        DMM_FREE(pvt);
        return err;
    }
    // Disable buffering to have fresh data on successive reads
    setlinebuf(pvt->f);
    pvt->hook = NULL;
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    fclose(pvt->f);
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    if (DMM_HOOK_ISIN(hook))
        return EINVAL;
    if (strcmp("out", DMM_HOOK_NAME(hook)) != 0)
        return EINVAL;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = NULL;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    int err = 0;

    /* Accept only generic messages */
    if (msg->cm_type != DMM_MSGTYPE_GENERIC) {
        err = ENOTSUP;
        goto err;
    }
    /* Accept only TIMERTRIGGER messages */
    if (msg->cm_cmd != DMM_MSG_TIMERTRIGGER) {
        err = ENOTSUP;
        goto err;
    }
    err = process_timer_msg(node);

err:
    DMM_MSG_FREE(msg);
    return err;

}
static struct dmm_type type = {
    "memory",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
