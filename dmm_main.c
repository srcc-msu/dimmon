// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#include "dmm_base_internals.h"
#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "dmm_settings.h"

static struct dmm_config_t config;

static void usage()
{
    fprintf(stderr, "Usage: dimmon [-c config_file]\n");
}

static void set_defaults()
{
    config.config_file = "dimmon.conf";
}

static void parse_commandline(int argc, char *argv[])
{
    char optstring[] = "c:";
    int opt;

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
        case 'c':
            config.config_file = optarg;
            break;
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    int err = 0;
    char errbuf[1288], *errmsg;
    FILE *cf;
    char buf[PATH_MAX + 1];
    char starter_type[DMM_TYPENAMESIZE + 1];
    int len;
    int lineno;
    enum {
        MODULES,
        STARTER,
        STARTER_DATA
    } stage = MODULES;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    set_defaults();
    parse_commandline(argc, argv);

    if(dmm_initialize()) {
        fprintf(stderr, "Cannot initialize DMM, exiting");
        exit(1);
    }

    if ((cf = fopen(config.config_file, "r")) == NULL) {
        dmm_emerg("Cannot open config file %s", config.config_file);
    }
    // Make config file read unbuffered so not to consume
    // too much data from file descriptor before
    // passing it to starter module
    setvbuf(cf, NULL, _IONBF, 0);

    for (lineno = 1;; lineno++) {
        if (fgets(buf, sizeof(buf), cf) == NULL)
            break;
        len = strlen(buf);
        if (len == 0)
            break;
        if (len == sizeof(buf) - 1) {
            dmm_emerg("Too long line #%d in config file", lineno);
        }
        if (buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            len--;
        }
        // Skip empty and #-style comment lines
        if (len == 0 || buf[0] == '#')
            continue;
        // Skip lua-style one-line comments
        if (len >= 2 && buf[0] == '-' && buf[1] == '-')
            continue;

        if (strcmp(buf, "==") == 0) {
            stage = STARTER;
            break;
        }

        if (dmm_module_load(buf) == 0) {
            dmm_log(DMM_LOG_INFO, "Module %s loaded", buf);
        } else {
            dmm_log(DMM_LOG_ERR, "Module %s load failed", buf);
        }
    }

    lineno++;
    if (stage != STARTER || (fgets(buf, sizeof(buf), cf) == NULL)) {
        dmm_emerg("No starter type in config file");
    }
    len = strlen(buf);
    if (len == 0) {
        dmm_emerg("No starter type in config file");
    }
    if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    if (len >= DMM_TYPENAMESIZE) {
        dmm_emerg("Too long starter type in config file");
    }
    strncpy(starter_type, buf, sizeof(starter_type));
    starter_type[DMM_TYPENAMESIZE] = '\0';

    lineno++;
    if (fgets(buf, sizeof(buf), cf) == NULL) {
        dmm_emerg("No starter data in config file");
    }
    len = strlen(buf);
    if (len == 0) {
        dmm_emerg("No starter data in config file");
    }
    if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }

    if (strcmp(buf, "==") != 0) {
        dmm_emerg("No starter data in config file");
    }

    fflush(cf);

    dmm_startup(starter_type, fileno(cf), lineno);

    fclose(cf);

    err = dmm_main_loop();

    errmsg = strerror_r(err, errbuf, sizeof(errbuf));
    printf("Finished: %s\n", errmsg);

    return 0;
}

