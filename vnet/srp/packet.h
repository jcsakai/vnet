/*
 * srp/packet.h: srp packet format.
 *
 * Copyright (c) 2008 Eliot Dresselhaus
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef included_srp_packet_h
#define included_srp_packet_h

#include <clib/byte_order.h>
#include <vnet/ethernet/packet.h>

/* SRP version 2. */

#define foreach_srp_packet_mode			\
  _ (reserved0)					\
  _ (reserved1)					\
  _ (reserved2)					\
  _ (reserved3)					\
  _ (control_pass_to_host)			\
  _ (control_locally_buffered_for_host)		\
  _ (keep_alive)				\
  _ (data)

typedef enum {
#define _(f) SRP_MODE_##f,
  foreach_srp_packet_mode
#undef _
  SRP_N_MODE,
} srp_mode_t;

typedef union {
  /* For computing parity bit. */
  u16 as_u16;

  struct {
    u8 ttl;

#if CLIB_ARCH_IS_BIG_ENDIAN
    u8 is_inner_ring : 1;
    u8 mode : 3;
    u8 priority : 3;
    u8 parity : 1;
#endif
#if CLIB_ARCH_IS_LITTLE_ENDIAN
    u8 parity : 1;
    u8 priority : 3;
    u8 mode : 3;
    u8 is_inner_ring : 1;
#endif
  };
} srp_header_t;

#define foreach_srp_control_packet_type		\
  _ (reserved)					\
  _ (topology)					\
  _ (ips)

typedef enum {
#define _(f) SRP_CONTROL_PACKET_TYPE_##f,
  foreach_srp_control_packet_type
#undef _
} srp_control_packet_type_t;

typedef struct {
  /* Set to 0. */
  u8 version;

  srp_control_packet_type_t type : 8;

  /* IP4-like checksum of packet starting with start of control header. */
  u16 checksum;

  u16 ttl;
} srp_control_header_t;

typedef struct {
  u8 type;
  u8 unused;
  /* MAC address. */
  u8 address[6];
} srp_topology_mac_binding_t;

typedef struct {
  srp_header_t srp;
  ethernet_header_t ethernet;
  srp_control_header_t control;

  /* Length in bytes of mac binding data. */
  u16 n_bytes_of_mac_bindings;

  /* MAC address of originator of this topology request. */
  u8 originator_address[6];

  /* Bindings follow. */
  srp_topology_mac_binding_t bindings[0];
} srp_topology_header_t;

typedef struct {
  srp_header_t srp;
  ethernet_header_t ethernet;
  srp_control_header_t control;
  u8 originator_address[6];
  u8 ips_octet;
  u8 reserved;
} srp_ips_header_t;

#endif /* included_srp_packet_h */
