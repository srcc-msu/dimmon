-- SPDX-License-Identifier: BSD-2-Clause-Views
-- Copyright (c) 2013-2023
--   Research Computing Center Lomonosov Moscow State University

-- dmm - module for interface to DiMMon

local dimmon_interfaces =  {"lib/dmm_message.i", "lib/dmm_sockevent_types.i"}

local bit = require('bit')
local dmm = {}

-- dmm.sfam - functions for dealing with structs
-- with flexible array member
dmm.sfam = {}

-- dmm.ll - low-level dmm functions
dmm.ll = require('dmm_ll')

local ffi = require('ffi')
local type_member_sizeof = {}
local cnt = 0

local function member_sizeof (structtype, member)
  local s_type = string.gsub(structtype, ' ', '_')
  local key = s_type .. ':' .. member
  local s = type_member_sizeof[key]
  if s then
    return s
  end
  local pref = '_lua_'..s_type..'_'..member:gsub('%.', '_')..'_'..cnt
  cnt = cnt + 1
  local sizeconst = pref..'_size'
  local c_code = 'static const int '..sizeconst..' = sizeof((('..structtype..'*)0)->'..member..'[0]);'
  ffi.cdef(c_code)
  s = ffi.C[sizeconst]
  type_member_sizeof[key] = s
  return s
end

function dmm.sfam.struct_sizeof(structtype, member, nelem)
  return ffi.sizeof(structtype) + nelem * member_sizeof(structtype, member)
end

function dmm.sfam.malloc(structtype, member, nelem)
  local s = dmm.sfam.struct_sizeof(structtype, member, nelem)
  local ptr = ffi.C.malloc(s)
  return ffi.cast(structtype.." *", ptr)
end

for _, i in ipairs(dimmon_interfaces) do
  local f = io.open(i)
  local s = f:read("*a")
  ffi.cdef(s)
end

local token = 1

--! @brief create new dmm control message
--! @param params.payload_type message payload type
--! @param params.len message payload length in bytes
--!                   default to sizeof(params.payload_type)
--! @param params.cmd message command
--! @param params.type message type
--! @param params.flags message flags
--! @param params.src source node ID. Defaults to current node ID
--! @return ffi pointer (cdata with pointer) to message and
--!         ffi pointer to message payload cast to params.payload_type *
--!         or void * if payload_type is not given
function dmm.msg_create(params)
  local len = assert(
                params.len or
                ffi.sizeof(params.payload_type)
              )
  local src = params.src or node_id
  local cmd = params.cmd or 0
  local type = params.type or 0
  local flags = params.flags or 0
  local msg = ffi.C.dmm_msg_create(src, cmd, type, token, flags, len)
  token = token + 1
  msg = ffi.cast('dmm_msg_p', msg)
  local payload_type_p
  if params.payload_type then
    payload_type_p =  params.payload_type..' *'
  else
    payload_type_p = 'void *'
  end
  payload_ptr = ffi.cast(payload_type_p, ffi.cast('char *', msg) + ffi.offsetof('struct dmm_msg', 'cm_data'))
  return msg, payload_ptr
end

function dmm.msg_free(msg)
  ffi.C.free(msg)
end

function dmm.msg_gc(msg)
  ffi.gc(msg, dmm.msg_free)
end

function dmm.msg_send(dst, msg)
  if (type(dst) ~= 'number') then
    error ('1st arg to dmm.msg_send should be a number', 2)
  elseif (type(msg) ~= 'cdata') then
    error ('2nd arg to dmm.msg_send should be a cdata', 2)
  end
  local lumsg = dmm.ll.cdata2ludata(msg)
  local resp = coroutine.yield(dst, lumsg)
  return ffi.cast('dmm_msg_p', dmm.ll.ludata2cdata(resp))
end

local dmm_msg_p_type = ffi.typeof('dmm_msg_p')

--! @brief if resp is an error response, issue error
--! @param resp cdata (dmm_msg_p) - response control message
--! @param message error message
--! @param level level of error position like in Lua error function
--! @return resp if it is a good (not an error) response
function dmm.assert_good_resp(resp, message, level)
  assert(ffi.typeof(resp) == dmm_msg_p_type)

  message = message or "Error in response to control message"
  if level and level > 0 then
    -- Increment level for this function additional level
    level = level + 1
  end
  -- Default level is where check_resp is called
  level = level or 2

  if bit.band(resp.cm_flags, ffi.C.DMM_MSG_ERR) ~= 0 then
    error(message, level)
  end

  return resp
