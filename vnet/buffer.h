/*
 * vnet/buffer.h: vnet buffer flags
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

#ifndef included_vnet_buffer_h
#define included_vnet_buffer_h

#include <vlib/vlib.h>

/* VLIB buffer flags for ip4/ip6 packets.  Set by input interfaces for ip4/ip6
   tcp/udp packets with hardware computed checksums. */
#define LOG2_IP_BUFFER_L4_CHECKSUM_COMPUTED LOG2_VLIB_BUFFER_FLAG_USER(1)
#define LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT  LOG2_VLIB_BUFFER_FLAG_USER(2)
#define IP_BUFFER_L4_CHECKSUM_COMPUTED (1 << LOG2_IP_BUFFER_L4_CHECKSUM_COMPUTED)
#define IP_BUFFER_L4_CHECKSUM_CORRECT  (1 << LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT)

typedef struct {
  /* RX/TX software interface for this packet. */
  u32 sw_if_index[VLIB_N_RX_TX];

  union {
    /* Ethernet. */
    struct {
      /* Saved value of current header by ethernet-input. */
      u32 start_of_ethernet_header;
    } ethernet;

    /* IP4/6 buffer opaque. */
    struct {
      /* Adjacency from destination IP address lookup [VLIB_TX].
	 Adjacency from source IP address lookup [VLIB_RX].
	 This gets set to ~0 until source lookup is performed. */
      u32 adj_index[VLIB_N_RX_TX];

      union {
	struct {
	  /* Flow hash value for this packet computed from IP src/dst address
	     protocol and ports. */
	  u32 flow_hash;

	  /* Current configuration index. */
	  u32 current_config_index;
	};

	/* Alternate used for local TCP packets. */
	struct {
	  u32 listener_index;

	  u32 established_connection_index;

	  u32 mini_connection_index;
	} tcp;
      };
    } ip;

    u32 unused[6];
  };
} vnet_buffer_opaque_t;

#define vnet_buffer(b) ((vnet_buffer_opaque_t *) (b)->opaque)

#endif /* included_vnet_buffer_h */
