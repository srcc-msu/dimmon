// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <array>
#include <functional>
#include <unordered_map>

#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>

extern "C" {
#include "luajit-2.1/lauxlib.h"
#include "luajit-2.1/lualib.h"
}

#include "dmm_base.h"
#include "dmm_message.h"

/*
 * Helper functions to match response to original
 * control message
 */
typedef std::array<uint32_t, 3> msg_triplet;

namespace std {
template <> struct hash<msg_triplet> {
    size_t operator()(const msg_triplet &t) const {
        size_t h = 0;
        for (auto i: t) {
            h ^= std::hash<decltype(i)>()(i) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
}

/*
 * Original Lua allows calling yield only from a coroutine.
 * This seems not to be the case (although not stated explicitly)
 * for LuaJIT which we use now, but for sake of compatibility we use
 * initial lua_State Li only to create new threads. All work is done
 * in those threads which (I believe) will be destroyed by Lua
 * garbage collector.
 */
struct pvt_data {
    lua_State *Li;
    /*
     * If Lua thread sent a message and is waiting for an answer,
     * msg2thread maps (type, cmd, token) triplet to reference (int value
     * in Lua registry corresponding to Lua thread
     * which sent the message and is waiting for a response
     */
    std::unordered_map<msg_triplet, int> msg2thread;
    struct dmm_msg_startup startup_info;
};

static const char *fd_luareader(lua_State *L, void *data, size_t *size)
{
    (void)L;
    // XXX may not work in multithreaded version
    static char buf[1024];
    struct dmm_msg_startup *s = reinterpret_cast<struct dmm_msg_startup *>(data);
    ssize_t len;

    if (s->lineno > 0) {
        // Fill the buf with \n to emulate lines
        // read from config file (feed them to Lua as empty)
        len = std::min(static_cast<size_t>(s->lineno), sizeof(buf));
        memset(buf, '\n', len);
        s->lineno -= len;
    } else {
        // Read the real configuration
        len = read(s->fd, buf, sizeof(buf));
        if (len == -1) {
            dmm_log(DMM_LOG_ALERT, "Error reading from fd %d received in startup message", s->fd);
            goto eof_or_error;
        }
        if (len == 0) {
            // EOF
            goto eof_or_error;
        }
    }
    *size = len;
    return buf;

eof_or_error:
    *size = 0;
    return NULL;
}

/**
 * @brief Calls Lua code
 *
 * call_lua_code requires that main Lua thread
 * stack (DMM_NODE_PRIVATE(node)->Li) contain thread L,
 * i.e. L should be on the top of DMM_NODE_PRIVATE(node)->Li stack.
 * Stack L should contain a function to resume (if nfuncs == 1) and
 * nargs arguments to pass to code which is to be resumed
 * After return call_lua_code empties thread (L) stack
 * and pops L from main (DMM_NODE_PRIVATE(node)->Li) stack
 *
 * @param node context to call code
 * @param L Lua thread (child of node->pvt->Li)
 * @param nfuncs 1 if a function to be called is pushed on
 *               the stack, 0 otherwise (if the coroutine is
 *               to be resumed)
 * @param nargs number of arguments to pass to Lua code
 * @return error code
 */
static int call_lua_code(dmm_node_p node, lua_State *L, int nfuncs, int nargs)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    int level_before, nresults;
    int res, err = 0;

    level_before = lua_gettop(L);
    res = lua_resume(L, nargs);
    nresults =  lua_gettop(L) - level_before + nfuncs + nargs;

    if (res == 0) {
        /*
         * The first result is the error code:
         * - if it is integer, return as is
         * - if there is no results, return ENOTSUP
         * - otherwise, convert to boolean, true is OK (0 return code)
         *     false is not OK (return EINVAL)
         * All other results are ignored
         * Pop the thread and all the results.
         */
        if (nresults > 0) {
            if (nresults > 1) {
                dmm_log(DMM_LOG_NOTICE, "Lua code in node " DMM_PRINODE " returned with %d (>1) results, ignoring all but the first",
                                        DMM_NODEINFO(node), nresults
                       );
                // Pop ignored results
                lua_pop(L, nresults - 1);
            }

            if (lua_isnumber(L, -1)) {
                err = lua_tointeger(L,  -1);
            } else if (lua_isnil(L, -1)) {
                err = ENOTSUP;
            } else {
                err = lua_toboolean(L,  -1) ? 0 : EINVAL;
            }
            lua_pop(L, 1);
        } else {
            err = ENOTSUP;
        }
        lua_pop(pvt->Li, 1);
    } else if (res == LUA_YIELD) {
        if (nresults == 2) {
            // Lua code is sending control message
            dmm_id_t dst;
            dmm_msg_p msg;
            int thr_ref;
            msg_triplet m;

            dst = luaL_checkint(L, -2);
            luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
            msg = (dmm_msg_p)lua_touserdata(L, -1);
            lua_pop(L, 2);
            lua_pushthread(L);
            thr_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            m = {msg->cm_type, msg->cm_cmd, msg->cm_token};
            pvt->msg2thread[m] = thr_ref;
            // Pop the thread from main stack
            lua_pop(pvt->Li, 1);
            err = DMM_MSG_SEND_ID(dst, msg);
        } else {
            dmm_log(DMM_LOG_ERR, "Lua code yielded incorrectly with %d results", nresults);
            err = EINVAL;
            lua_pop(L, nresults);
            lua_pop(pvt->Li, 1);
        }
    } else {
        dmm_log(DMM_LOG_ERR, "Luacontrol: run lua code in node " DMM_PRINODE " failed with status %d: %s",
                              DMM_NODEINFO(node), res, luaL_checkstring(L, -1)
               );
        if (res == LUA_ERRMEM) {
            err =  ENOMEM;
        } else {
            err = EINVAL;
        }
        lua_pop(L, nresults);
        lua_pop(pvt->Li, 1);
    }

    return err;
}

static int process_startup_message(dmm_node_p node, struct dmm_msg_startup *s)
{
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    lua_State *L = lua_newthread(pvt->Li);
    int res = 0, err = 0;

    pvt->startup_info = *s;
    if ((res = lua_load(L, fd_luareader, &pvt->startup_info, "Config file")) != 0) {
        dmm_log(DMM_LOG_ERR, "Luacontrol: load lua code from config file failed with status %d: %s", res, luaL_checkstring(L, -1));
        if (res == LUA_ERRMEM)
            err = ENOMEM;
        else
            err = EINVAL;
        lua_pop(L, 1);
        lua_pop(pvt->Li, 1);
        goto out;
    }

    err = call_lua_code(node, L, 1, 0);
    if (err != 0)
        dmm_log(DMM_LOG_ERR, "Luacontrol: run code from config file failed");

out:
    return err;
}

static int process_response(dmm_node_p node, dmm_msg_p msg)
{
    int thr_ref;
    msg_triplet m;
    lua_State *L;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    int err = 0;

    m = {msg->cm_type, msg->cm_cmd, msg->cm_token};
    auto it = pvt->msg2thread.find(m);
    if (it == pvt->msg2thread.end()) {
        dmm_log(DMM_LOG_ERR, "Node " DMM_PRINODE "received unexpected response for command"
                              "type %" PRIu32 " cmd %" PRIu32,
                              DMM_NODEINFO(node),
                              msg->cm_type,
                              msg->cm_cmd
               );
        err = EINVAL;
        goto out;
    }
    thr_ref = it->second;
    pvt->msg2thread.erase(it);

    lua_rawgeti(pvt->Li, LUA_REGISTRYINDEX, thr_ref);
    L = lua_tothread(pvt->Li, -1);
    luaL_unref(pvt->Li,  LUA_REGISTRYINDEX, thr_ref);
    lua_pushlightuserdata(L, msg);
    err = call_lua_code(node, L, 0, 1);

out:
    DMM_MSG_FREE(msg);
    return err;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    Dl_info info;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    new(pvt) pvt_data;
    DMM_NODE_SETPRIVATE(node, pvt);

    /*
     * Promote Lua dynamic library (libluajit-5.1.so as of now) to RTLD_GLOBAL
     * to allow subsequent Lua/C modules to resolve
     * Lua C API symbols
     * This would not be needed if Glibc had Solaris's
     * RTLD_GROUP but sadly it does not have one now.
     */
    dladdr((void *)lua_newstate, &info);
    dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);

    if ((pvt->Li = luaL_newstate()) == NULL) {
        DMM_FREE(pvt);
        return ENOMEM;
    }
    luaL_openlibs(pvt->Li);
    // Save node for future reference
    // Lua can access node id in 'node_id' global
    lua_pushinteger(pvt->Li, DMM_NODE_ID(node));
    lua_setglobal(pvt->Li, "node_id");
    lua_pushlightuserdata(pvt->Li, (void *)ctor);
    lua_pushlightuserdata(pvt->Li, node);
    lua_settable(pvt->Li, LUA_REGISTRYINDEX);

    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    lua_close(pvt->Li);
    pvt->~pvt_data();
    DMM_FREE(pvt);
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    lua_State *L;
    int err = 0;
    struct pvt_data *pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);

