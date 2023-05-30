-- SPDX-License-Identifier: BSD-2-Clause-Views
-- Copyright (c) 2013-2023
--   Research Computing Center Lomonosov Moscow State University

local dmm = require('dmm')

assert(dmm.require_interface('net/ip', 'll'))

local net_ip = {}

local S = require("posix.sys.socket")

net_ip.AF_INET = S.AF_INET
net_ip.AF_INET6 = S.AF_INET6
net_ip.SOCK_DGRAM = S.SOCK_DGRAM

net_ip.Send = dmm.Module:new_type('net/ip/send')
net_ip.Recv = dmm.Module:new_type('net/ip/recv')

function net_ip.Send:createsock(domain, type,protocol)
  local msg, createsock = dmm.msg_create {
    payload_type = 'struct dmm_msg_netip_createsock',
    type = ffi.C.DMM_MSGTYPE_NETIPSEND,
    cmd = ffi.C.DMM_MSG_NETIPSEND_CREATESOCK,
  }
  createsock.domain = domain
  createsock.type = type
  createsock.protocol = protocol
  dmm.msg_send(self.nodeid, msg)
end

function net_ip.Send:connect(addr)
  assert(#addr < ffi.C.DMM_NETIP_MAXADDRLEN)
  local msg, conn = dmm.msg_create {
    payload_type = 'struct dmm_msg_netipsend_connect',
    type = ffi.C.DMM_MSGTYPE_NETIPSEND,
    cmd = ffi.C.DMM_MSG_NETIPSEND_CONNECT,
  }
  conn.addr = addr
  dmm.msg_send(self.nodeid, msg)
end

function net_ip.Recv:createsock(domain, type, protocol)
  local msg, createsock = dmm.msg_create {
    payload_type = 'struct dmm_msg_netip_createsock',
    type = ffi.C.DMM_MSGTYPE_NETIPRECV,
    cmd = ffi.C.DMM_MSG_NETIPRECV_CREATESOCK,
  }
  createsock.domain = domain
  createsock.type = type
  createsock.protocol = protocol
  dmm.msg_send(self.nodeid, msg)
end

function net_ip.Recv:bind(addr)
  assert(#addr < ffi.C.DMM_NETIP_MAXADDRLEN)
  local msg, conn = dmm.msg_create {
    payload_type = 'struct dmm_msg_netiprecv_bind',
    type = ffi.C.DMM_MSGTYPE_NETIPRECV,
    cmd = ffi.C.DMM_MSG_NETIPRECV_BIND,
  }
  conn.addr = addr
  dmm.msg_send(self.nodeid, msg)
end

function net_ip.Recv:setflags(flags)
  local msg, conn = dmm.msg_create {
    payload_type = 'struct dmm_msg_netiprecv_setflags',
    type = ffi.C.DMM_MSGTYPE_NETIPRECV,
    cmd = ffi.C.DMM_MSG_NETIPRECV_SETFLAGS,
  }
  conn.flags = flags
  dmm.msg_send(self.nodeid, msg)
end

function net_ip.Recv:getflags()
  local msg, conn = dmm.msg_create {
    len = 0,
    type = ffi.C.DMM_MSGTYPE_NETIPRECV,
    cmd = ffi.C.DMM_MSG_NETIPRECV_GETFLAGS,
  }
  local resp = dmm.msg_send(self.nodeid, msg)
  dmm.assert_good_resp(resp, "Cannot get flags of node " .. self.nodeid)
  return ffi.cast('struct dmm_msg_netiprecv_getflags_resp *', resp.cm_data).flags
end

return net_ip
