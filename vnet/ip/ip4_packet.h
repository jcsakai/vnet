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
    u8 src_address[4];
    u8 dst_address[4];
  };

  /* For checksumming we'll want to access IP header as 2 64 bit
     numbers followed by a 32 bit number. */
  PACKED (struct {
    u64 data64[2];
    u32 data32[1];
  });
} ip4_header_t;

/* Value of ip_version_and_header_length for packets w/o options. */
#define IP4_VERSION_AND_HEADER_LENGTH_NO_OPTIONS \
  ((4 << 4) | (sizeof (ip4_header_t) / sizeof (u32)))

static always_inline int
ip4_get_fragment_offset (ip4_header_t * i)
{ return clib_net_to_host_u16 (i->flags_and_fragment_offset) & 0x1fff; } 

/* Fragment offset in bytes. */
static always_inline int
ip4_get_fragment_offset_bytes (ip4_header_t * i)
{ return 8 * ip4_get_fragment_offset (i); }

static always_inline int
ip4_header_bytes (ip4_header_t * i)
{ return sizeof (u32) * (i->ip_version_and_header_length & 0xf); }

static always_inline void *
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

  return csum;
}

static inline uword
ip4_header_checksum_is_valid (ip4_header_t * i)
{ return i->checksum == ip4_header_checksum (i); }

/* VLIB buffer flags for ip4 packets.  Set by input interfaces for ip4
   tcp/udp packets with hardware computed checksums. */
#define LOG2_IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED LOG2_VLIB_BUFFER_FLAG_USER1
#define LOG2_IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT  LOG2_VLIB__BUFFER_FLAG_USER2
#define IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED VLIB_BUFFER_FLAG_USER1
#define IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT  VLIB_BUFFER_FLAG_USER2

#endif /* included_ip4_packet_h */
