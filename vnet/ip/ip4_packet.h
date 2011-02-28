/*
 * ip4/packet.h: ip4 packet format
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

#ifndef included_ip4_packet_h
#define included_ip4_packet_h

#include <vnet/ip/ip_packet.h>	/* for ip_csum_t */
#include <vnet/ip/tcp_packet.h>	/* for tcp_header_t */
#include <clib/byte_order.h>	/* for clib_net_to_host_u16 */

/* IP4 address which can be accessed either as 4 bytes
   or as a 32-bit number. */
typedef union {
  u8 data[4];
  u32 data_u32;
  /* Aliases. */
  u8 as_u8[4];
  u32 as_u32;
} ip4_address_t;

/* (src,dst) pair of addresses as found in packet header. */
typedef struct {
  ip4_address_t src, dst;
} ip4_address_pair_t;

typedef union {
  struct {
    /* 4 bit packet length (in 32bit units) and version VVVVLLLL.
       e.g. for packets w/ no options ip_version_and_header_length == 0x45. */
    u8 ip_version_and_header_length;

    /* Type of service. */
    u8 tos;

    /* Total layer 3 packet length including this header. */
    u16 length;

    /* Fragmentation ID. */
    u16 fragment_id;

    /* 3 bits of flags and 13 bits of fragment offset (in units
       of 8 byte quantities). */
    u16 flags_and_fragment_offset;
#define IP4_HEADER_FLAG_MORE_FRAGMENTS (1 << 13)
#define IP4_HEADER_FLAG_DONT_FRAGMENT (1 << 14)
#define IP4_HEADER_FLAG_CONGESTION (1 << 15)

    /* Time to live decremented by router at each hop. */
    u8 ttl;

    /* Next level protocol packet. */
    u8 protocol;

    /* Checksum. */
    u16 checksum;

    /* Source and destination address. */
    union {
      struct {
	ip4_address_t src_address, dst_address;
      };
      ip4_address_pair_t address_pair;
    };      
  };

  /* For checksumming we'll want to access IP header in word sized chunks. */
  /* For 64 bit machines. */
  PACKED (struct {
    u64 checksum_data_64[2];
    u32 checksum_data_64_32[1];
  });

  /* For 32 bit machines. */
  PACKED (struct {
    u32 checksum_data_32[5];
  });
} ip4_header_t;

/* Value of ip_version_and_header_length for packets w/o options. */
#define IP4_VERSION_AND_HEADER_LENGTH_NO_OPTIONS \
  ((4 << 4) | (sizeof (ip4_header_t) / sizeof (u32)))

always_inline int
ip4_get_fragment_offset (ip4_header_t * i)
{ return clib_net_to_host_u16 (i->flags_and_fragment_offset) & 0x1fff; } 

/* Fragment offset in bytes. */
always_inline int
ip4_get_fragment_offset_bytes (ip4_header_t * i)
{ return 8 * ip4_get_fragment_offset (i); }

always_inline int
ip4_header_bytes (ip4_header_t * i)
{ return sizeof (u32) * (i->ip_version_and_header_length & 0xf); }

always_inline void *
ip4_next_header (ip4_header_t * i)
{ return (void *) i + ip4_header_bytes (i); }

static inline u16
ip4_header_checksum (ip4_header_t * i)
{
  u16 save, csum;
  ip_csum_t sum;

  save = i->checksum;
  i->checksum = 0;
  sum = ip_incremental_checksum (0, i, ip4_header_bytes (i));
  csum = ~ip_csum_fold (sum);

  i->checksum = save;

  /* Make checksum agree for special case where either
     0 or 0xffff would give same 1s complement sum. */
  if (csum == 0 && save == 0xffff)
    csum = save;

  return csum;
}

static inline uword
ip4_header_checksum_is_valid (ip4_header_t * i)
{ return i->checksum == ip4_header_checksum (i); }

#define ip4_partial_header_checksum_x1(ip0,sum0)			\
do {									\
  if (BITS (ip_csum_t) > 32)						\
    {									\
      sum0 = ip0->checksum_data_64[0];					\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_64[1]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_64_32[0]);	\
    }									\
  else									\
    {									\
      sum0 = ip0->checksum_data_32[0];					\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[1]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[2]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[3]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[4]);	\
    }									\
} while (0)

#define ip4_partial_header_checksum_x2(ip0,ip1,sum0,sum1)		\
do {									\
  if (BITS (ip_csum_t) > 32)						\
    {									\
      sum0 = ip0->checksum_data_64[0];					\
      sum1 = ip1->checksum_data_64[0];					\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_64[1]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_64[1]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_64_32[0]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_64_32[0]);	\
    }									\
  else									\
    {									\
      sum0 = ip0->checksum_data_32[0];					\
      sum1 = ip1->checksum_data_32[0];					\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[1]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_32[1]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[2]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_32[2]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[3]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_32[3]);	\
      sum0 = ip_csum_with_carry (sum0, ip0->checksum_data_32[4]);	\
      sum1 = ip_csum_with_carry (sum1, ip1->checksum_data_32[4]);	\
    }									\
} while (0)

always_inline uword
ip4_address_is_multicast (ip4_address_t * a)
{ return (a->data[0] & 0xf0) == 0xe0; }

always_inline void
ip4_multicast_address_set_for_group (ip4_address_t * a, ip_multicast_group_t g)
{
  ASSERT (g < (1 << 28));
  a->as_u32 = clib_host_to_net_u32 ((0xe << 28) + g);
}

always_inline void
ip4_tcp_reply_x1 (ip4_header_t * ip0, tcp_header_t * tcp0)
{
  u32 src0, dst0;

  src0 = ip0->src_address.data_u32;
  dst0 = ip0->dst_address.data_u32;
  ip0->src_address.data_u32 = dst0;
  ip0->dst_address.data_u32 = src0;

  src0 = tcp0->ports.src;
  dst0 = tcp0->ports.dst;
  tcp0->ports.src = dst0;
  tcp0->ports.dst = src0;
}

always_inline void
ip4_tcp_reply_x2 (ip4_header_t * ip0, ip4_header_t * ip1,
		  tcp_header_t * tcp0, tcp_header_t * tcp1)
{
  u32 src0, dst0, src1, dst1;

  src0 = ip0->src_address.data_u32;
  src1 = ip1->src_address.data_u32;
  dst0 = ip0->dst_address.data_u32;
  dst1 = ip1->dst_address.data_u32;
  ip0->src_address.data_u32 = dst0;
  ip1->src_address.data_u32 = dst1;
  ip0->dst_address.data_u32 = src0;
  ip1->dst_address.data_u32 = src1;

  src0 = tcp0->ports.src;
  src1 = tcp1->ports.src;
  dst0 = tcp0->ports.dst;
  dst1 = tcp1->ports.dst;
  tcp0->ports.src = dst0;
  tcp1->ports.src = dst1;
  tcp0->ports.dst = src0;
  tcp1->ports.dst = src1;
}

#endif /* included_ip4_packet_h */
