/*
 * ip/tcp.c: tcp protocol
 *
 * Copyright (c) 2011 Eliot Dresselhaus
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

#include <vnet/ip/ip.h>
#include <math.h>

typedef union {
  struct {
    u16 src, dst;
  };
  u32 as_u32;
} tcp_udp_ports_t;

typedef struct {
  union {
    struct {
      ip4_address_t src, dst;
    };
    u64 as_u64;
  } addresses;
  tcp_udp_ports_t ports;
} ip4_tcp_udp_address_t;

always_inline void
ip4_tcp_udp_address_from_header2 (ip4_tcp_udp_address_t * a,
				  ip4_header_t * ip,
				  tcp_header_t * tcp)
{
  a->addresses.as_u64 = clib_mem_unaligned (&ip->src_address, u64);
  a->ports.as_u32 = clib_mem_unaligned (&tcp->ports.src_and_dst, u32);
}

always_inline void
ip4_tcp_udp_address_from_header (ip4_tcp_udp_address_t * a,
				 ip4_header_t * ip)
{
  tcp_header_t * tcp = ip4_next_header (ip);
  ip4_tcp_udp_address_from_header2 (a, ip, tcp);
}

always_inline uword
ip4_tcp_udp_address_is_equal (ip4_tcp_udp_address_t * a0,
			      ip4_tcp_udp_address_t * a1)
{
  return (a0->addresses.as_u64 == a1->addresses.as_u64
	  && a0->ports.as_u32 == a1->ports.as_u32);
}

always_inline uword
ip4_tcp_udp_address_is_equal_to_header (ip4_tcp_udp_address_t * a,
					ip4_header_t * ip,
					tcp_header_t * tcp)
{
  return (a->addresses.as_u64 == clib_mem_unaligned (&ip->src_address, u64)
	  && a->ports.as_u32 == clib_mem_unaligned (&tcp->ports.src_and_dst, u32));
}

static u8 * format_ip4_tcp_udp_address (u8 * s, va_list * args)
{
  ip4_tcp_udp_address_t * a = va_arg (*args, ip4_tcp_udp_address_t *);

  s = format (s, "%U:%d -> %U:%d",
	      format_ip4_address, a->addresses.src,
	      clib_net_to_host_u16 (a->ports.src),
	      format_ip4_address, a->addresses.dst,
	      clib_net_to_host_u16 (a->ports.dst));

  return s;
}

typedef struct {
  union {
    struct {
      ip6_address_t src, dst;
    };
    u64 as_u64[4];
  } addresses;
  tcp_udp_ports_t ports;
} ip6_tcp_udp_address_t;

always_inline void
ip6_tcp_udp_address_from_header2 (ip6_tcp_udp_address_t * a,
				  ip6_header_t * ip,
				  tcp_header_t * tcp)
{
  a->addresses.as_u64[0] = clib_mem_unaligned (&ip->src_address.as_u64[0], u64);
  a->addresses.as_u64[1] = clib_mem_unaligned (&ip->src_address.as_u64[1], u64);
  a->addresses.as_u64[2] = clib_mem_unaligned (&ip->src_address.as_u64[2], u64);
  a->addresses.as_u64[3] = clib_mem_unaligned (&ip->src_address.as_u64[3], u64);
  a->ports.as_u32 = clib_mem_unaligned (&tcp->ports.src_and_dst, u32);
}

always_inline void
ip6_tcp_udp_address_from_header (ip6_tcp_udp_address_t * a,
				 ip6_header_t * ip)
{
  tcp_header_t * tcp = ip6_next_header (ip);
  ip6_tcp_udp_address_from_header2 (a, ip, tcp);
}

always_inline uword
ip6_tcp_udp_address_is_equal (ip6_tcp_udp_address_t * a0,
			      ip6_tcp_udp_address_t * a1)
{
  return (a0->addresses.as_u64 == a1->addresses.as_u64
	  && a0->ports.as_u32 == a1->ports.as_u32);
}

always_inline uword
ip6_tcp_udp_address_is_equal_to_header (ip6_tcp_udp_address_t * a,
					ip6_header_t * ip,
					tcp_header_t * tcp)
{
  return (a->addresses.as_u64[0] == clib_mem_unaligned (&ip->src_address.as_u64[0], u64)
	  && a->addresses.as_u64[1] == clib_mem_unaligned (&ip->src_address.as_u64[1], u64)
	  && a->addresses.as_u64[2] == clib_mem_unaligned (&ip->src_address.as_u64[2], u64)
	  && a->addresses.as_u64[3] == clib_mem_unaligned (&ip->src_address.as_u64[3], u64)
	  && a->ports.as_u32 == clib_mem_unaligned (&tcp->ports.src_and_dst, u32));
}

static u8 * format_ip6_tcp_udp_address (u8 * s, va_list * args)
{
  ip6_tcp_udp_address_t * a = va_arg (*args, ip6_tcp_udp_address_t *);

  s = format (s, "%U:%d -> %U:%d",
	      format_ip6_address, &a->addresses.src,
	      clib_net_to_host_u16 (a->ports.src),
	      format_ip6_address, &a->addresses.dst,
	      clib_net_to_host_u16 (a->ports.dst));

  return s;
}

static u8 my_zero_mask_table[256] = {
  [0xf0] = (1 << 1),
  [0x0f] = (1 << 0),
  [0xff] = (1 << 0) | (1 << 1),
};

always_inline u32 my_zero_mask (u32 x)
{
  return ((my_zero_mask_table[(x >> 0) & 0xff] << 0)
	  | (my_zero_mask_table[(x >> 8) & 0xff] << 2));
}

static u8 my_first_set_table[256] = {
  [0x00] = 4,
  [0xf0] = 1,
  [0x0f] = 0,
  [0xff] = 0,
};

always_inline u32 my_first_set (u32 zero_mask)
{
  u8 r0 = my_first_set_table[(zero_mask >> 0) & 0xff];
  u8 r1 = 2 + my_first_set_table[(zero_mask >> 8) & 0xff];
  return r0 != 4 ? r0 : r1;
}

typedef union {
  u32x4 as_u32x4;
  tcp_udp_ports_t as_ports[4];
} tcp_udp_ports_x4_t;

typedef struct {
  union {
    u32x4 as_u32x4;
    ip4_address_t as_ip4_address[4];
  } src, dst;
  tcp_udp_ports_x4_t ports;
} ip4_tcp_udp_address_x4_t;

always_inline void
ip4_tcp_udp_address_x4_set_from_headers (ip4_tcp_udp_address_x4_t * a,
					 ip4_header_t * ip,
					 tcp_header_t * tcp,
					 u32 i)
{
  a->src.as_ip4_address[i] = ip->src_address;
  a->dst.as_ip4_address[i] = ip->dst_address;
  a->ports.as_ports[i].as_u32 = tcp->ports.src_and_dst;
}

always_inline void
ip4_tcp_udp_address_x4_copy_and_invalidate (ip4_tcp_udp_address_x4_t * dst,
					    ip4_tcp_udp_address_x4_t * src,
					    u32 dst_i, u32 src_i)
{
#define _(d,s) d = s; s = 0;
  _ (dst->src.as_ip4_address[dst_i].as_u32, src->src.as_ip4_address[src_i].as_u32);
  _ (dst->dst.as_ip4_address[dst_i].as_u32, src->dst.as_ip4_address[src_i].as_u32);
  _ (dst->ports.as_ports[dst_i].as_u32, src->ports.as_ports[src_i].as_u32);
#undef _
}

always_inline void
ip4_tcp_udp_address_x4_invalidate (ip4_tcp_udp_address_x4_t * a, u32 i)
{
  a->src.as_ip4_address[i].as_u32 = 0;
  a->dst.as_ip4_address[i].as_u32 = 0;
  a->ports.as_ports[i].as_u32 = 0;
}

always_inline uword
ip4_tcp_udp_address_x4_match_helper (ip4_tcp_udp_address_x4_t * ax4,
				     u32x4 src, u32x4 dst, u32x4 ports)
{
  u32x4 r;
  u32 m;

  r = u32x4_is_equal (src, ax4->src.as_u32x4);
  r &= u32x4_is_equal (dst, ax4->dst.as_u32x4);
  r &= u32x4_is_equal (ports, ax4->ports.as_u32x4);

  /* At this point r will be either all zeros (if nothing matched)
     or have 32 1s in the position that did match. */
  m = u8x16_compare_byte_mask ((u8x16) r);

  return m;
}

