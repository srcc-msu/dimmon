// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef MODULES_MEMORY_SENSORS_H_
#define MODULES_MEMORY_SENSORS_H_

enum {
    MEMORY_MEMTOTAL = 520,
    MEMORY_MEMFREE,
    MEMORY_MEMAVAILABLE,
    MEMORY_BUFFERS,
    MEMORY_CACHED,
    MEMORY_SWAPCACHED,
    MEMORY_ACTIVE,
    MEMORY_INACTIVE,
    MEMORY_ACTIVE_ANON,
    MEMORY_INACTIVE_ANON,
    MEMORY_ACTIVE_FILE,
    MEMORY_INACTIVE_FILE,
    MEMORY_UNEVICTABLE,
    MEMORY_MLOCKED,
    MEMORY_SWAPTOTAL,
    MEMORY_SWAPFREE,
    MEMORY_DIRTY,
    MEMORY_WRITEBACK,
    MEMORY_ANONPAGES,
    MEMORY_MAPPED,
    MEMORY_SHMEM,
    MEMORY_SLAB,
    MEMORY_SRECLAIMABLE,
    MEMORY_SUNRECLAIM,
    MEMORY_KERNELSTACK,
    MEMORY_PAGETABLES,
    MEMORY_NFS_UNSTABLE,
    MEMORY_BOUNCE,
    MEMORY_WRITEBACKTMP,
    MEMORY_COMMITLIMIT,
    MEMORY_COMMITTED_AS,
    MEMORY_VMALLOCTOTAL,
    MEMORY_VMALLOCUSED,
    MEMORY_VMALLOCCHUNK,
    MEMORY_HARDWARECORRUPTED,
    MEMORY_ANONHUGEPAGES,
    MEMORY_CMATOTAL,
    MEMORY_CMAFREE,
    MEMORY_HUGEPAGES_TOTAL,
    MEMORY_HUGEPAGES_FREE,
    MEMORY_HUGEPAGES_RSVD,
    MEMORY_HUGEPAGES_SURP,
    MEMORY_HUGEPAGESIZE,
    MEMORY_DIRECTMAP4K,
    MEMORY_DIRECTMAP2M
};

#endif /* MODULES_MEMORY_SENSORS_H_ */