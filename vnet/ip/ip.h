/*
 * ip/ip.h: ip generic (4 or 6) main
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

#ifndef included_ip_main_h
#define included_ip_main_h

#include <clib/hash.h>
#include <clib/heap.h>		/* adjacency heap */

#include <vlib/vlib.h>

#include <vnet/ip/format.h>
#include <vnet/ip/ip_packet.h>
#include <vnet/ip/lookup.h>

#include <vnet/ip/tcp_packet.h>
#include <vnet/ip/udp_packet.h>
#include <vnet/ip/icmp46_packet.h>

#include <vnet/ip/ip4.h>
#include <vnet/ip/ip4_error.h>
#include <vnet/ip/ip4_packet.h>

#include <vnet/ip/ip6.h>
#include <vnet/ip/ip6_packet.h>
#include <vnet/ip/ip6_error.h>
#include <vnet/ip/icmp6.h>

/* Per protocol info. */
typedef struct {
  /* Protocol name (also used as hash key). */
  u8 * name;

  /* Protocol number. */
  ip_protocol_t protocol;

  /* Format function for this IP protocol. */
  format_function_t * format_header;

  /* Parser for header. */
  unformat_function_t * unformat_header;

  /* Parser for per-protocol matches. */
  unformat_function_t * unformat_match;

  /* Parser for packet generator edits for this protocol. */
  unformat_function_t * unformat_pg_edit;
} ip_protocol_info_t;

/* Per TCP/UDP port info. */
typedef struct {
  /* Port name (used as hash key). */
  u8 * name;

  /* UDP/TCP port number in network byte order. */
  u16 port;

  /* Port specific format function. */
  format_function_t * format_header;

  /* Parser for packet generator edits for this protocol. */
  unformat_function_t * unformat_pg_edit;
} tcp_udp_port_info_t;

typedef struct {
  /* Per IP protocol info. */
  ip_protocol_info_t * protocol_infos;

  /* Protocol info index hashed by 8 bit IP protocol. */
  uword * protocol_info_by_protocol;

  /* Hash table mapping IP protocol name (see protocols.def)
     to protocol number. */
  uword * protocol_info_by_name;

  /* Per TCP/UDP port info. */
  tcp_udp_port_info_t * port_infos;

  /* Hash table from network-byte-order port to port info index. */
  uword * port_info_by_port;

  /* Hash table mapping TCP/UDP name to port info index. */
  uword * port_info_by_name;
} ip_main_t;

extern ip_main_t ip_main;

clib_error_t *
ip_main_init (vlib_main_t * vm);

static inline ip_protocol_info_t *
ip_get_protocol_info (ip_main_t * im, u32 protocol)
{
  uword * p;

  p = hash_get (im->protocol_info_by_protocol, protocol);
  return p ? vec_elt_at_index (im->protocol_infos, p[0]) : 0;
}

static inline tcp_udp_port_info_t *
ip_get_tcp_udp_port_info (ip_main_t * im, u32 port)
{
  uword * p;

  p = hash_get (im->port_info_by_port, port);
  return p ? vec_elt_at_index (im->port_infos, p[0]) : 0;
}
      
/* VLIB buffer flags for ip4 packets.  Set by input interfaces for ip4/ip6
   tcp/udp packets with hardware computed checksums. */
#define LOG2_IP_BUFFER_L4_CHECKSUM_COMPUTED LOG2_VLIB_BUFFER_FLAG_USER1
#define LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT  LOG2_VLIB_BUFFER_FLAG_USER2
#define IP_BUFFER_L4_CHECKSUM_COMPUTED VLIB_BUFFER_FLAG_USER1
#define IP_BUFFER_L4_CHECKSUM_CORRECT  VLIB_BUFFER_FLAG_USER2

extern vlib_cli_command_t set_interface_ip_command;
extern vlib_cli_command_t vlib_cli_ip4_command, vlib_cli_show_ip4_command;
extern vlib_cli_command_t vlib_cli_ip6_command, vlib_cli_show_ip6_command;

#endif /* included_ip_main_h */