always_inline uword
ip4_tcp_udp_address_x4_match (ip4_tcp_udp_address_x4_t * ax4,
			      ip4_header_t * ip,
			      tcp_header_t * tcp)
{
  u32x4 src = u32x4_splat (ip->src_address.as_u32);
  u32x4 dst = u32x4_splat (ip->dst_address.as_u32);
  u32x4 ports = u32x4_splat (tcp->ports.src_and_dst);
  return my_first_set (ip4_tcp_udp_address_x4_match_helper (ax4, src, dst, ports));
}

always_inline uword
ip4_tcp_udp_address_x4_first_empty (ip4_tcp_udp_address_x4_t * ax4)
{
  u32x4 zero = {0};
  return my_first_set (ip4_tcp_udp_address_x4_match_helper (ax4, zero, zero, zero));
}

always_inline uword
ip4_tcp_udp_address_x4_empty_mask (ip4_tcp_udp_address_x4_t * ax4)
{
  u32x4 zero = {0};
  return my_zero_mask (ip4_tcp_udp_address_x4_match_helper (ax4, zero, zero, zero));
}

typedef struct {
  union {
    u32x4 as_u32x4[4];
    u32   as_u32[4][4];
  } src, dst;
  tcp_udp_ports_x4_t ports;
} ip6_tcp_udp_address_x4_t;

always_inline void
ip6_tcp_udp_address_x4_set_from_headers (ip6_tcp_udp_address_x4_t * a,
					 ip6_header_t * ip,
					 tcp_header_t * tcp,
					 u32 i)
{
  a->src.as_u32[0][i] = ip->src_address.as_u32[0];
  a->src.as_u32[1][i] = ip->src_address.as_u32[1];
  a->src.as_u32[2][i] = ip->src_address.as_u32[2];
  a->src.as_u32[3][i] = ip->src_address.as_u32[3];
  a->dst.as_u32[0][i] = ip->dst_address.as_u32[0];
  a->dst.as_u32[1][i] = ip->dst_address.as_u32[1];
  a->dst.as_u32[2][i] = ip->dst_address.as_u32[2];
  a->dst.as_u32[3][i] = ip->dst_address.as_u32[3];
  a->ports.as_ports[i].as_u32 = tcp->ports.src_and_dst;
}

always_inline void
ip6_tcp_udp_address_x4_copy_and_invalidate (ip6_tcp_udp_address_x4_t * dst,
					    ip6_tcp_udp_address_x4_t * src,
					    u32 dst_i, u32 src_i)
{
#define _(d,s) d = s; s = 0;
  _ (dst->src.as_u32[0][dst_i], src->src.as_u32[0][src_i]);
  _ (dst->src.as_u32[1][dst_i], src->src.as_u32[1][src_i]);
  _ (dst->src.as_u32[2][dst_i], src->src.as_u32[2][src_i]);
  _ (dst->src.as_u32[3][dst_i], src->src.as_u32[3][src_i]);
  _ (dst->dst.as_u32[0][dst_i], src->dst.as_u32[0][src_i]);
  _ (dst->dst.as_u32[1][dst_i], src->dst.as_u32[1][src_i]);
  _ (dst->dst.as_u32[2][dst_i], src->dst.as_u32[2][src_i]);
  _ (dst->dst.as_u32[3][dst_i], src->dst.as_u32[3][src_i]);
  _ (dst->ports.as_ports[dst_i].as_u32, src->ports.as_ports[src_i].as_u32);
#undef _
}

always_inline void
ip6_tcp_udp_address_x4_invalidate (ip6_tcp_udp_address_x4_t * a,
				   u32 i)
{
  a->src.as_u32[0][i] = 0;
  a->src.as_u32[1][i] = 0;
  a->src.as_u32[2][i] = 0;
  a->src.as_u32[3][i] = 0;
  a->dst.as_u32[0][i] = 0;
  a->dst.as_u32[1][i] = 0;
  a->dst.as_u32[2][i] = 0;
  a->dst.as_u32[3][i] = 0;
  a->ports.as_ports[i].as_u32 = 0;
}

always_inline uword
ip6_tcp_udp_address_x4_match_helper (ip6_tcp_udp_address_x4_t * ax4,
				     u32x4 src0, u32x4 src1, u32x4 src2, u32x4 src3,
				     u32x4 dst0, u32x4 dst1, u32x4 dst2, u32x4 dst3,
				     u32x4 ports)
{
  u32x4 r;
  u32 m;

  r = u32x4_is_equal (src0, ax4->src.as_u32x4[0]);
  r &= u32x4_is_equal (src1, ax4->src.as_u32x4[1]);
  r &= u32x4_is_equal (src2, ax4->src.as_u32x4[2]);
  r &= u32x4_is_equal (src3, ax4->src.as_u32x4[3]);
  r &= u32x4_is_equal (dst0, ax4->dst.as_u32x4[0]);
  r &= u32x4_is_equal (dst1, ax4->dst.as_u32x4[1]);
  r &= u32x4_is_equal (dst2, ax4->dst.as_u32x4[2]);
  r &= u32x4_is_equal (dst3, ax4->dst.as_u32x4[3]);
  r &= u32x4_is_equal (ports, ax4->ports.as_u32x4);

  /* At this point r will be either all zeros (if nothing matched)
     or have 32 1s in the position that did match. */
  m = u8x16_compare_byte_mask ((u8x16) r);

  return m;
}

always_inline uword
ip6_tcp_udp_address_x4_match (ip6_tcp_udp_address_x4_t * ax4,
			      ip6_header_t * ip,
			      tcp_header_t * tcp)
{
  u32x4 src0 = u32x4_splat (ip->src_address.as_u32[0]);
  u32x4 src1 = u32x4_splat (ip->src_address.as_u32[1]);
  u32x4 src2 = u32x4_splat (ip->src_address.as_u32[2]);
  u32x4 src3 = u32x4_splat (ip->src_address.as_u32[3]);
  u32x4 dst0 = u32x4_splat (ip->dst_address.as_u32[0]);
  u32x4 dst1 = u32x4_splat (ip->dst_address.as_u32[1]);
  u32x4 dst2 = u32x4_splat (ip->dst_address.as_u32[2]);
  u32x4 dst3 = u32x4_splat (ip->dst_address.as_u32[3]);
  u32x4 ports = u32x4_splat (tcp->ports.src_and_dst);
  return my_first_set (ip6_tcp_udp_address_x4_match_helper (ax4,
							    src0, src1, src2, src3,
							    dst0, dst1, dst2, dst3,
							    ports));
}

always_inline uword
ip6_tcp_udp_address_x4_first_empty (ip6_tcp_udp_address_x4_t * ax4)
{
  u32x4 zero = {0};
  return my_first_set (ip6_tcp_udp_address_x4_match_helper (ax4,
							    zero, zero, zero, zero,
							    zero, zero, zero, zero,
							    zero));
}

always_inline uword
ip6_tcp_udp_address_x4_empty_mask (ip6_tcp_udp_address_x4_t * ax4)
{
  u32x4 zero = {0};
  return my_zero_mask (ip6_tcp_udp_address_x4_match_helper (ax4,
							    zero, zero, zero, zero,
							    zero, zero, zero, zero,
							    zero));
}

#define foreach_tcp_timer			\
  /* Used to rank mini connections. */		\
  _ (mini_connection, 10e-3)			\
  /* Used for timestamps. */			\
  _ (timestamp, 1e-6)

typedef enum {
#define _(f,s) TCP_TIMER_##f,
  foreach_tcp_timer
#undef _
  TCP_N_TIMER,
} tcp_timer_type_t;

always_inline u32
find_oldest_timestamp_x4 (u32 * time_stamps, u32 now)
{
  u32 dt0, dt_min0, i_min0;
  u32 dt1, dt_min1, i_min1;

  i_min0 = i_min1 = 0;
  dt_min0 = now - time_stamps[0];
  dt_min1 = now - time_stamps[2];
  dt0 = now - time_stamps[1];
  dt1 = now - time_stamps[3];

  i_min0 += dt0 > dt_min0;
  i_min1 += dt1 > dt_min1;

  dt_min0 = i_min0 > 0 ? dt0 : dt_min0;
  dt_min1 = i_min1 > 0 ? dt1 : dt_min1;

  return dt_min0 > dt_min1 ? i_min0 : (2 + i_min1);
}

typedef struct {
  ip4_tcp_udp_address_x4_t address_x4;
  u32 time_stamps[4];
} ip4_tcp_udp_address_x4_and_timestamps_t;

typedef struct {
  ip6_tcp_udp_address_x4_t address_x4;
  u32 time_stamps[4];
} ip6_tcp_udp_address_x4_and_timestamps_t;

#define foreach_tcp_connection_state					\
  /* unused */								\
  _ (unused)								\
  /* Sent SYN-ACK waiting for ACK if he ever feels like sending one. */	\
  _ (listen_ack_wait)							\
  /* Sent SYN waiting for ACK or RST. */				\
  _ (connecting)							\
  /* Sent FIN after he sent FIN waiting for final ACK. */		\
  _ (closing)								\
  /* Pseudo-type for established connections. */			\
  _ (established)

