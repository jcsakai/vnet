/*
 * gnet/packet.h: gnet packet format.
 *
 * Copyright (c) 2012 Eliot Dresselhaus
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

#ifndef included_gnet_packet_h
#define included_gnet_packet_h

#include <vnet/ethernet/packet.h>

/* Gnet addresses are of the form (x0,x1,x2,x3) 4D torus.
   Each X has 6 bits.  So we support racks up to 64 x 64 nodes.
   And 64 x 64 racks.  16M nodes total.
   as_u8[0] = [5:0] x0 [7:6] x3 bits [5:4]
   as_u8[1] = [5:0] x1 [7:6] x3 bits [3:2]
   as_u8[2] = [5:0] x2 [7:6] x3 bits [1:0]. */
typedef union {
  struct { u8 as_u8[3]; };

  /* To save a cycle comparing. */
  struct { u8 cmp_u8; u16 cmp_u16; };
} gnet_address_t;

#define GNET_ADDRESS_N_DIMENSION 4
#define GNET_ADDRESS_N_BITS_PER_DIMENSION 6
#define GNET_ADDRESS_N_PER_DIMENSION (1 << GNET_ADDRESS_N_BITS_PER_DIMENSION)

always_inline uword
gnet_address_is_equal (gnet_address_t * a, gnet_address_t * b)
{ return a->cmp_u8 == b->cmp_u8 && a->cmp_u16 == b->cmp_u16; }

always_inline u32
gnet_address_get (gnet_address_t * a, u32 i)
{
  ASSERT (i < GNET_ADDRESS_N_DIMENSION);
  if (i < 3)
    return a->as_u8[i] & 0x3f;
  return ((a->as_u8[0] & 0xc0) >> 2) | ((a->as_u8[1] & 0xc0) >> 4) | (a->as_u8[1] >> 6);
}

/* 2 & 3 coordinates as 12bit number. */
always_inline u32
gnet_address_get_23 (gnet_address_t * a)
{
  return a->as_u8[2] | ((a->as_u8[1] & 0xc0) << 2) | ((a->as_u8[0] & 0xc0) << 4);
}

always_inline void
gnet_address_set (gnet_address_t * a, u32 x0, u32 x1, u32 x2, u32 x3)
{
  ASSERT (x0 < GNET_ADDRESS_N_PER_DIMENSION);
  ASSERT (x1 < GNET_ADDRESS_N_PER_DIMENSION);
  ASSERT (x2 < GNET_ADDRESS_N_PER_DIMENSION);
  ASSERT (x3 < GNET_ADDRESS_N_PER_DIMENSION);
  a->as_u8[0] = (x0 & 0x3f) | ((x3 & (3 << 4)) << 2);
  a->as_u8[1] = (x1 & 0x3f) | ((x3 & (3 << 2)) << 4);
  a->as_u8[2] = (x2 & 0x3f) | ((x3 & (3 << 0)) << 6);
}

/* Gnet packets look very very much like ethernet since they are send
   over networks with ethernet controllers. */
typedef CLIB_PACKED (struct {
#if CLIB_ARCH_IS_BIG_ENDIAN
  u8 flow_hash_bit : 5;
  u8 is_broadcast : 1;
  u8 is_control : 1;
  u8 is_group : 1;
#endif
#if CLIB_ARCH_IS_LITTLE_ENDIAN
  u8 is_group : 1;
  u8 is_control : 1;
  u8 is_broadcast : 1;
  u8 flow_hash_bit : 5;
#endif

  /* (x0,x1,x2,x3) coordinates of destination. 6 bits each. */
  gnet_address_t dst_address;

  /* [11:0] destination linux network namespace.
     [31:12] multicast group (only valid if is_broadcast == 0 and i/g == group).
     [31:12] rewrite index for packets destined to gateways
     (gnet header will be re-written with real ethernet header by gateway). */
  u32 dst_namespace_and_multicast_group_or_rewrite_index;

  /* Flow hash for this packet.  Used to load balance across multiple equal cost grid paths. */
  u32 flow_hash;

  /* Packet type (same as type in Ethernet header). */
  u16 type;
}) gnet_header_t;