end

local message_handlers = {}
local default_message_handler = nil

function dmm.set_message_handler(msg_type, msg_cmd, func, payload_type)
  if (type(func) ~= 'function') then
    error('3rd arg to dmm.set_message_handler should be a function', 2)
  end
  if (not message_handlers[msg_type]) then
    message_handlers[msg_type] = {}
  end
  message_handlers[msg_type][msg_cmd] = {f = func, t = ffi.typeof(payload_type..' *')}
end

function dmm.set_default_message_handler(func)
  if (type(func) ~= 'function') then
    error('Arg to dmm.set_default_message_handler should be a function', 2)
  end
  default_message_handler = func
end

function dmm.rcvmsg(msg)
  local func, payload_type
  local cmsg = ffi.cast('dmm_msg_p', msg)
  if (message_handlers[cmsg.cm_type]) then
    local t = message_handlers[cmsg.cm_type][cmsg.cm_cmd]
    if (t) then
      func = t.f
      payload_type = t.t
      return func(cmsg.cm_src, cmsg, ffi.cast(t.t, cmsg.cm_data))
    end
  end
  -- No message specific handler
  if (default_message_handler) then
    return default_message_handler(cmsg.cm_src, cmsg)
  end
  return nil
end

-- Function for specific control messages

function dmm.node_create(type)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_nodecreate',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_NODECREATE,
  }
  assert(string.len(type) < ffi.C.DMM_NODENAMESIZE - 1)
  p.type = type

  local resp = dmm.msg_send(node_id, msg)
  dmm.assert_good_resp(resp, "Cannot create node of type " .. type, 2)
  return resp.cm_src
end

function dmm.node_rm(id)
  local msg = dmm.msg_create{
    len = 0,
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_NODERM,
  }
  dmm.msg_send(id, msg)
end

function dmm.node_connect(srcnode, srchook, dstnode, dsthook)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_nodeconnect',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_NODECONNECT,
  }
  assert(string.len(srchook) <= ffi.C.DMM_HOOKNAMESIZE - 1)
  assert(string.len(dsthook) <= ffi.C.DMM_HOOKNAMESIZE - 1)
  p.srchook = srchook
  if (type(dstnode) == 'number') then
    p.dstnode = '['..tostring(dstnode)..']'
  else
    assert(string.len(dstnode) <= ffi.C.DMM_ADDRSIZE - 1)
    p.dstnode = dstnode
  end
  p.dsthook = dsthook
  dmm.msg_send(srcnode, msg)
end

function dmm.node_disconnect(srcnode, srchook, dstnode, dsthook)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_nodedisconnect',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_NODEDISCONNECT,
  }
  assert(string.len(srchook) <= ffi.C.DMM_HOOKNAMESIZE - 1)
  assert(string.len(dsthook) <= ffi.C.DMM_HOOKNAMESIZE - 1)
  p.srchook = srchook
  if (type(dstnode) == 'number') then
    p.dstnode = '['..tostring(dstnode)..']'
  else
    assert(string.len(dstnode) <= ffi.C.DMM_ADDRSIZE - 1)
    p.dstnode = dstnode
  end
  p.dsthook = dsthook
  dmm.msg_send(srcnode, msg)
end

--[[ Commented out as NODESETNAME is not implemented in DiMMon
function dmm.node_setname(node, name)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_nodesetname',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_NODESETNAME,
  }
  assert(string.len(name) <= ffi.C.DMM_NODENAMESIZE - 1)
  p.name = name
  dmm.msg_send(node, msg)
end
--]]

function dmm.timer_create()
  local msg = dmm.msg_create{
    len = 0,
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERCREATE,
  }
  local resp = dmm.msg_send(node_id,msg)
  dmm.assert_good_resp(resp, "Cannot create timer", 2)
  return ffi.cast('struct dmm_msg_timercreate_resp *', resp.cm_data).id
end

function dmm.timer_rm(timer_id)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_timerrm',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERRM,
  }
  p.id = timer_id
  dmm.msg_send(node_id,msg)
end

