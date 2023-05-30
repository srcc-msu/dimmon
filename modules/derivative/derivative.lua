-- SPDX-License-Identifier: BSD-2-Clause-Views
-- Copyright (c) 2013-2023
--   Research Computing Center Lomonosov Moscow State University

local dmm = require('dmm')

assert(dmm.require_interface('derivative', 'll'))

local Derivative = dmm.Module:new_type('derivative')

function Derivative:clear()
  local msg = dmm.msg_create{
    len = 0,
    type = ffi.C.DMM_MSGTYPE_DERIVATIVE,
    cmd = ffi.C.DMM_MSG_DERIVATIVE_CLEAR,
  }
  dmm.msg_send(self.nodeid, msg)
end

-- Auxiliary table in from [DERIVATIVE_CONST] = "C type string"
-- Used to fill type_format table which is really used
local output_type_formats = {
    [ffi.C.DERIVATIVE_INT32]  = "int32_t",
    [ffi.C.DERIVATIVE_UINT32] = "uint32_t",
    [ffi.C.DERIVATIVE_INT64]  = "int64_t",
    [ffi.C.DERIVATIVE_UINT64] = "uint64_t",
    [ffi.C.DERIVATIVE_FLOAT]  = "float",
    [ffi.C.DERIVATIVE_DOUBLE] = "double",
}

-- String => DERIVATIVE_CONST mapping to use in Derivative:set
local non_type_formats = {
  ["@none"] = ffi.C.DERIVATIVE_NONE,
}

-- tonumber(ffi.typeof(type)) => DERIVATIVE_CONST to use in Derivative:set
local type_formats = {}

for const, type in pairs(output_type_formats) do
  type_formats[tonumber(ffi.typeof(type))] = const
end

output_type_formats = nil

local function derivative_const(type)
  local dc
  if non_type_formats[type] then
    dc = non_type_formats[type]
  else
    dc = type_formats[tonumber(ffi.typeof(type))]
  end
  assert(dc, "Cannot find derivative type constant for type " .. type)
  return dc
end

--! @brief Sets the way derivative processes sensor data
--!
--! Derivative:set can be called as
--! set(src_id, type, monotonic, dst_id)
--! or set(src_id1, src_id2, type, monotonic, dst_id1)
--! The sensors_id to set are one sensor (4 argument call)
--! or sensor_id range from src_id1 to src_id2 (5 argument call)
--! type can be one of the predefined strings:
--! "@none" - skip the sensor
--! or type can be a name of C type like "int", "float",
--! "unit64_t" etc.
function Derivative:set(...)
  local numarg = select('#', ...)
  local msg, set
  if  numarg == 4 then
    local src_id, type, monotonic, dst_id = ...
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_derivative_set', 'descs', 1),
      payload_type = 'struct dmm_msg_derivative_set',
      type = ffi.C.DMM_MSGTYPE_DERIVATIVE,
      cmd = ffi.C.DMM_MSG_DERIVATIVE_SET,
    }
    set.descs[0] = {src_id, derivative_const(type), monotonic, dst_id}
  elseif numarg == 5 then
    local startsrcid, finishsrcid, type, monotonic, startdstid = ...
    local dc = derivative_const(type)
    msg, set = dmm.msg_create {
      len = dmm.sfam.struct_sizeof('struct dmm_msg_derivative_set', 'descs', finishsrcid - startsrcid + 1),
      payload_type = 'struct dmm_msg_derivative_set',
      type = ffi.C.DMM_MSGTYPE_DERIVATIVE,
      cmd = ffi.C.DMM_MSG_DERIVATIVE_SET,
    }
    dstid_off = startdstid - startsrcid
    for id = startsrcid, finishsrcid do
      set.descs[id - startsrcid] = {id, dc, monotonic, id + dstid_off}
    end
  else
    error("set accepts 4 or 5 arguments", 2)
  end
  dmm.msg_send(self.nodeid, msg)
end

return Derivative
