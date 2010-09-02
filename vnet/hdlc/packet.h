#ifndef included_vnet_hdlc_packet_h
#define included_vnet_hdlc_packet_h

/*
 * HDLC packet format
 *
 * Copyright (c) 2009 Eliot Dresselhaus
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

#define foreach_hdlc_protocol			\
  _ (0x0800, ip4)				\
  _ (0x8035, slarp)				\
  _ (0x8847, mpls_unicast)			\
  _ (0x8848, mpls_multicast)			\
  _ (0x86DD, ip6)

typedef enum {
#define _(n,f) HDLC_PROTOCOL_##f = n,
  foreach_hdlc_protocol
#undef _
} hdlc_protocol_t;

typedef struct {
  /* Set to 0x0f for unicast; 0x8f for broadcast. */
  u8 address;

  /* Always zero. */
  u8 control;

  /* Layer 3 protocol for this packet. */
  u16 protocol;
} hdlc_header_t;

#endif /* included_vnet_hdlc_packet_h */