function dmm.timer_setperiodic(timer_id, period_sec, period_nsec)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_timerset',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERSET,
  }
  p.id = timer_id
  p.next = {0, 0}
  p.interval.tv_sec = period_sec
  p.interval.tv_nsec = period_nsec
  p.flags = 0
  dmm.msg_send(node_id, msg)
end

function dmm.timer_settimeout(timer_id, timeout_sec, timeout_nsec)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_timerset',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERSET,
  }
  p.id = timer_id
  p.next.tv_sec = timeout_sec
  p.next.tv_nsec = timeout_nsec
  p.interval = {0, 0}
  p.flags = 0
  dmm.msg_send(node_id, msg)
end

function dmm.timer_subscribe(node_id, timer_id)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_timersubscribe',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERSUBSCRIBE,
  }
  p.id = timer_id
  dmm.msg_send(node_id, msg)
end

function dmm.timer_unsubscribe(node_id, timer_id)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_timerunsubscribe',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_TIMERUNSUBSCRIBE,
  }
  p.id = timer_id
  dmm.msg_send(node_id, msg)
end

function dmm.sockevent_subscribe(node_id, fd, events)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_sockeventsubscribe',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_SOCKEVENTSUBSCRIBE,
  }
  p.fd = fd
  p.events = events
  dmm.msg_send(node_id, msg)
end

function dmm.sockevent_unsubscribe(node_id, fd)
  local msg, p = dmm.msg_create{
    payload_type = 'struct dmm_msg_sockeventunsubscribe',
    type = ffi.C.DMM_MSGTYPE_GENERIC,
    cmd = ffi.C.DMM_MSG_SOCKEVENTUNSUBSCRIBE,
  }
  p.fd = fd
  dmm.msg_send(node_id, msg)
end

--
-- Logging functions
--

-- Log level constants
dmm.LOG_EMERG   = ffi.C.DMM_LOG_EMERG
dmm.LOG_ALERT   = ffi.C.DMM_LOG_ALERT
dmm.LOG_CRIT    = ffi.C.DMM_LOG_CRIT
dmm.LOG_ERR     = ffi.C.DMM_LOG_ERR
dmm.LOG_WARN    = ffi.C.DMM_LOG_WARN
dmm.LOG_NOTICE  = ffi.C.DMM_LOG_NOTICE
dmm.LOG_INFO    = ffi.C.DMM_LOG_INFO
dmm.LOG_DEBUG   = ffi.C.DMM_LOG_DEBUG

if ffi.C.DMM_DEBUG_BUILD ~= 0 then
  dmm.debug = function (...)
    local s = ''
    for i, v in ipairs({...}) do
      s = s .. tostring(v)
    end
    local d = debug.getinfo(2, "nSl")
    s = s .. " in " .. d.short_src
    s = s .. " in " .. d.what .. " " .. d.namewhat
    s = s .. " function " .. (d.name or 'UNKNOWN')
    s = s .. " at line " .. d.currentline
    ffi.C.dmm_log(dmm.LOG_DEBUG, "%s", s)
  end
else
  dmm.debug = function (...) end
end

function dmm.log(priority, ...)
  local s = ''
  for i, v in ipairs({...}) do
    s = s .. tostring(v)
  end
  ffi.C.dmm_log(priority, "%s", s)
end

function dmm.emerg(...)
  local s = ''
  for i, v in ipairs({...}) do
    s = s .. tostring(v)
  end
  ffi.C.dmm_emerg("%s", s)
end

-- Path for low level interfaces, e.g. *.i file)
dmm.ll_ipath = ''
-- Path for high level interfaces, e.g. *.lua files
dmm.hl_ipath = ''

--! find first readable file for module in path
--! return (f, fname) pair
--! f is a file fname opened for reading
--! fname is a file name found
local function file_for_module(module, path)
  -- Change '/' to '_' to avoid subdir search
  module = module:gsub('/', '_')
  for w in path:gmatch('([^;]+)') do
    -- Change '?' to module name
    local fname = w:gsub('%?', function() return module end)
    local f = io.open(fname)
    if f then
      return f, fname
    end
  end
  return nil
end

local function require_ll_interface(module)
  local f, fname = file_for_module(module, dmm.ll_ipath)
  if f then
    local s = f:read('*a')
    ffi.cdef(s)
    f:close()
    return true
  end
  return nil