typedef enum {
#define _(f) TCP_CONNECTION_STATE_##f,
  foreach_tcp_connection_state
#undef _
  TCP_N_CONNECTION_STATE,
} tcp_connection_state_t;

typedef struct {
  u32 his, ours;
} tcp_sequence_pair_t;

/* Kept small to fight off syn flood attacks. */
typedef struct {
  tcp_sequence_pair_t sequence_numbers;

  /* Time stamps saved from options. */
  u32 my_time_stamp, his_time_stamp;

  /* segment size and window scale (saved from options
     or set to defaults). */
  u16 max_segment_size;

  u8 window_scale;

  tcp_connection_state_t state : 8;
} tcp_mini_connection_t;

typedef struct {
  tcp_sequence_pair_t sequence_numbers;

  /* segment size and window scale (saved from options
     or set to defaults). */
  u16 max_segment_size;

  /* Window from latest received packet. */
  u16 his_window;

  u16 my_window;

  u8 his_window_scale;

  u8 my_window_scale;

  /* Number of un-acknowledged bytes we've sent. */
  u32 n_tx_unacked_bytes;

  u32 tx_head_buffer_index;

  u32 tx_tail_buffer_index;

  u32 his_time_stamp, my_time_stamp;

  struct {
    f64 sum, sum2;
    f64 count;
  } round_trip_time_stats;

  u32 listener_opaque;
} tcp_established_connection_t;

typedef struct {
  u8 log2_n_mini_connection_hash_elts;
  u8 log2_n_established_connection_hash_elts;
  u8 is_ip6;

  u32 mini_connection_hash_mask;
  u32 established_connection_hash_mask;

  uword * establed_connection_overflow_hash;

  tcp_mini_connection_t * mini_connections;

  tcp_established_connection_t * established_connections;

  /* Default valid_local_adjacency_bitmap for listeners who want to listen
     for a given port in on all interfaces. */
  uword * default_valid_local_adjacency_bitmap;
} ip46_tcp_main_t;

typedef enum {
  /* Received a SYN-ACK after sending a SYN to connect. */
  TCP_LISTEN_CONNECTION_ESTABLISHED,
  /* Received a reset (RST) after sending a SYN to connect. */
  TCP_LISTEN_CONNECT_FAILED,
  /* Received a end-of-file (FIN) from an established connection. */ 
  TCP_LISTEN_EOF,
  /* Received a reset RST from an established connection. */
  TCP_LISTEN_RESET,
} tcp_listen_event_type_t;

typedef struct tcp_udp_listener_t {
  /* Bitmap indicating which of local (interface) addresses
     we should listen on for this destination port. */
  uword * valid_local_adjacency_bitmap;

  /* Destination tcp/udp port to listen for connections. */
  u16 dst_port;

  u16 next_index;

  u32 flags;
#define TCP_LISTENER_FLAG_ENABLE_IP4 (1 << 0)
#define TCP_LISTENER_FLAG_ENABLE_IP6 (1 << 1)

  /* Buffers for which event in event_function applies to. */
  u32 * event_data;

  void (* event_function) (struct tcp_udp_listener_t * l,
			   tcp_listen_event_type_t event_type);
} tcp_listener_t;

typedef struct {
  u8 next, error;
} tcp_lookup_disposition_t;

typedef struct {
  ip46_tcp_main_t ip4, ip6;

  /* Array of non-established connections, but soon-to be established connections. */
  ip4_tcp_udp_address_x4_and_timestamps_t * ip4_mini_connection_address_hash;
  ip6_tcp_udp_address_x4_and_timestamps_t * ip6_mini_connection_address_hash;

  /* Vector of size log2_n_established_connection_hash_elts plus overflow. */
  ip4_tcp_udp_address_x4_t * ip4_established_connection_address_hash;
  ip6_tcp_udp_address_x4_t * ip6_established_connection_address_hash;

  /* Jenkins hash seeds for various hash tables. */
  u32x4_union_t connection_hash_seeds[2][3];
  u32x4_union_t connection_hash_masks[2];

  /* Pool of listeners. */
  tcp_listener_t * listener_pool;

  /* Table mapping destination port to listener index. */
  u16 * listener_index_by_dst_port;

  tcp_lookup_disposition_t disposition_by_state_and_flags[TCP_N_CONNECTION_STATE][64];

  u8 log2_clocks_per_tick[TCP_N_TIMER];

  f64 secs_per_tick[TCP_N_TIMER];

  /* Holds pointers to default and per-packet TCP options while
     parsing a TCP packet's options. */
  tcp_mini_connection_t option_decode_mini_connection_template;
} tcp_main_t;

always_inline u32
tcp_time_now (tcp_main_t * tm, tcp_timer_type_t t)
{
  ASSERT (t < ARRAY_LEN (tm->log2_clocks_per_tick));
  return clib_cpu_time_now () >> tm->log2_clocks_per_tick[t];
}

static void
tcp_time_init (vlib_main_t * vm, tcp_main_t * tm)
{
  int i;
  f64 log2 = .69314718055994530941;

  for (i = 0; i < ARRAY_LEN (tm->log2_clocks_per_tick); i++)
    {
      static f64 t[] = {
#define _(f,r) r,
	foreach_tcp_timer
#undef _
      };
      tm->log2_clocks_per_tick[i] =
	flt_round_nearest (log (t[i] / vm->clib_time.seconds_per_clock) / log2);
      tm->secs_per_tick[i] = vm->clib_time.seconds_per_clock * (1 << tm->log2_clocks_per_tick[i]);
    }
}

tcp_main_t tcp_main;

typedef enum {
  TCP_LOOKUP_NEXT_DROP,
  TCP_LOOKUP_NEXT_PUNT,
  TCP_LOOKUP_NEXT_LISTEN_SYN,
  TCP_LOOKUP_NEXT_LISTEN_ACK,
  TCP_LOOKUP_NEXT_CONNECT_SYN_ACK,
  TCP_LOOKUP_NEXT_ESTABLISHED,
  TCP_LOOKUP_N_NEXT,
} tcp_lookup_next_t;

typedef struct {
  /* Adjacency from src address lookup.  We'll use this to avoid
     having to perform lookup again for replies. */
  u32 src_adj_index;

  u32 listener_index;

  u32 established_connection_index;

  u32 mini_connection_index;
} tcp_udp_lookup_buffer_opaque_t;

#define foreach_tcp_error						\
  _ (NONE, "no error")							\
  _ (LOOKUP_DROPS, "lookup drops")					\
  _ (LISTEN_RESPONSES, "listen responses sent")				\
  _ (CONNECTS_SENT, "connects sent")					\
  _ (LISTENS_ESTABLISHED, "listens connected")				\
  _ (CONNECTS_ESTABLISHED, "connects established")			\
  _ (NO_LISTENER_FOR_PORT, "no listener for port")			\
  _ (WRONG_LOCAL_ADDRESS_FOR_PORT, "wrong local address for port")	\
  _ (NO_DATA, "valid packets with no data")

typedef enum {
#define _(sym,str) TCP_ERROR_##sym,
  foreach_tcp_error
#undef _
  TCP_N_ERROR,
} tcp_error_t;

always_inline u32x4 u32x4_splat_x2 (u32 x)
{
  u32x4 r = u32x4_set0 (x);
  return u32x4_interleave_lo (r, r);
}

always_inline u32x4 u32x4_set_x2 (u32 x, u32 y)
{
  u32x4 r0 = u32x4_set0 (x);
  u32x4 r1 = u32x4_set0 (y);
  return u32x4_interleave_lo (r0, r1);
}

#define u32x4_get(x,i)					\
  __builtin_ia32_vec_ext_v4si ((i32x4) (x), (int) (i))

/* Dispatching on tcp/udp listeners (by dst port)
   and tcp/udp connections (by src/dst address/port). */