    if ((msg->cm_flags & DMM_MSG_RESP) != 0)
        return process_response(node, msg);

    if (msg->cm_type == DMM_MSGTYPE_GENERIC && msg->cm_cmd == DMM_MSG_STARTUP) {
        err = process_startup_message(node, DMM_MSG_DATA(msg, struct dmm_msg_startup));
    } else {
        // Pass to Lua
        L = lua_newthread(pvt->Li);
        lua_getglobal(L, "dmm");
        if (!lua_isnil(L, -1)) {
            lua_getfield(L, -1, "rcvmsg");
            if (!lua_isnil(L, -1)) {
                // Remove global dmm table from stack
                lua_remove(L, -2);
                lua_pushlightuserdata(L, msg);
                err = call_lua_code(node, L, 1, 1);
            } else {
                dmm_debug("dmm.rcvmsg function undefined for node " DMM_PRINODE, DMM_NODEINFO(node));
                lua_pop(L, 2);
                lua_pop(pvt->Li, 1);
                err = ENOTSUP;
            }
        } else {
            dmm_debug("dmm.rcvmsg function undefined for node " DMM_PRINODE, DMM_NODEINFO(node));
            lua_pop(L, 1);
            lua_pop(pvt->Li, 1);
            err = ENOTSUP;
        }
    }

    DMM_MSG_FREE(msg);
    return err;
}

/* No hooks are valid */
static int newhook(dmm_hook_p hook)
{
    (void) hook;
    return EINVAL;
}

static struct dmm_type type = {
    "luacontrol",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);