end

local function require_hl_interface(module)
  local f, fname = file_for_module(module, dmm.hl_ipath)
  if f then
    f:close()
    return dofile(fname)
  end
  return nil
end

function dmm.require_interface(module, type)
  if     type == 'hl' then
    return require_hl_interface(module)
  elseif type == 'll' then
    return require_ll_interface(module)
  elseif type ~= nil and type ~= '' then
    error (type .. " is not a valid interface type", 2)
  end
  local ret
  for i, t in ipairs{'hl', 'll'} do
    ret = dmm.require_interface(module, t)
    if ret then
      return ret
    end
  end
  return nil
end

-- Object oriented interface to DiMMon

-- Auxiliary functions

--! @brief get numerical node id for node
--! @param node value convertible to number or dmm.Module subtype
--! @return node id (a number) for node
function dmm.nodeid(node)
  return    tonumber(node)
         or tonumber(node.nodeid)
         or error("Not a node", 2)
end

--! @brief get numerical timer id for timer
--! @param timer value convertible to number of dmm.Timer
--! @return timer id (a number) for timer
function dmm.timerid(timer)
  return    tonumber(timer)
         or tonumber(timer.timerid)
         or error("Not a timer", 2)
end

local nodeid = dmm.nodeid
local timerid = dmm.timerid

-- Base class for interface to DiMMon modules
dmm.Module = {}
dmm.Module.__index = dmm.Module

--! @brief create class for a module
--! @param type type name for a module (is passed to dmm.node_create)
--! @param ctor additional contructor to call after node creation
--! @return Class for new module (subtype of dmm.Module)
function dmm.Module:new_type(type, ctor)
  local new_class = {}
  new_class.__index = new_class
  setmetatable(new_class, self)

  new_class.new = function(self, ...)
    local me = dmm.Module.new(self, type)
    if ctor then
      ctor(me, ...)
    end
    return me
  end

  return new_class
end

--! @brief Default contructor for a module
--! Can also be used as a constructor for a module which does not have
--! any specififc methods, e.g. dmm.Module:new('cpuload')
--! @param type type name for module (is passed to dmm.node_create)
function dmm.Module:new(type)
  local me = {}
  setmetatable(me, self)
  local status, res = pcall(dmm.node_create, type)
  if status then
    me.nodeid = res
  else
  -- Just rethrow the error
    error(res, 2)
  end
  return me
end

-- Create dmm.self node which points to self (using node_id global)
dmm.self = {}
setmetatable(dmm.self, dmm.Module)
dmm.self.nodeid = node_id

function dmm.Module:rm()
  assert(self.nodeid and self.nodeid > 0)
  dmm.node_rm(self.nodeid)
  self.nodeid = nil
end

function dmm.Module:connect(srchook, dstnode, dsthook)
  dmm.node_connect(self.nodeid, srchook, nodeid(dstnode), dsthook)
end

function dmm.Module:disconnect(srchook, dstnode, dsthook)
  dmm.node_disconnect(self.nodeid, srchook, nodeid(dstnode), dsthook)
end

function dmm.Module:sockevent_subscribe(fd, events)
  dmm.sockevent_subscribe(self.nodeid, fd, events)
end

function dmm.Module:sockevent_unsubscribe(fd)
  dmm.sockevent_unsubscribe(self.nodeid, fd)
end

function dmm.Module:timer_subscribe(timer)
  dmm.timer_subscribe(self.nodeid, timerid(timer))
end

function dmm.Module:timer_unsubscribe(timer)
  dmm.timer_unsubscribe(self.nodeid, timerid(timer))
end

dmm.Timer = {}
dmm.Timer.__index = dmm.Timer

function dmm.Timer:new()
  local me = {}
  setmetatable(me, self)
  me.timerid = dmm.timer_create()
  return me
end

function dmm.Timer:rm()
  assert(self.timerid and self.timerid > 0)
  dmm.timer_rm(self.timerid)
  self.timerid = nil
end

function dmm.Timer:setperiodic(period_sec, period_nsec)
  dmm.timer_setperiodic(self.timerid, period_sec, period_nsec)
end

function dmm.Timer:settimeout(timeout_sec, timeout_nsec)
  dmm.timer_settimeout(self.timerid, timeout_sec, timeout_nsec)
end

return dmm
