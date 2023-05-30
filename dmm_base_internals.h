// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_BASE_INTERNALS_H_
#define DMM_BASE_INTERNALS_H_

int dmm_initialize(void);
void dmm_startup(const char *type, int fd, int lineno);
int dmm_module_load(const char *fname);

extern int dmm_epollfd;

#endif /* DMM_BASE_INTERNALS_H_ */
