-- SPDX-License-Identifier: BSD-2-Clause-Views
-- Copyright (c) 2013-2023
--   Research Computing Center Lomonosov Moscow State University

local dmm = require('dmm')

assert(dmm.require_interface('dbgprinter', 'll'))

local Dbgprinter = dmm.Module:new_type('dbgprinter')

function Dbgprinter:clear()
  local msg = dmm.msg_create{
    len = 0,
    type = ffi.C.DMM_MSGTYPE_DBGPRINTER,
    cmd = ffi.C.DMM_MSG_DBGPRINTER_CLEAR,
  }
  dmm.msg_send(self.nodeid, msg)
end

-- Auxiliary table in from [DBGPRINTER_CONST] = "C type string"
-- Used to fill type_format table which is really used
local output_type_formats = {
    [ffi.C.DBGPRINTER_CHAR]   = "char",
    [ffi.C.DBGPRINTER_INT32]  = "int32_t",
    [ffi.C.DBGPRINTER_UINT32] = "uint32_t",
    [ffi.C.DBGPRINTER_INT64]  = "int64_t",
    [ffi.C.DBGPRINTER_UINT64] = "uint64_t",
    [ffi.C.DBGPRINTER_FLOAT]  = "float",
    [ffi.C.DBGPRINTER_DOUBLE] = "double",
}

-- String => DBGPRINTER_CONST mapping to use in Dbgprinter:set
local non_type_formats = {
  ["@string"]  = ffi.C.DBGPRINTER_STRING,
  ["@none"]    = ffi.C.DBGPRINTER_NONE,
  ["@hexdump"] = ffi.C.DBGPRINTER_HEXDUMP,
  ["@default"] = ffi.C.DBGPRINTER_DEFAULT,
}

-- tonumber(ffi.typeof(type)) => DBGPRINTER_CONST to use in Dbgprinter:set
local type_formats = {}

for const, type in pairs(output_type_formats) do
  type_formats[tonumber(ffi.typeof(type))] = const
end

output_type_formats = nil

local function dbgprinter_const(type)
  local dc
  if non_type_formats[type] then
    dc = non_type_formats[type]
  else
    dc = type_formats[tonumber(ffi.typeof(type))]
  end
  assert(dc, "Cannot find dbgprinter type constant for type " .. type)
  return dc
end

--! @brief Sets the way dbgprinter prints sensor data
--!
--! Dbgprinter:set can be called as
--! set(id, type) or set(id1, id2, type)
--! The sensors_id to set are one sensor (2 argument call)
--! or sensor_id range from id1 to id2 (3 argument call)
--! type can be one of the predefined strings:
--! "@string"  - print as a string
--! "@hexdump" - print hexdump of sensor data
--! "@default" - print a default representation
--! "@none"    - skip the sensor (do not print anything)
--! or type can be a name of C type like "int", "float",
--! "unit64_t" etc.
function Dbgprinter:set(...)
  local numarg = select('#', ...)
  local msg, set
  if  numarg == 2 then
    local id, type = ...
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_dbgprinter_set', 'descs', 1),
      payload_type = 'struct dmm_msg_dbgprinter_set',
      type = ffi.C.DMM_MSGTYPE_DBGPRINTER,
      cmd = ffi.C.DMM_MSG_DBGPRINTER_SET,
    }
    set.descs[0] = {id, dbgprinter_const(type)}
  elseif numarg == 3 then
    local startid, finishid, type = ...
    local dc = dbgprinter_const(type)
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_dbgprinter_set', 'descs', finishid - startid + 1),
      payload_type = 'struct dmm_msg_dbgprinter_set',
      type = ffi.C.DMM_MSGTYPE_DBGPRINTER,
      cmd = ffi.C.DMM_MSG_DBGPRINTER_SET,
    }
    for id = startid, finishid do
      set.descs[id - startid] = {id, dc}
    end
  else
    error("set accepts 2 or 3 arguments", 2)
  end
  dmm.msg_send(self.nodeid, msg)
end

return Dbgprinter
