// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef TIMESPEC_H_
#define TIMESPEC_H_

/* Implements a += b for struct timespec */
static inline struct timespec TIMESPEC_INC(struct timespec *a, const struct timespec *b)
{
    a->tv_nsec += b->tv_nsec;
    a->tv_sec  += b->tv_sec;
    if (a->tv_nsec >= 1000000000) {
        a->tv_nsec -= 1000000000;
        a->tv_sec++;
    }
    return *a;
}

static inline double TIMESPEC_DIFF(struct timespec *a, const struct timespec *b)
{
    return a->tv_sec - b->tv_sec + (a->tv_nsec - b->tv_nsec) * 1e-9;
}

static inline int TIMESPEC_ISZERO(struct timespec a)
{
    return a.tv_sec == 0 && a.tv_nsec == 0;
}

static inline int TIMESPEC_GT(struct timespec a, struct timespec b)
{
    return a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec);
}

#endif /* TIMESPEC_H_ */
