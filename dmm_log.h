// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_LOG_H_
#define DMM_LOG_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// XXX - TODO: add trace level
enum {
    DMM_LOG_EMERG = 0,
    DMM_LOG_ALERT,
    DMM_LOG_CRIT,
    DMM_LOG_ERR,
    DMM_LOG_WARN,
    DMM_LOG_NOTICE,
    DMM_LOG_INFO,
    DMM_LOG_DEBUG
};

int dmm_log_init();

void dmm_log(int pri, const char *format, ...);
void dmm_vlog(int pri, const char *format, va_list ap);
void dmm_emerg(const char *format, ...) __attribute__((__noreturn__));

#ifdef DEBUG
#define dmm_debug(format, ...) dmm_debug_impl(format " at file %s, line %d", __VA_ARGS__, __FILE__, __LINE__)
static inline void dmm_debug_impl(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    dmm_vlog(DMM_LOG_DEBUG, format, ap);
    va_end(ap);
}
#else
static inline void dmm_debug(const char *format, ...) {(void)format;}
#endif

#ifdef __cplusplus
} // End of extern "C" {
#endif

#endif /* DMM_LOG_H_ */
