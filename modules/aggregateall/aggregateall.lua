-- SPDX-License-Identifier: BSD-2-Clause-Views
-- Copyright (c) 2013-2023
--   Research Computing Center Lomonosov Moscow State University

local dmm = require('dmm')

assert(dmm.require_interface('aggregateall', 'll'))

local Aggregateall = dmm.Module:new_type('aggregateall')

function Aggregateall:clear()
  local msg = dmm.msg_create{
    len = 0,
    type = ffi.C.DMM_MSGTYPE_AGGREGATEALL,
    cmd = ffi.C.DMM_MSG_AGGREGATEALL_CLEAR,
  }
  dmm.msg_send(self.nodeid, msg)
end

-- Auxiliary table in from [AGGREGATEALL_CONST] = "C type string"
-- Used to fill type_format table which is really used
local output_type_formats = {
    [ffi.C.AGGREGATEALL_INT32]  = "int32_t",
    [ffi.C.AGGREGATEALL_UINT32] = "uint32_t",
    [ffi.C.AGGREGATEALL_INT64]  = "int64_t",
    [ffi.C.AGGREGATEALL_UINT64] = "uint64_t",
    [ffi.C.AGGREGATEALL_FLOAT]  = "float",
    [ffi.C.AGGREGATEALL_DOUBLE] = "double",
}

-- String => AGGREGATEALL_CONST mapping to use in Aggregateall:set
local non_type_formats = {
  ["@none"] = ffi.C.AGGREGATEALL_NONE,
}

-- tonumber(ffi.typeof(type)) => AGGREGATEALL_CONST to use in Aggregateall:set
local type_formats = {}

for const, type in pairs(output_type_formats) do
  type_formats[tonumber(ffi.typeof(type))] = const
end

output_type_formats = nil

local function aggregateall_const(type)
  local ac
  if non_type_formats[type] then
    ac = non_type_formats[type]
  else
    ac = type_formats[tonumber(ffi.typeof(type))]
  end
  assert(ac, "Cannot find aggregateall type constant for type " .. type)
  return ac
end

--! @brief Sets the way aggregateall processes sensor data
--!
--! Aggregateall:set can be called as
--! set(src_id, type, dst_id)
--! or set(src_id1, src_id2, type, dst_id1)
--! The sensors_id to set are one sensor (3 argument call)
--! or sensor_id range from src_id1 to src_id2 (4 argument call)
--! type can be one of the predefined strings:
--! "@none" - skip the sensor
--! or type can be a name of C type like "int", "float",
--! "unit64_t" etc.
function Aggregateall:set(...)
  local numarg = select('#', ...)
  local msg, set
  if  numarg == 3 then
    local src_id, type, dst_id = ...
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_aggregateall_set', 'descs', 1),
      payload_type = 'struct dmm_msg_aggregateall_set',
      type = ffi.C.DMM_MSGTYPE_AGGREGATEALL,
      cmd = ffi.C.DMM_MSG_AGGREGATEALL_SET,
    }
    set.descs[0] = {src_id, agregateall_const(type), dst_id}
  elseif numarg == 4 then
    local startsrcid, finishsrcid, type, startdstid = ...
    local ac = aggregateall_const(type)
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_aggregateall_set', 'descs', finishsrcid - startsrcid + 1),
      payload_type = 'struct dmm_msg_aggregateall_set',
      type = ffi.C.DMM_MSGTYPE_AGGREGATEALL,
      cmd = ffi.C.DMM_MSG_AGGREGATEALL_SET,
    }
    dstid_off = startdstid - startsrcid
    for id = startsrcid, finishsrcid do
      set.descs[id - startsrcid] = {id, ac, id + dstid_off}
    end
  else
    error("set accepts 3 or 4 arguments", 2)
  end
  dmm.msg_send(self.nodeid, msg)
end

return Aggregateall