always_inline uword
ip46_tcp_lookup (vlib_main_t * vm,
		 vlib_node_runtime_t * node,
		 vlib_frame_t * frame,
		 uword is_ip6)
{
  ip_lookup_main_t * im = &ip4_main.lookup_main;
  tcp_main_t * tm = &tcp_main;
  ip46_tcp_main_t * tm46 = is_ip6 ? &tm->ip6 : &tm->ip4;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next, mini_now;
  vlib_node_runtime_t * error_node = node;

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  mini_now = tcp_time_now (tm, TCP_TIMER_mini_connection);
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip_adjacency_t * adj0;
	  union {
	    ip_buffer_opaque_t ip;
	    tcp_udp_lookup_buffer_opaque_t tcp_udp;
	  } * pi0;
	  ip6_header_t * ip60;
	  ip4_header_t * ip40;
	  tcp_header_t * tcp0;
	  u32 bi0, imin0, iest0, li0;
	  tcp_connection_state_t state0;
	  u8 error0, next0;
	  u8 min_match0, est_match0, is_min_match0, is_est_match0;
	  u8 min_oldest0, est_first_empty0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  pi0 = vlib_get_buffer_opaque (p0);

	  adj0 = ip_get_adjacency (im, pi0->ip.dst_adj_index);
	  ASSERT (adj0->lookup_next_index == IP_LOOKUP_NEXT_LOCAL);
      
	  {
	    u32x4 a0, b0, c0;

	    a0 = tm->connection_hash_seeds[is_ip6][0].as_u32x4;
	    b0 = tm->connection_hash_seeds[is_ip6][1].as_u32x4;
	    c0 = tm->connection_hash_seeds[is_ip6][2].as_u32x4;

	    if (is_ip6)
	      {
		ip60 = vlib_buffer_get_current (p0);
		tcp0 = ip6_next_header (ip60);

		a0 ^= u32x4_splat_x2 (ip60->src_address.as_u32[0]);
		b0 ^= u32x4_splat_x2 (ip60->src_address.as_u32[1]);
		c0 ^= u32x4_splat_x2 (ip60->src_address.as_u32[2]);

		hash_v3_mix_u32x (a0, b0, c0);

		a0 ^= u32x4_splat_x2 (ip60->src_address.as_u32[3]);
		b0 ^= u32x4_splat_x2 (ip60->dst_address.as_u32[0]);
		c0 ^= u32x4_splat_x2 (ip60->dst_address.as_u32[1]);

		hash_v3_mix_u32x (a0, b0, c0);

		a0 ^= u32x4_splat_x2 (ip60->dst_address.as_u32[2]);
		b0 ^= u32x4_splat_x2 (ip60->dst_address.as_u32[3]);
		c0 ^= u32x4_splat_x2 (tcp0->ports.src_and_dst);
	      }
	    else
	      {
		ip40 = vlib_buffer_get_current (p0);
		tcp0 = ip4_next_header (ip40);

		a0 ^= u32x4_splat_x2 (ip40->src_address.as_u32);
		b0 ^= u32x4_splat_x2 (ip40->dst_address.as_u32);
		c0 ^= u32x4_splat_x2 (tcp0->ports.src_and_dst);
	      }

	    hash_v3_finalize_u32x (a0, b0, c0);

	    c0 &= tm->connection_hash_masks[is_ip6].as_u32x4;

	    imin0 = u32x4_get0 (c0);
	    iest0 = u32x4_get (c0, 1);
	  }

	  if (is_ip6)
	    {
	      ASSERT (0);
	    }
	  else
	    {
	      ip4_tcp_udp_address_x4_and_timestamps_t * mina0;
	      ip4_tcp_udp_address_x4_t * esta0;

	      mina0 = vec_elt_at_index (tm->ip4_mini_connection_address_hash, imin0);
	      esta0 = vec_elt_at_index (tm->ip4_established_connection_address_hash, iest0);

	      min_match0 = ip4_tcp_udp_address_x4_match (&mina0->address_x4, ip40, tcp0);
	      est_match0 = ip4_tcp_udp_address_x4_match (esta0, ip40, tcp0);

	      min_oldest0 = find_oldest_timestamp_x4 (mina0->time_stamps, mini_now);
	      est_first_empty0 = ip4_tcp_udp_address_x4_first_empty (esta0);

	      if (PREDICT_FALSE (est_first_empty0 >= 4))
		{
		  /* Lookup in overflow hash. */
		  ASSERT (0);
		}
	    }

	  is_min_match0 = min_match0 < 4;
	  is_est_match0 = est_match0 < 4;

	  imin0 = 4 * imin0 + (is_min_match0 ? min_match0 : min_oldest0);
	  iest0 = 4 * iest0 + (is_est_match0 ? est_match0 : est_first_empty0);

	  /* Should simultaneously not match both in mini and established connection tables. */
	  ASSERT (! (is_min_match0 && is_est_match0));

	  {
	    tcp_mini_connection_t * min0;
	    tcp_established_connection_t * est0;
	    tcp_sequence_pair_t * seq_pair0;
	    u8 flags0, seq_ok_flag0, ack_ok_flag0;

	    min0 = vec_elt_at_index (tm46->mini_connections, imin0);
	    est0 = vec_elt_at_index (tm46->established_connections, iest0);

	    if (min_match0 < 4)
	      {
		ASSERT (min0->state != TCP_CONNECTION_STATE_unused);
		ASSERT (min0->state != TCP_CONNECTION_STATE_established);
	      }

	    seq_pair0 = is_min_match0 ? &min0->sequence_numbers : &est0->sequence_numbers;

	    state0 = is_min_match0 ? min0->state : TCP_CONNECTION_STATE_unused;
	    state0 = is_est_match0 ? TCP_CONNECTION_STATE_established : state0;

	    pi0->tcp_udp.established_connection_index = iest0;
	    pi0->tcp_udp.mini_connection_index = imin0;
	    pi0->tcp_udp.listener_index = li0 = tm->listener_index_by_dst_port[tcp0->ports.dst];

	    flags0 = tcp0->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_RST | TCP_FLAG_FIN);

#define TCP_LOOKUP_TCP_FLAG_SEQ_OK TCP_FLAG_PSH
#define TCP_LOOKUP_TCP_FLAG_ACK_OK TCP_FLAG_URG
#define TCP_LOOKUP_TCP_FLAG_SEQ_ACK_OK (TCP_LOOKUP_TCP_FLAG_SEQ_OK | TCP_LOOKUP_TCP_FLAG_ACK_OK)
	    seq_ok_flag0 = (clib_net_to_host_u32 (tcp0->seq_number)
			    == seq_pair0->his
			    ? TCP_LOOKUP_TCP_FLAG_SEQ_OK
			    : 0);
	    ack_ok_flag0 = (clib_net_to_host_u32 (tcp0->ack_number) - seq_pair0->ours
			    <= est0->n_tx_unacked_bytes
			    ? TCP_LOOKUP_TCP_FLAG_ACK_OK
			    : 0);

	    flags0 |= seq_ok_flag0 | ack_ok_flag0;

	    next0 = tm->disposition_by_state_and_flags[state0][flags0].next;
	    error0 = tm->disposition_by_state_and_flags[state0][flags0].error;

	    next0 = li0 != 0 ? next0 : TCP_LOOKUP_NEXT_PUNT;
	    error0 = li0 != 0 ? error0 : TCP_ERROR_NO_LISTENER_FOR_PORT;
	  }

	  p0->error = error_node->errors[error0];

	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    /* FIXME */ ;

  return frame->n_vectors;
}

static uword
ip4_tcp_lookup (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_lookup (vm, node, frame, /* is_ip6 */ 0); }

static uword
ip6_tcp_lookup (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_lookup (vm, node, frame, /* is_ip6 */ 1); }

static void
ip46_size_hash_tables (ip46_tcp_main_t * m)
{
  m->mini_connection_hash_mask = pow2_mask (m->log2_n_mini_connection_hash_elts);
  vec_validate_aligned (m->mini_connections,
			m->mini_connection_hash_mask,
			CLIB_CACHE_LINE_BYTES);

  m->established_connection_hash_mask = pow2_mask (m->log2_n_established_connection_hash_elts);
  vec_validate_aligned (m->established_connections,
			m->established_connection_hash_mask,
			CLIB_CACHE_LINE_BYTES);
}

static void
ip46_tcp_lookup_init (vlib_main_t * vm, tcp_main_t * tm, int is_ip6)
{
  ip46_tcp_main_t * m = is_ip6 ? &tm->ip6 : &tm->ip4;

  m->is_ip6 = is_ip6;

  m->log2_n_mini_connection_hash_elts = 8;
  m->log2_n_established_connection_hash_elts = 8;
  ip46_size_hash_tables (m);

  if (is_ip6)
    {
      vec_validate_aligned (tm->ip6_mini_connection_address_hash,
			    m->mini_connection_hash_mask / 4,
			    CLIB_CACHE_LINE_BYTES);
      vec_validate_aligned (tm->ip6_established_connection_address_hash,
			    m->established_connection_hash_mask / 4,
			    CLIB_CACHE_LINE_BYTES);
    }
  else
    {
      vec_validate_aligned (tm->ip4_mini_connection_address_hash,
			    m->mini_connection_hash_mask / 4,
			    CLIB_CACHE_LINE_BYTES);
      vec_validate_aligned (tm->ip4_established_connection_address_hash,
			    m->established_connection_hash_mask / 4,
			    CLIB_CACHE_LINE_BYTES);
    }
  tm->connection_hash_masks[is_ip6].as_u32x4 =
    u32x4_set_x2 (m->mini_connection_hash_mask / 4,
		  m->established_connection_hash_mask / 4);
}

