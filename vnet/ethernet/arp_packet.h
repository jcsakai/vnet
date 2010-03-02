/*
 * ethernet/arp.c: IP v4 ARP node
 *
 * Copyright (c) 2010 Cisco Systems
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

#ifndef included_ethernet_arp_packet_h
#define included_ethernet_arp_packet_h

#define foreach_ethernet_arp_hardware_type	\
  _ (0, reserved)				\
  _ (1, ethernet)				\
  _ (2, experimental_ethernet)			\
  _ (3, ax_25)					\
  _ (4, proteon_pronet_token_ring)		\
  _ (5, chaos)					\
  _ (6, ieee_802)				\
  _ (7, arcnet)					\
  _ (8, hyperchannel)				\
  _ (9, lanstar)				\
  _ (10, autonet)				\
  _ (11, localtalk)				\
  _ (12, localnet)				\
  _ (13, ultra_link)				\
  _ (14, smds)					\
  _ (15, frame_relay)				\
  _ (16, atm)					\
  _ (17, hdlc)					\
  _ (18, fibre_channel)				\
  _ (19, atm19)					\
  _ (20, serial_line)				\
  _ (21, atm21)					\
  _ (22, mil_std_188_220)			\
  _ (23, metricom)				\
  _ (24, ieee_1394)				\
  _ (25, mapos)					\
  _ (26, twinaxial)				\
  _ (27, eui_64)				\
  _ (28, hiparp)				\
  _ (29, iso_7816_3)				\
  _ (30, arpsec)				\
  _ (31, ipsec_tunnel)				\
  _ (32, infiniband)				\
  _ (33, cai)					\
  _ (34, wiegand)				\
  _ (35, pure_ip)				\
  _ (36, hw_exp1)				\
  _ (256, hw_exp2)

#define foreach_ethernet_arp_opcode		\
  _ (reserved)					\
  _ (request)					\
  _ (reply)					\
  _ (reverse_request)				\
  _ (reverse_reply)				\
  _ (drarp_request)				\
  _ (drarp_reply)				\
  _ (drarp_error)				\
  _ (inarp_request)				\
  _ (inarp_reply)				\
  _ (arp_nak)					\
  _ (mars_request)				\
  _ (mars_multi)				\
  _ (mars_mserv)				\
  _ (mars_join)					\
  _ (mars_leave)				\
  _ (mars_nak)					\
  _ (mars_unserv)				\
  _ (mars_sjoin)				\
  _ (mars_sleave)				\
  _ (mars_grouplist_request)			\
  _ (mars_grouplist_reply)			\
  _ (mars_redirect_map)				\
  _ (mapos_unarp)				\
  _ (exp1)					\
  _ (exp2)

typedef enum {
#define _(n,f) ETHERNET_ARP_HARDWARE_TYPE_##f = (n),
  foreach_ethernet_arp_hardware_type
#undef _
} ethernet_arp_hardware_type_t;

typedef enum {
#define _(f) ETHERNET_ARP_OPCODE_##f,
  foreach_ethernet_arp_opcode
#undef _
  ETHERNET_ARP_N_OPCODE,
} ethernet_arp_opcode_t;

typedef PACKED (struct {
  u8 ethernet[6];
  ip4_address_t ip4;
}) ethernet_arp_ip4_over_ethernet_address_t;

typedef struct {
  u16 l2_type;
  u16 l3_type;
  u8 n_l2_address_bytes;
  u8 n_l3_address_bytes;
  u16 opcode;
  union {
    ethernet_arp_ip4_over_ethernet_address_t ip4_over_ethernet[2];

    /* Others... */
    u8 data[0];
  };
} ethernet_arp_header_t;

typedef PACKED (struct {
  ethernet_header_t ethernet;
  ethernet_arp_header_t arp;
}) ethernet_and_arp_header_t;

#endif /* included_ethernet_arp_packet_h */
