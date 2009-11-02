/*
 * ethernet/packet.h: ethernet packet format.
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

#ifndef included_ethernet_packet_h
#define included_ethernet_packet_h

typedef enum {
#define ethernet_type(n,s) ETHERNET_TYPE_##s = n,
#include <vnet/ethernet/types.def>
#undef ethernet_type
} ethernet_type_t;

typedef struct {
  /* Source/destination address. */
  u8 dst_address[6];
  u8 src_address[6];

  /* Ethernet type. */
  u16 type;
} ethernet_header_t;

/* For VLAN ethernet type. */
typedef struct {
  /* 3 bit priority, 1 bit CFI and 12 bit vlan id. */
  u16 priority_cfi_and_id;

#define ETHERNET_N_VLAN (1 << 12)

  /* Inner ethernet type. */
  u16 type;
} ethernet_vlan_header_t;

#endif /* included_ethernet_packet_h */