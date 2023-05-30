// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <assert.h>
#include <dlfcn.h>
#include <string.h>

#include "luajit-2.1/lauxlib.h"
#include "luajit-2.1/lualib.h"

static
int lua_iscdata(lua_State *L, int index)
{
    return strcmp(luaL_typename(L, index), "cdata") == 0;
}

/*
 * Makes *d = s
 * Needed to convert LuaJIT FFI cdata pointer to
 * Lua lightuserdata
 */
void copyptr(void *s, void **d) { *d = s; }

/*
 * Takes 1 argument: pointer cdata
 * Returns lightuserdata with that pointer
 */
static
int cdata2ludata(lua_State *L)
{
    void *temp;
    if (!lua_iscdata(L, 1))
        luaL_typerror(L, 1, "cdata");
    // Temp storage for converted pointer
    lua_pushlightuserdata(L, &temp);

    // Get compiled chunk
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    // Get ffi.C
    lua_pushvalue(L, lua_upvalueindex(1));

    lua_call(L, 3, 0);

    lua_pushlightuserdata(L,  temp);

    return 1;
}

/*
 * Just returns argument pointer
 * Needed to convert Lua lightuserdata to
 * LuaJIT FFI cdata<void *>
 */
void *returnptr(void *p) { return p; }

/*
 * Takes 1 argument: lightuserdata
 * Returns cdata<void *> with that pointer
 */
static
int ludata2cdata(lua_State *L)
{
    luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
    // Get compiled chunk
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    // Get ffi.C
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_call(L, 2, 1);
    return 1;
}

/*
static const struct luaL_Reg dmm_ll [] = {
    {NULL, NULL}
};
*/

static const char init_s[] = ""
    "local lib = ...\n"
    "local ffi = require('ffi')\n"
    "local l = ffi.load(lib)\n"
    "ffi.cdef('void copyptr(void *s, void **d)')\n"
    "ffi.cdef('void *returnptr(void *p)')\n"
    "return l\n"
    ;

/*
 * This code will be an anonymous function in upvalue(1)
 * of cdata2ludata
 */
static const char copyptr_s[] = ""
    "local s, d, ffi_ns = ...\n"
    "ffi_ns.copyptr(s, d)\n"
    ;
/*
 * This code will be an anonymous function in upvalue(1)
 * of ludata2cdata
 */
static const char returnptr_s[] = ""
    "local p, ffi_ns = ...\n"
    "return ffi_ns.returnptr(p)\n"
    ;

__attribute__ ((visibility ("default")))
int luaopen_dmm_ll(lua_State *L)
{
    Dl_info info;

    lua_newtable(L);
    assert(luaL_loadstring(L, init_s) == 0);

    dladdr((void *)luaopen_dmm_ll, &info);
    lua_pushstring(L, info.dli_fname);

    // init_s string get this library file name
    //  and returns ffi library namespace for this library
    assert(lua_pcall(L, 1, LUA_MULTRET, 0) == 0);
    // copy ffi_ns for second closure
    lua_pushvalue(L,  -1);
    assert(luaL_loadstring(L, copyptr_s) == 0);
    lua_pushcclosure(L, cdata2ludata, 2);
    lua_setfield(L, -3, "cdata2ludata");
    assert(luaL_loadstring(L, returnptr_s) == 0);
    lua_pushcclosure(L, ludata2cdata, 2);
    lua_setfield(L, -2, "ludata2cdata");

    return 1;
}