#define foreach_gnet_control_packet_type	\
  _ (invalid)					\
  _ (read_reg_32)				\
  _ (write_reg_32)				\
  _ (update_reg_32)

typedef enum {
#define _(f) GNET_CONTROL_PACKET_TYPE_##f,
  foreach_gnet_control_packet_type
#undef _
  GNET_N_CONTROL_PACKET_TYPE,
} gnet_control_packet_type_t;

typedef CLIB_PACKED (struct {
  gnet_control_packet_type_t type : 8;

  /* Source of this control packet. */
  gnet_address_t src_address;

  /* Register address to read/write or that just changed. */
  u32 reg_address;

  /* Data returned for read operation; data to be written for write operation.
     Mask of changed bits and new value for update operation. */
  u32 data[2];
}) gnet_control_header_t;

/* Registers for gnet hardware (router, gateway, switch). */
typedef volatile struct {
  struct {
    u32 version;

    /* 0 => router/interconnect mode (interconnects (x0,x1) plane with (x2,x3) plane.
       1 => gateway mode (interconnects (x0,x1) plane with external aggregation switch (5 x 40g => 2 x 100g switch)).
       2 => switch mode.
       3 => mac mode (4 x 10g gnet mac).  (x0,x1) plane gnet mac. */
    u32 mode;

    /* [3:0] number of router modules this rack. */
    u32 config;

    u32 reset;
    CLIB_PAD_FROM_TO (0x8, 0x100);

    /* [3:0] link state (1 up, 0 down) for 4 links n s e w. */
    u32 link_state;

    /* (x0,x1,x2,x3) for our station.  Used to calculate delta[i] and thereby
       forward packets.  Let delta[i] = dst_address_from_packet[i] - my_address[i].
       delta[3] != 0 || delta[2] != 0 => packet for another rack.  Flow hash chooses router address.
         Don't rotate flow hash so next hop will choose the same router address.
	 Router address => new delta[1] & delta[0].
       If delta[3] == 0 && delta[2] == 0 then
         delta[1] != 0 => +/north and/or -/south links ok for packet (if they are link up).
	 delta[0] != 0 => +/east and/or -/west links ok for packet (if they are link up).
       If delta[0] == 0 && delta[1] == 0 => packet is for us.
	 else if 2 link bits set? 1 flow hash bits => path; rotate flow hash.
	 else if 1 link bits set? no flow hash bits => take only possible path.
	 else set re-route bit and send back where it came from. (?) */
    u32 my_address;

    /* (x0,x1) address of router modules for this rack.
       Flow hash upper 6 bits from packet header chooses router address.
       Only (x0, x1) address matters here. */
    u32 router_address_this_rack[64];
  } main;
  
  struct {
    struct {
      /* lookup tcam. */
      struct {
	u32 dst_address_value[256];
	u32 dst_address_mask[256];

	/* [15:0] result ram base index.
	   [24:16] number of bits of flow hash.
	   result index = base_index + flow_hash_bits + (dst_address_from_packet &~ mask) << n_flow_hash_bits. */
	u32 results[256];
      } lookup;

      /* Jenkins hash A/B/C for flow hash for incoming packets. */
      u32 flow_hash_seeds[3];

      /* (x3,x2,x1,x0) address of destination. */
      u32 result_ram[32 << 10];
    } ip4_rx;

    /* Analagous for ip6. */
    struct {
    } ip6_rx;

    /* for each of 2 100g uplinks, 48 bit ethernet source address for packets we send. */
    u32 tx_src_mac_address[2][2];

    /* adjacency table: dst mac address for packets we send. */
    u32 tx_dst_mac_address_table[4 << 10][2];

    /* Lots of counters. */
  } xswitch;

} gnet_hw_regs_t;

#endif /* included_gnet_packet_h */
