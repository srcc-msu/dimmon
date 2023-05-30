// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef MODULES_CPULOAD_SENSORS_H_
#define MODULES_CPULOAD_SENSORS_H_

enum {
    CPU_USER = 500,
    CPU_NICE,
    CPU_SYSTEM,
    CPU_IDLE,
    CPU_IOWAIT,
    CPU_IRQ,
    CPU_SOFTIRQ,
    CPU_STEAL,
    CPU_GUEST,
    CPU_GUEST_NICE
};

#endif /* MODULES_CPULOAD_SENSORS_H_ */
