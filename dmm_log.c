// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "dmm_log.h"

static const char *prioritynames[] = {
    [DMM_LOG_EMERG]   = "emerg",
    [DMM_LOG_ALERT]   = "alert",
    [DMM_LOG_CRIT]    = "crit",
    [DMM_LOG_ERR]     = "err",
    [DMM_LOG_WARN]    = "warn",
    [DMM_LOG_NOTICE]  = "notice",
    [DMM_LOG_INFO]    = "info",
    [DMM_LOG_DEBUG]   = "debug"
};

static int pid;

int dmm_log_init()
{
    pid = getpid();
    return 0;
}

void dmm_log(int pri, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    dmm_vlog(pri, format, ap);
    va_end(ap);
}

void dmm_emerg(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    dmm_vlog(DMM_LOG_EMERG, format, ap);
    va_end(ap);
    exit(1);
}

void dmm_vlog(int pri, const char *format, va_list ap)
{
    time_t ltime;
    struct tm result;
    char stime[32];

    ltime = time(NULL);
    localtime_r(&ltime, &result);
    strftime(stime, sizeof(stime), "%d %b %Y %T", &result);

    fprintf(stderr, "DMM[%d] %s %s: ", pid, stime, prioritynames[pri]);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
}
