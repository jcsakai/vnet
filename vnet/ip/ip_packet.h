/*
 * ip/ip_packet.h: packet format common between ip4 & ip6
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

#ifndef included_ip_packet_h
#define included_ip_packet_h

#include <clib/clib.h>

typedef enum ip_protocol {
#define ip_protocol(n,s) IP_PROTOCOL_##s = n,
#include "protocols.def"
#undef ip_protocol
} ip_protocol_t;

/* TCP/UDP ports. */
typedef enum {
#define ip_port(s,n) IP_PORT_##s = n,
#include "ports.def"
#undef ip_port
} ip_port_t;

/* IP checksum support. */

/* Incremental checksum update. */
typedef u64 ip_csum_t;

static always_inline u64
ip_csum_with_carry (u64 sum, u64 x)
{
  u64 t = sum + x;
  return t + (t < x);
}

/* Update checksum changing field at even byte offset from 0 -> x. */
static always_inline ip_csum_t
ip_csum_add_even (ip_csum_t c, ip_csum_t x)
{
  ip_csum_t d;

  d = c - x;

  /* Fold in carry from high bit. */
  d -= d > c;

  return d;
}

/* Update checksum changing field at even byte offset from x -> 0. */
static always_inline ip_csum_t
ip_csum_sub_even (ip_csum_t c, ip_csum_t x)
{ return ip_csum_with_carry (c, x); }

static always_inline u16 ip_csum_fold (ip_csum_t c)
{
  /* Reduce to 16 bits. */
  c = (c & (u64) 0xffffffff) + (c >> (u64) 32);

  c = (c & 0xffff) + (c >> 16);
  c = (c & 0xffff) + (c >> 16);
  c = (c & 0xffff) + (c >> 16);

  ASSERT ((c >> 16) == 0);
  return c;
}

/* Checksum routine. */
ip_csum_t ip_incremental_checksum (ip_csum_t sum, void * data, uword n_bytes);

#endif /* included_ip_packet_h */