// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

#include "dmm_base_internals.h"
#include "dmm_base.h"
#include "dmm_log.h"

static int dmm_type_register(dmm_type_p type);

/* Processing modules */
int dmm_module_load(const char *fname)
{
    void *modhnd;
    const char *errstr;
    char errbuf[128], *errmsg;
    dmm_module_p mod_desc;
    dmm_type_p *mod_types;
    dmm_moduleinit_t *mod_init_sym;
    dmm_moduleinit_t mod_init;
    int err;

    dmm_debug("Module %s: begin loading", fname);
    modhnd = dlopen(fname, RTLD_NOW | RTLD_LOCAL);
    if (!modhnd) {
        dmm_log(DMM_LOG_ERR, "%s", dlerror());
        return ENOENT;
    }
    dlerror();
    mod_desc = (dmm_module_p)dlsym(modhnd, DMM_MODDESCSYMBOL_STR);
    if ((errstr = dlerror()) != NULL) {
        dmm_log(DMM_LOG_ERR, "%s", errstr);
        return ENOENT;
    }

    dmm_debug("Module %s was compiled from source %s", fname, mod_desc->srcfile);
    if (mod_desc->abiversion != DMM_ABIVERSION) {
        dmm_log(DMM_LOG_ERR, "Module %s ABI version (%u) does not match system version (%u) cannot load", fname, mod_desc->abiversion, DMM_ABIVERSION);
        return EINVAL;
    }
    dlerror();
    mod_init_sym = (dmm_moduleinit_t *)dlsym(modhnd, DMM_MODINITSYMBOL_STR);
    if ((errstr = dlerror()) == NULL && mod_init_sym != NULL) {
        // No dlsym() error, symbol is found
        mod_init = *mod_init_sym;
        err = mod_init();
        if (err != 0) {
            errmsg = strerror_r(err, errbuf, sizeof(errbuf));
            dmm_log(DMM_LOG_ERR, "Failed to initialize module %s: %s", fname, errmsg);
            return err;
        }
    }
    for (mod_types = mod_desc->types; *mod_types != NULL; ++mod_types) {
        if ((err = dmm_type_register(*mod_types)) != 0) {
            dmm_log(DMM_LOG_ERR, "Type \"%s\": cannot register", fname);
        }
    }
    dmm_log(DMM_LOG_INFO, "Module %s: loaded", fname);
    return 0;
}

/*
 * Processing types
 */

/* Type list */
static SLIST_HEAD(, dmm_type) typelist = SLIST_HEAD_INITIALIZER(dmm_type);

dmm_type_p dmm_type_find(const char *name)
{
    dmm_type_p t;
    SLIST_FOREACH(t, &typelist, alltypes) {
        if (strncmp(name, t->tp_name, DMM_TYPENAMESIZE) == 0) {
            return t;
        }
    }
    return NULL;
}

static int dmm_type_register(dmm_type_p type)
{
    size_t namelen = strlen(type->tp_name);
    if (namelen == 0 || namelen > sizeof(type->tp_name) - 1) {
        dmm_log(DMM_LOG_ERR, "Type name \"%s\" is invalid, type rejected", type->tp_name);
        return EINVAL;
    } else if (dmm_type_find(type->tp_name) != NULL) {
        dmm_log(DMM_LOG_ERR, "Type \"%s\": already registered", type->tp_name);
        return EEXIST;
    }

    SLIST_INSERT_HEAD(&typelist, type, alltypes);

    dmm_log(DMM_LOG_INFO, "Type \"%s\": registered", type->tp_name);
    return 0;
}