static void
tcp_lookup_init (vlib_main_t * vm, tcp_main_t * tm)
{
  int is_ip6;

  /* Initialize hash seeds. */
  for (is_ip6 = 0; is_ip6 < 2; is_ip6++)
    {
      u32 * r = clib_random_buffer_get_data (&vm->random_buffer, 3 * 2 * sizeof (r[0]));
      tm->connection_hash_seeds[is_ip6][0].as_u32x4 = u32x4_set_x2 (r[0], r[1]);
      tm->connection_hash_seeds[is_ip6][1].as_u32x4 = u32x4_set_x2 (r[2], r[3]);
      tm->connection_hash_seeds[is_ip6][2].as_u32x4 = u32x4_set_x2 (r[4], r[5]);

      ip46_tcp_lookup_init (vm, tm, is_ip6);
    }

  {
    tcp_listener_t * l;

    pool_get_aligned (tm->listener_pool, l, CLIB_CACHE_LINE_BYTES);

    /* Null listener must always have zero index. */
    ASSERT (l - tm->listener_pool == 0);

    memset (l, 0, sizeof (l[0]));

    /* No adjacencies are valid. */
    l->valid_local_adjacency_bitmap = 0;

    vec_validate_init_empty (tm->listener_index_by_dst_port,
			     (1 << 16) - 1,
			     l - tm->listener_pool);
  }

  /* Initialize disposition table. */
  {
    int i, j;
    for (i = 0; i < ARRAY_LEN (tm->disposition_by_state_and_flags); i++)
      for (j = 0; j < ARRAY_LEN (tm->disposition_by_state_and_flags[i]); j++)
	{
	  tm->disposition_by_state_and_flags[i][j].next = TCP_LOOKUP_NEXT_DROP;
	  tm->disposition_by_state_and_flags[i][j].error = TCP_ERROR_LOOKUP_DROPS;
	}

#define _(t,f,n,e)							\
do {									\
    tm->disposition_by_state_and_flags[TCP_CONNECTION_STATE_##t][f].next = (n);	\
    tm->disposition_by_state_and_flags[TCP_CONNECTION_STATE_##t][f].error = (e); \
} while (0)

    /* SYNs for new connections -> tcp-listen. */
    _ (unused, TCP_FLAG_SYN | 0*TCP_LOOKUP_TCP_FLAG_SEQ_OK | 0*TCP_LOOKUP_TCP_FLAG_ACK_OK,
       TCP_LOOKUP_NEXT_LISTEN_SYN, TCP_ERROR_NONE);
    _ (unused, TCP_FLAG_SYN | 1*TCP_LOOKUP_TCP_FLAG_SEQ_OK | 0*TCP_LOOKUP_TCP_FLAG_ACK_OK,
       TCP_LOOKUP_NEXT_LISTEN_SYN, TCP_ERROR_NONE);
    _ (unused, TCP_FLAG_SYN | 0*TCP_LOOKUP_TCP_FLAG_SEQ_OK | 1*TCP_LOOKUP_TCP_FLAG_ACK_OK,
       TCP_LOOKUP_NEXT_LISTEN_SYN, TCP_ERROR_NONE);
    _ (unused, TCP_FLAG_SYN | 1*TCP_LOOKUP_TCP_FLAG_SEQ_OK | 1*TCP_LOOKUP_TCP_FLAG_ACK_OK,
       TCP_LOOKUP_NEXT_LISTEN_SYN, TCP_ERROR_NONE);

    _ (listen_ack_wait, TCP_FLAG_ACK | TCP_LOOKUP_TCP_FLAG_SEQ_ACK_OK,
       TCP_LOOKUP_NEXT_LISTEN_ACK, TCP_ERROR_NONE);

    _ (established, TCP_FLAG_ACK | TCP_LOOKUP_TCP_FLAG_SEQ_ACK_OK,
       TCP_LOOKUP_NEXT_ESTABLISHED, TCP_ERROR_NONE);

#undef _
  }
}

static char * tcp_error_strings[] = {
#define _(sym,string) string,
  foreach_tcp_error
#undef _
};

static VLIB_REGISTER_NODE (ip4_tcp_lookup_node) = {
  .function = ip4_tcp_lookup,
  .name = "ip4-tcp-lookup",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_LOOKUP_N_NEXT,
  .next_nodes = {
    [TCP_LOOKUP_NEXT_DROP] = "error-drop",
    [TCP_LOOKUP_NEXT_PUNT] = "error-punt",
    [TCP_LOOKUP_NEXT_LISTEN_SYN] = "ip4-tcp-listen",
    [TCP_LOOKUP_NEXT_LISTEN_ACK] = "ip4-tcp-establish",
    [TCP_LOOKUP_NEXT_CONNECT_SYN_ACK] = "ip4-tcp-connect",
    [TCP_LOOKUP_NEXT_ESTABLISHED] = "ip4-tcp-established",
  },

  .n_errors = TCP_N_ERROR,
  .error_strings = tcp_error_strings,
};

static VLIB_REGISTER_NODE (ip6_tcp_lookup_node) = {
  .function = ip6_tcp_lookup,
  .name = "ip6-tcp-lookup",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_LOOKUP_N_NEXT,
  .next_nodes = {
    [TCP_LOOKUP_NEXT_DROP] = "error-drop",
    [TCP_LOOKUP_NEXT_PUNT] = "error-punt",
    [TCP_LOOKUP_NEXT_LISTEN_SYN] = "ip6-tcp-listen",
    [TCP_LOOKUP_NEXT_LISTEN_ACK] = "ip4-tcp-establish",
    [TCP_LOOKUP_NEXT_CONNECT_SYN_ACK] = "ip6-tcp-connect",
    [TCP_LOOKUP_NEXT_ESTABLISHED] = "ip6-tcp-established",
  },

  .n_errors = TCP_N_ERROR,
  .error_strings = tcp_error_strings,
};

always_inline void
tcp_options_decode_for_syn (tcp_main_t * tm, tcp_mini_connection_t * m, tcp_header_t * tcp)
{
  u8 * o = (void *) (tcp + 1);
  u32 n_bytes = (tcp->tcp_header_u32s_and_reserved >> 4) * sizeof (u32);
  u8 * e = o + n_bytes;
  tcp_mini_connection_t * tmpl = &tm->option_decode_mini_connection_template;
  tcp_option_type_t t;
  u8 i, l, * p;
  u8 * option_decode[16];

  /* Initialize defaults. */
  option_decode[TCP_OPTION_MSS] = (u8 *) &tmpl->max_segment_size;
  option_decode[TCP_OPTION_WINDOW_SCALE] = (u8 *) &tmpl->window_scale;
  option_decode[TCP_OPTION_TIME_STAMP] = (u8 *) &tmpl->his_time_stamp;

  if (n_bytes > 0)
    {
#define _							\
do {								\
  t = o[0];							\
  i = t >= ARRAY_LEN (option_decode) ? TCP_OPTION_END : t;	\
  option_decode[i] = o + 2;					\
  /* Skip nop; don't skip end; else length from packet. */	\
  l = t < 2 ? t : o[1];						\
  p = o + l;							\
  o = p < e ? p : o;						\
} while (0)

      _; _; _;
      /* Fast path: NOP NOP TIMESTAMP. */
      if (o >= e) goto done;
      _; _;
      if (o >= e) goto done;
      _; _; _;

#undef _

    done:;
    }

  m->max_segment_size =
    clib_net_to_host_u16 (*(u16 *) option_decode[TCP_OPTION_MSS]);
  m->window_scale = *option_decode[TCP_OPTION_WINDOW_SCALE];
  m->his_time_stamp = clib_net_to_host_u32 (((u32 *) option_decode[TCP_OPTION_TIME_STAMP])[0]);
}

always_inline u32
tcp_options_decode_for_ack (tcp_main_t * tm, tcp_header_t * tcp,
			    u32 * his_time_stamp)
{
  u8 * o = (void *) (tcp + 1);
  u32 n_bytes = (tcp->tcp_header_u32s_and_reserved >> 4) * sizeof (u32);
  u8 * e = o + n_bytes;
  tcp_option_type_t t;
  u8 i, l, * p;
  u8 * option_decode[16];
  u32 default_time_stamps[2];

  /* Initialize defaults. */
  default_time_stamps[0] = default_time_stamps[1] = 0;
  option_decode[TCP_OPTION_TIME_STAMP] = (u8 *) &default_time_stamps;

  if (n_bytes > 0)
    {
#define _							\
do {								\
  t = o[0];							\
  i = t >= ARRAY_LEN (option_decode) ? TCP_OPTION_END : t;	\
  option_decode[i] = o + 2;					\
  /* Skip nop; don't skip end; else length from packet. */	\
  l = t < 2 ? t : o[1];						\
  p = o + l;							\
  o = p < e ? p : o;						\
} while (0)

      _; _; _;
      /* Fast path: NOP NOP TIMESTAMP. */
      if (o >= e) goto done;
      _; _;
      if (o >= e) goto done;
      _; _; _;
#undef _

    done:;
    }

  if (his_time_stamp)
    his_time_stamp[0] = clib_net_to_host_u32 (((u32 *) option_decode[TCP_OPTION_TIME_STAMP])[0]);

  return clib_net_to_host_u32 (((u32 *) option_decode[TCP_OPTION_TIME_STAMP])[1]);
}


static void
tcp_options_decode_init (tcp_main_t * tm)
{
  tcp_mini_connection_t * m = &tm->option_decode_mini_connection_template;

  memset (m, 0, sizeof (m[0]));
  m->max_segment_size = clib_host_to_net_u16 (576 - 40);
  m->window_scale = 0;
  m->his_time_stamp = 0;
}

typedef enum {
  TCP_LISTEN_NEXT_DROP,
  TCP_LISTEN_NEXT_REPLY,
  TCP_LISTEN_N_NEXT,
} tcp_listen_next_t;

always_inline uword
ip46_tcp_listen (vlib_main_t * vm,
		 vlib_node_runtime_t * node,
		 vlib_frame_t * frame,
		 uword is_ip6)
{
  tcp_main_t * tm = &tcp_main;
  ip46_tcp_main_t * tm46 = is_ip6 ? &tm->ip6 : &tm->ip4;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next, * random_ack_numbers;
  u32 n_left_from, n_left_to_next, next, mini_now, timestamp_now;
  u16 * fid, * fragment_ids;
  vlib_node_runtime_t * error_node;

  error_node = vlib_node_get_runtime
    (vm, is_ip6 ? ip6_tcp_lookup_node.index : ip4_tcp_lookup_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = TCP_LISTEN_NEXT_REPLY;
  mini_now = tcp_time_now (tm, TCP_TIMER_mini_connection);
  timestamp_now = tcp_time_now (tm, TCP_TIMER_timestamp);
  
  random_ack_numbers = clib_random_buffer_get_data (&vm->random_buffer,
						    n_packets * sizeof (random_ack_numbers[0]));
  /* Get random fragment IDs for replies. */
  fid = fragment_ids = clib_random_buffer_get_data (&vm->random_buffer,
						    n_packets * sizeof (fragment_ids[0]));

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  tcp_udp_lookup_buffer_opaque_t * pi0;
	  ip6_header_t * ip60;
	  ip4_header_t * ip40;
	  tcp_header_t * tcp0;
	  tcp_mini_connection_t * min0;
	  ip_csum_t sum0;
	  u32 bi0, imin0, my_seq_net0, his_seq_host0, his_seq_net0;
	  u8 i0, new_flags0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  pi0 = vlib_get_buffer_opaque (p0);

	  imin0 = pi0->mini_connection_index;
	  i0 = imin0 % 4;

	  if (is_ip6)
	    {
	      ip6_tcp_udp_address_x4_and_timestamps_t * mina0;

	      ip60 = vlib_buffer_get_current (p0);
	      tcp0 = ip6_next_header (ip60);

	      mina0 = vec_elt_at_index (tm->ip6_mini_connection_address_hash, imin0 / 4);

	      ip6_tcp_udp_address_x4_set_from_headers (&mina0->address_x4,
						       ip60, tcp0, i0);
	      mina0->time_stamps[i0] = mini_now;
	    }
	  else
	    {
	      ip4_tcp_udp_address_x4_and_timestamps_t * mina0;

	      ip40 = vlib_buffer_get_current (p0);
	      tcp0 = ip4_next_header (ip40);

	      mina0 = vec_elt_at_index (tm->ip4_mini_connection_address_hash, imin0 / 4);

	      ip4_tcp_udp_address_x4_set_from_headers (&mina0->address_x4,
						       ip40, tcp0, i0);
	      mina0->time_stamps[i0] = mini_now;
	    }

	  min0 = vec_elt_at_index (tm46->mini_connections, imin0);

	  min0->state = TCP_CONNECTION_STATE_listen_ack_wait;
	  min0->my_time_stamp = timestamp_now;
	  tcp_options_decode_for_syn (tm, min0, tcp0);

	  my_seq_net0 = *random_ack_numbers++;
	  his_seq_host0 = 1 + clib_net_to_host_u32 (tcp0->seq_number);

	  min0->sequence_numbers.ours = 1 + clib_net_to_host_u32 (my_seq_net0);
	  min0->sequence_numbers.his = his_seq_host0;

	  if (is_ip6)
	    {
	      ASSERT (0);
	    }
	  else
	    {
	      /* Reply to TCP SYN with a SYN-ACK. */
	      ip4_tcp_reply_x1 (ip40, tcp0);

	      sum0 = ip40->checksum;

	      sum0 = ip_csum_update (sum0, ip40->ttl, ip4_main.host_config.ttl,
				 ip4_header_t, ttl);
	      ip40->ttl = ip4_main.host_config.ttl;

	      sum0 = ip_csum_update (sum0, ip40->fragment_id, fid[0],
				     ip4_header_t, fragment_id);
	      ip40->fragment_id = fid[0];
	      fid += 1;

	      ip40->checksum = ip_csum_fold (sum0);

	      ASSERT (ip40->checksum == ip4_header_checksum (ip40));
	    }

	  sum0 = tcp0->checksum;

	  new_flags0 = TCP_FLAG_SYN | TCP_FLAG_ACK;
	  sum0 = ip_csum_update (sum0, tcp0->flags, new_flags0,
				 tcp_header_t, flags);
	  tcp0->flags = new_flags0;

	  his_seq_net0 = clib_host_to_net_u32 (his_seq_host0);

	  sum0 = ip_csum_update (sum0, tcp0->ack_number, his_seq_net0,
				 tcp_header_t, ack_number);
	  sum0 = ip_csum_update (sum0, tcp0->seq_number, my_seq_net0,
				 tcp_header_t, seq_number);

	  tcp0->ack_number = his_seq_net0;
	  tcp0->seq_number = my_seq_net0;

	  tcp0->checksum = ip_csum_fold (sum0);
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  vlib_error_count (vm, error_node->node_index,
		    TCP_ERROR_LISTEN_RESPONSES,
		    frame->n_vectors);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    /* FIXME */ ;

  return frame->n_vectors;
}

static uword
ip4_tcp_listen (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_listen (vm, node, frame, /* is_ip6 */ 0); }

static uword
ip6_tcp_listen (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_listen (vm, node, frame, /* is_ip6 */ 1); }

static VLIB_REGISTER_NODE (ip4_tcp_listen_node) = {
  .function = ip4_tcp_listen,
  .name = "ip4-tcp-listen",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_LISTEN_N_NEXT,
  .next_nodes = {
    [TCP_LISTEN_NEXT_DROP] = "error-drop",
    [TCP_LISTEN_NEXT_REPLY] = DEBUG > 0 ? "ip4-input" : "ip4-lookup",
  },
};

static VLIB_REGISTER_NODE (ip6_tcp_listen_node) = {
  .function = ip6_tcp_listen,
  .name = "ip6-tcp-listen",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_LISTEN_N_NEXT,
  .next_nodes = {
    [TCP_LISTEN_NEXT_DROP] = "error-drop",
    [TCP_LISTEN_NEXT_REPLY] = DEBUG > 0 ? "ip6-input" : "ip6-lookup",
  },
};

typedef enum {
  TCP_CONNECT_NEXT_DROP,
  TCP_CONNECT_NEXT_REPLY,
  TCP_CONNECT_N_NEXT,
} tcp_connect_next_t;

always_inline uword
ip46_tcp_connect (vlib_main_t * vm,
		 vlib_node_runtime_t * node,
		 vlib_frame_t * frame,
		 uword is_ip6)
{
  tcp_main_t * tm = &tcp_main;
  ip46_tcp_main_t * tm46 = is_ip6 ? &tm->ip6 : &tm->ip4;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  vlib_node_runtime_t * error_node;

  error_node = vlib_node_get_runtime
    (vm, is_ip6 ? ip6_tcp_lookup_node.index : ip4_tcp_lookup_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip60;
	  ip4_header_t * ip40;
	  tcp_header_t * tcp0;
	  u32 bi0;
	  u8 error0, next0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);

	  if (is_ip6)
	    {
	      ip60 = vlib_buffer_get_current (p0);
	      tcp0 = ip6_next_header (ip60);
	    }
	  else
	    {
	      ip40 = vlib_buffer_get_current (p0);
	      tcp0 = ip4_next_header (ip40);
	    }

	  ASSERT (0);

	  error0 = next0 = 0;
	  p0->error = error_node->errors[error0];

	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    /* FIXME */ ;

  return frame->n_vectors;
}

static uword
ip4_tcp_connect (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_connect (vm, node, frame, /* is_ip6 */ 0); }

static uword
ip6_tcp_connect (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_connect (vm, node, frame, /* is_ip6 */ 1); }

static VLIB_REGISTER_NODE (ip4_tcp_connect_node) = {
  .function = ip4_tcp_connect,
  .name = "ip4-tcp-connect",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_CONNECT_N_NEXT,
  .next_nodes = {
    [TCP_CONNECT_NEXT_DROP] = "error-drop",
    [TCP_CONNECT_NEXT_REPLY] = DEBUG > 0 ? "ip4-input" : "ip4-lookup",
  },
};

static VLIB_REGISTER_NODE (ip6_tcp_connect_node) = {
  .function = ip6_tcp_connect,
  .name = "ip6-tcp-connect",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_CONNECT_N_NEXT,
  .next_nodes = {
    [TCP_CONNECT_NEXT_DROP] = "error-drop",
    [TCP_CONNECT_NEXT_REPLY] = DEBUG > 0 ? "ip6-input" : "ip6-lookup",
  },
};

typedef enum {
  TCP_ESTABLISH_NEXT_DROP,
  TCP_ESTABLISH_NEXT_REPLY,
  TCP_ESTABLISH_N_NEXT,
} tcp_establish_next_t;

always_inline uword
ip46_tcp_establish (vlib_main_t * vm,
		    vlib_node_runtime_t * node,
		    vlib_frame_t * frame,
		    uword is_ip6)
{
  tcp_main_t * tm = &tcp_main;
  ip46_tcp_main_t * tm46 = is_ip6 ? &tm->ip6 : &tm->ip4;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next, mini_long_long_ago, timestamp_now;
  vlib_node_runtime_t * error_node;

  error_node = vlib_node_get_runtime
    (vm, is_ip6 ? ip6_tcp_lookup_node.index : ip4_tcp_lookup_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  mini_long_long_ago =
    (tcp_time_now (tm, TCP_TIMER_mini_connection)
     + (1 << (BITS (mini_long_long_ago) - 1)));
  timestamp_now = tcp_time_now (tm, TCP_TIMER_timestamp);
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  tcp_udp_lookup_buffer_opaque_t * pi0;
	  ip6_header_t * ip60;
	  ip4_header_t * ip40;
	  tcp_header_t * tcp0;
	  tcp_mini_connection_t * min0;
	  tcp_established_connection_t * est0;
	  tcp_listener_t * l0;
	  u32 bi0, imin0, iest0;
	  u8 error0, next0, i0, e0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  pi0 = vlib_get_buffer_opaque (p0);

	  imin0 = pi0->mini_connection_index;
	  iest0 = pi0->established_connection_index;

	  i0 = imin0 % 4;
	  e0 = iest0 % 4;

	  min0 = vec_elt_at_index (tm46->mini_connections, imin0);
	  est0 = vec_elt_at_index (tm46->established_connections, iest0);

	  if (is_ip6)
	    {
	      ip6_tcp_udp_address_x4_and_timestamps_t * mina0;
	      ip6_tcp_udp_address_x4_t * esta0;

	      ip60 = vlib_buffer_get_current (p0);
	      tcp0 = ip6_next_header (ip60);

	      mina0 = vec_elt_at_index (tm->ip6_mini_connection_address_hash, imin0 / 4);
	      esta0 = vec_elt_at_index (tm->ip6_established_connection_address_hash, iest0 / 4);

	      ip6_tcp_udp_address_x4_copy_and_invalidate (esta0, &mina0->address_x4, e0, i0);

	      mina0->time_stamps[i0] = mini_long_long_ago;
	    }
	  else
	    {
	      ip4_tcp_udp_address_x4_and_timestamps_t * mina0;
	      ip4_tcp_udp_address_x4_t * esta0;

	      ip40 = vlib_buffer_get_current (p0);
	      tcp0 = ip4_next_header (ip40);

	      mina0 = vec_elt_at_index (tm->ip4_mini_connection_address_hash, imin0 / 4);
	      esta0 = vec_elt_at_index (tm->ip4_established_connection_address_hash, iest0 / 4);

	      ip4_tcp_udp_address_x4_copy_and_invalidate (esta0, &mina0->address_x4, e0, i0);

	      mina0->time_stamps[i0] = mini_long_long_ago;
	    }

	  min0->state = TCP_CONNECTION_STATE_unused;
	  est0->sequence_numbers = min0->sequence_numbers;
	  est0->max_segment_size = min0->max_segment_size;
	  est0->his_window_scale = min0->window_scale;
	  est0->his_window = clib_net_to_host_u16 (tcp0->window);

#if 0
	  {
	    u32 t = tcp_options_decode_for_ack (tm, tcp0, 0);
	    f64 dt = (t - min0->my_time_stamp) * tm->secs_per_tick[TCP_TIMER_timestamp];

	    est0->round_trip_time_stats.sum += dt;
	    est0->round_trip_time_stats.sum2 += dt*dt;
	    est0->round_trip_time_stats.count += 1;
	    clib_warning ("dt %d %.4e", (t - min0->my_time_stamp), dt);
	  }
#endif

	  est0->my_window_scale = 7;
	  est0->my_window = 256;

	  l0 = pool_elt_at_index (tm->listener_pool, pi0->listener_index);
	  vec_add1 (l0->event_data, iest0);

	  next0 = TCP_ESTABLISH_NEXT_DROP;
	  error0 = TCP_ERROR_LISTENS_ESTABLISHED;

	  p0->error = error_node->errors[error0];

	  /* FIXME */
	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    /* FIXME */ ;

  /* Inform listeners of new connections. */
  {
    tcp_listener_t * l;
    pool_foreach (l, tm->listener_pool, ({
      if (vec_len (l->event_data) > 0)
	{
	  if (l->event_function)
	    l->event_function (l, TCP_LISTEN_CONNECTION_ESTABLISHED);
	  _vec_len (l->event_data) = 0;
	}
    }));
  }

  return frame->n_vectors;
}

static uword
ip4_tcp_establish (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_establish (vm, node, frame, /* is_ip6 */ 0); }

static uword
ip6_tcp_establish (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_establish (vm, node, frame, /* is_ip6 */ 1); }

static VLIB_REGISTER_NODE (ip4_tcp_establish_node) = {
  .function = ip4_tcp_establish,
  .name = "ip4-tcp-establish",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_ESTABLISH_N_NEXT,
  .next_nodes = {
    [TCP_ESTABLISH_NEXT_DROP] = "error-drop",
    [TCP_ESTABLISH_NEXT_REPLY] = DEBUG > 0 ? "ip4-input" : "ip4-lookup",
  },
};

static VLIB_REGISTER_NODE (ip6_tcp_establish_node) = {
  .function = ip6_tcp_establish,
  .name = "ip6-tcp-establish",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_ESTABLISH_N_NEXT,
  .next_nodes = {
    [TCP_ESTABLISH_NEXT_DROP] = "error-drop",
    [TCP_ESTABLISH_NEXT_REPLY] = DEBUG > 0 ? "ip6-input" : "ip6-lookup",
  },
};

typedef enum {
  TCP_ESTABLISHED_NEXT_DROP,
  TCP_ESTABLISHED_N_NEXT,
} tcp_established_next_t;

always_inline void
tcp_ack (tcp_main_t * tm, tcp_established_connection_t * c, u32 n_bytes)
{
  ASSERT (n_bytes == 0);
}

always_inline uword
ip46_tcp_established (vlib_main_t * vm,
		      vlib_node_runtime_t * node,
		      vlib_frame_t * frame,
		      uword is_ip6)
{
  tcp_main_t * tm = &tcp_main;
  ip46_tcp_main_t * tm46 = is_ip6 ? &tm->ip6 : &tm->ip4;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  vlib_node_runtime_t * error_node;

  error_node = vlib_node_get_runtime
    (vm, is_ip6 ? ip6_tcp_lookup_node.index : ip4_tcp_lookup_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  tcp_udp_lookup_buffer_opaque_t * pi0;
	  ip6_header_t * ip60;
	  ip4_header_t * ip40;
	  tcp_header_t * tcp0;
	  tcp_established_connection_t * est0;
	  tcp_listener_t * l0;
	  u32 bi0, n_data_bytes0, his_ack_host0, n_ack0;
	  u8 error0, next0, n_advance_bytes0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  pi0 = vlib_get_buffer_opaque (p0);

	  if (is_ip6)
	    {
	      ip60 = vlib_buffer_get_current (p0);
	      tcp0 = ip6_next_header (ip60);
	      ASSERT (ip60->protocol == IP_PROTOCOL_TCP);
	      n_advance_bytes0 = tcp_header_bytes (tcp0);
	      n_data_bytes0 = clib_net_to_host_u16 (ip60->payload_length) - n_advance_bytes0;
	      n_advance_bytes0 += sizeof (ip60[0]);
	    }
	  else
	    {
	      ip40 = vlib_buffer_get_current (p0);
	      tcp0 = ip4_next_header (ip40);
	      n_advance_bytes0 = (ip4_header_bytes (ip40)
				  + tcp_header_bytes (tcp0));
	      n_data_bytes0 = clib_net_to_host_u16 (ip40->length) - n_advance_bytes0;
	    }

	  est0 = vec_elt_at_index (tm46->established_connections,
				   pi0->established_connection_index);

	  error0 = TCP_ERROR_NO_DATA;
	  next0 = TCP_ESTABLISHED_NEXT_DROP;

	  l0 = pool_elt_at_index (tm->listener_pool, pi0->listener_index);

	  /* Update window. */
	  est0->his_window = clib_net_to_host_u16 (tcp0->window);

	  /* Update his sequence number to account for data he's just sent. */
	  est0->sequence_numbers.his += n_data_bytes0;

	  his_ack_host0 = clib_net_to_host_u32 (tcp0->ack_number);
	  n_ack0 = his_ack_host0 - est0->sequence_numbers.ours;
	  tcp_ack (tm, est0, n_ack0);
	  est0->sequence_numbers.ours = his_ack_host0;

	  next0 = n_data_bytes0 > 0 ? l0->next_index : next0;

	  vlib_buffer_advance (p0, n_advance_bytes0);

	  p0->error = error_node->errors[error0];

	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    /* FIXME */ ;

  return frame->n_vectors;
}

static uword
ip4_tcp_established (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_established (vm, node, frame, /* is_ip6 */ 0); }

static uword
ip6_tcp_established (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{ return ip46_tcp_established (vm, node, frame, /* is_ip6 */ 1); }

static VLIB_REGISTER_NODE (ip4_tcp_established_node) = {
  .function = ip4_tcp_established,
  .name = "ip4-tcp-established",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_ESTABLISHED_N_NEXT,
  .next_nodes = {
    [TCP_ESTABLISHED_NEXT_DROP] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (ip6_tcp_established_node) = {
  .function = ip6_tcp_established,
  .name = "ip6-tcp-established",

  .vector_size = sizeof (u32),

  .n_next_nodes = TCP_ESTABLISHED_N_NEXT,
  .next_nodes = {
    [TCP_ESTABLISHED_NEXT_DROP] = "error-drop",
  },
};

/* FIXME */
static VLIB_REGISTER_NODE (ip4_udp_lookup_node) = {
  .function = ip4_tcp_lookup,
  .name = "ip4-udp-lookup",

  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (ip6_udp_lookup_node) = {
  .function = ip6_tcp_lookup,
  .name = "ip6-udp-lookup",

  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

uword
ip4_tcp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index)
{
  tcp_main_t * tm = &tcp_main;
  tcp_listener_t * l;

  pool_get_aligned (tm->listener_pool, l, CLIB_CACHE_LINE_BYTES);

  memset (l, 0, sizeof (l[0]));

  l->dst_port = dst_port;
  l->next_index = vlib_node_add_next (vm, ip4_tcp_established_node.index, next_node_index);
  l->valid_local_adjacency_bitmap = 0;

  tm->listener_index_by_dst_port[clib_host_to_net_u16 (dst_port)] = l - tm->listener_pool;

  return l - tm->listener_pool;
}

uword
ip4_udp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index)
{
  return 0;
}

static void
tcp_udp_lookup_ip4_add_del_interface_address (ip4_main_t * im,
					      uword opaque,
					      u32 sw_if_index,
					      ip4_address_t * address,
					      u32 address_length,
					      u32 if_address_index,
					      u32 is_delete)
{
  tcp_main_t * tm = &tcp_main;

  tm->ip4.default_valid_local_adjacency_bitmap
    = clib_bitmap_set (tm->ip4.default_valid_local_adjacency_bitmap,
		       if_address_index,
		       is_delete ? 0 : 1);
}

static void
tcp_udp_lookup_ip6_add_del_interface_address (ip6_main_t * im,
					      uword opaque,
					      u32 sw_if_index,
					      ip6_address_t * address,
					      u32 address_length,
					      u32 if_address_index,
					      u32 is_delete)
{
  tcp_main_t * tm = &tcp_main;

  tm->ip6.default_valid_local_adjacency_bitmap
    = clib_bitmap_set (tm->ip6.default_valid_local_adjacency_bitmap,
		       if_address_index,
		       is_delete ? 0 : 1);
}

static clib_error_t *
tcp_udp_lookup_init (vlib_main_t * vm)
{
  tcp_main_t * tm = &tcp_main;
  ip4_main_t * im4 = &ip4_main;
  ip6_main_t * im6 = &ip6_main;
  ip_lookup_main_t * lm4 = &im4->lookup_main;
  ip_lookup_main_t * lm6 = &im6->lookup_main;
  clib_error_t * error;
  int i;

  if ((error = vlib_call_init_function (vm, ip4_lookup_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ip6_lookup_init)))
    return error;

  tcp_time_init (vm, tm);

  /* Setup all IP protocols to be punted and builtin-unknown. */
  for (i = 0; i < 256; i++)
    {
      lm4->local_next_by_ip_protocol[i] = IP_LOCAL_NEXT_PUNT;
      lm6->local_next_by_ip_protocol[i] = IP_LOCAL_NEXT_PUNT;
      lm4->builtin_protocol_by_ip_protocol[i] = IP_BUILTIN_PROTOCOL_UNKNOWN;
      lm6->builtin_protocol_by_ip_protocol[i] = IP_BUILTIN_PROTOCOL_UNKNOWN;
    }

  lm4->local_next_by_ip_protocol[IP_PROTOCOL_TCP] = IP_LOCAL_NEXT_TCP_LOOKUP;
  lm4->local_next_by_ip_protocol[IP_PROTOCOL_UDP] = IP_LOCAL_NEXT_UDP_LOOKUP;
  lm4->local_next_by_ip_protocol[IP_PROTOCOL_ICMP] = IP_LOCAL_NEXT_ICMP;
  lm4->builtin_protocol_by_ip_protocol[IP_PROTOCOL_TCP] = IP_BUILTIN_PROTOCOL_TCP;
  lm4->builtin_protocol_by_ip_protocol[IP_PROTOCOL_UDP] = IP_BUILTIN_PROTOCOL_UDP;
  lm4->builtin_protocol_by_ip_protocol[IP_PROTOCOL_ICMP] = IP_BUILTIN_PROTOCOL_ICMP;

  lm6->local_next_by_ip_protocol[IP_PROTOCOL_TCP] = IP_LOCAL_NEXT_TCP_LOOKUP;
  lm6->local_next_by_ip_protocol[IP_PROTOCOL_UDP] = IP_LOCAL_NEXT_UDP_LOOKUP;
  lm6->local_next_by_ip_protocol[IP_PROTOCOL_ICMP6] = IP_LOCAL_NEXT_ICMP;
  lm6->builtin_protocol_by_ip_protocol[IP_PROTOCOL_TCP] = IP_BUILTIN_PROTOCOL_TCP;
  lm6->builtin_protocol_by_ip_protocol[IP_PROTOCOL_UDP] = IP_BUILTIN_PROTOCOL_UDP;
  lm6->builtin_protocol_by_ip_protocol[IP_PROTOCOL_ICMP6] = IP_BUILTIN_PROTOCOL_ICMP;

  {
    ip4_add_del_interface_address_callback_t cb;

    cb.function = tcp_udp_lookup_ip4_add_del_interface_address;
    cb.function_opaque = 0;
    vec_add1 (im4->add_del_interface_address_callbacks, cb);
  }

  {
    ip6_add_del_interface_address_callback_t cb;

    cb.function = tcp_udp_lookup_ip6_add_del_interface_address;
    cb.function_opaque = 0;
    vec_add1 (im6->add_del_interface_address_callbacks, cb);
  }

  tcp_lookup_init (vm, tm);
  tcp_options_decode_init (tm);

  return 0;
}

VLIB_INIT_FUNCTION (tcp_udp_lookup_init);
