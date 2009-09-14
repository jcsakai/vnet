/*
 * ip/tcp_udp_lookup.c: tcp socket lookup
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

#include <vnet/ip/ip.h>

typedef struct {
  union {
    u8 src_address[4], dst_address[4];
    u16 src_port, dst_port;
  };

  union {
    u32 src_address32;
    u32 dst_address32;
    u32 src_dst_port32;
  };

  union {
    u64 src_dst_address64;
    u32 src_dst_port32;
  };
} ip4_tcp_udp_address_t;

static always_inline void
ip4_tcp_udp_address_from_header (ip4_tcp_udp_address_t * a,
				 ip4_header_t * ip)
{
  tcp_header_t * tcp = ip4_next_header (ip);
  a->src_dst_address64 = clib_mem_unaligned (&ip->src_address, u64);
  a->src_dst_port32 = clib_mem_unaligned (&tcp->src_port, u32);
}

static u8 * format_ip4_tcp_udp_address (u8 * s, va_list * args)
{
  ip4_tcp_udp_address_t * a = va_arg (*args, ip4_tcp_udp_address_t *);

  s = format (s, "%U:%U -> %U:%U",
	      format_ip4_address, a->src_address,
	      format_tcp_udp_port, a->src_port,
	      format_ip4_address, a->dst_address,
	      format_tcp_udp_port, a->dst_port);

  return s;
}

typedef struct {
  /* CPU cycle counter of last packet received. */
  u64 cpu_time_last_packet;

  /* Source/destination address/ports. */
  ip4_tcp_udp_address_t address;

  /* Number of bytes pending processing. */
  u32 n_bytes_pending;

  /* Buffer head/tail pointer. */
  u32 pending_buffer_head, pending_buffer_tail;

  u32 flags;
#define IP4_TCP_UDP_CONNECTION_IS_UDP (1 << 0)

  /* {tcp4,udp4}-lookup next index for packets matching this connection. */
  u16 next_index;

  u16 listener_index;

  /* User's per-connection data follows.
     Should be aligned to 64 bits. */
  u8 user_data[64
	       - 1*sizeof (u64)
	       - 1*sizeof (ip4_tcp_udp_address_t)
	       - 4*sizeof (u32)
	       - 2*sizeof (u16)];
} ip4_tcp_udp_connection_t;

typedef struct {
  /* Bitmap indicating which of 64 possibly local adjacencies
     we should listen on for this destination port. */
  u64 valid_local_adjacency_bitmap;

  u32 flags;
#define IP4_TCP_UDP_LISTENER_IS_UDP (1 << 0)

  /* Destination tcp/udp port to listen for connections. */
  u16 dst_port;

  /* Next index relative to {tcp4,udp4}-lookup for packets
     matching dst port. */
  u16 next_index;

  /* User's per-listener data follows.
     Should be aligned to 64 bits. */
  u8 user_data[64
	       - 1*sizeof (u64)
	       - 1*sizeof (u32)
	       - 2*sizeof (u16)];
} ip4_tcp_udp_listener_t;

typedef struct {
  vlib_main_t * vlib_main;

  /* Node index for lookup e.g. udp4-lookup. */
  u32 node_index;

  ip4_tcp_udp_connection_t * connection_pool;

  ip4_tcp_udp_listener_t * listener_pool;

  /* Table mapping destination port to listener index. */
  u16 * listener_index_by_dst_port;

  /* Packet/byte counters for each live connection. */
  vlib_combined_counter_main_t * connection_counters;

  /* Jenkins hash seeds for connection hash lookup. */
  u32 hash_seeds[3];

  /* Hash table mapping connection address to index into connection pool. */
  uword * connection_index_by_address;
} ip4_tcp_udp_lookup_main_t;

typedef enum {
  IP4_LOOKUP_TCP, IP4_LOOKUP_UDP,
} ip4_lookup_tcp_or_udp_t;

/* One for TCP; one for UDP. */
static ip4_tcp_udp_lookup_main_t ip4_tcp_udp_lookup_mains[2];

static inline ip4_tcp_udp_lookup_main_t *
ip4_tcp_udp_main_for_connection (ip4_tcp_udp_connection_t * c)
{
  return ip4_tcp_udp_lookup_mains
    + ((c->flags & IP4_TCP_UDP_CONNECTION_IS_UDP)
       ? IP4_LOOKUP_UDP : IP4_LOOKUP_TCP);
}

static inline ip4_tcp_udp_lookup_main_t *
ip4_tcp_udp_main_for_listener (ip4_tcp_udp_listener_t * l)
{
  return ip4_tcp_udp_lookup_mains
    + ((l->flags & IP4_TCP_UDP_LISTENER_IS_UDP)
       ? IP4_LOOKUP_UDP : IP4_LOOKUP_TCP);
}

static inline u32
ip4_tcp_udp_new_connection (ip4_lookup_tcp_or_udp_t tu, ip4_header_t * ip)
{
  ip4_tcp_udp_lookup_main_t * lm = ip4_tcp_udp_lookup_mains + tu;
  ip4_tcp_udp_connection_t * c;
  u32 ci;

  pool_get_aligned (lm->connection_pool, c, CLIB_CACHE_LINE_BYTES);
  ci = c - lm->connection_pool;

  ip4_tcp_udp_address_from_header (&c->address, ip);
  hash_set (lm->connection_index_by_address, 1 + 2*ci, ci);

  return ci;
}

static u8 * format_ip4_tcp_udp_connection (u8 * s, va_list * args)
{
  ip4_tcp_udp_lookup_main_t * lm;
  ip4_tcp_udp_connection_t * c = va_arg (*args, ip4_tcp_udp_connection_t *);

  lm = ip4_tcp_udp_main_for_connection (c);

  s = format (s, "%U, next %U",
	      format_ip4_tcp_udp_address, &c->address,
	      format_vlib_node_name, lm->vlib_main, lm->node_index, c->next_index);
	      
  if (c->n_bytes_pending > 0)
    /* FIXME */;

  return s;
}

static u8 * format_ip4_tcp_udp_listener (u8 * s, va_list * args)
{
  ip4_tcp_udp_lookup_main_t * lm;
  ip4_tcp_udp_listener_t * l = va_arg (*args, ip4_tcp_udp_listener_t *);

  lm = ip4_tcp_udp_main_for_listener (l);

  s = format (s, "%U -> %U",
	      format_tcp_udp_port, l->dst_port,
	      format_vlib_next_node_name,
	        lm->vlib_main, lm->node_index, l->next_index);

  return s;
}

typedef struct {
  u32 connection_index;
  u16 is_udp;
  u16 listener_index;
} ip4_tcp_udp_lookup_trace_t;

static u8 * format_ip4_tcp_udp_lookup_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ip4_tcp_udp_lookup_trace_t * t = va_arg (*va, ip4_tcp_udp_lookup_trace_t *);
  ip4_tcp_udp_lookup_main_t * lm;
  ip4_tcp_udp_listener_t * l;
  uword indent;

  lm = ip4_tcp_udp_lookup_mains + (t->is_udp
				   ? IP4_LOOKUP_UDP : IP4_LOOKUP_TCP);
  l = pool_elt_at_index (lm->listener_pool, t->listener_index);

  indent = format_get_indent (s);

  if (t->listener_index != 0)
    s = format (s, "listener: %U",
		format_ip4_tcp_udp_listener, l,
		format_white_space, indent);
  else
    s = format (s, "no listener");

  if (t->connection_index != 0)
    {
      ip4_tcp_udp_connection_t * c;
      c = pool_elt_at_index (lm->connection_pool, t->connection_index);
      s = format (s, "\n%Uconnection: %U",
		  format_white_space, indent,
		  format_ip4_tcp_udp_connection, c);
    }

  return s;
}

typedef enum {
  TCP_UDP_LOOKUP_NEXT_ERROR,
  TCP_UDP_LOOKUP_N_NEXT,
} tcp_udp_lookup_next_t;

/* Dispatching on tcp/udp listeners (by dst port)
   and tcp/udp connections (by src/dst address/port). */
static uword
ip4_tcp_udp_lookup (vlib_main_t * vm,
		    vlib_node_runtime_t * node,
		    vlib_frame_t * frame,
		    int is_udp)
{
  ip_lookup_main_t * im = &ip4_main.lookup_main;
  ip4_tcp_udp_lookup_main_t * lm;
  uword n_packets = frame->n_vectors;
  ip_buffer_and_adjacency_t * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  vlib_error_t unknown_port_error;

  lm = ip4_tcp_udp_lookup_mains + (is_udp ? IP4_LOOKUP_UDP : IP4_LOOKUP_TCP);

  unknown_port_error =
    vlib_error_set (ip4_input_node.index,
		    (is_udp
		     ? IP4_ERROR_UNKNOWN_UDP_PORT
		     : IP4_ERROR_UNKNOWN_TCP_PORT));

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip_adjacency_t * adj0;
	  ip4_tcp_udp_connection_t * c0;
	  ip4_tcp_udp_listener_t * l0;
	  ip4_header_t * ip0;
	  tcp_header_t * tcp0;
	  u32 pi0, ci0, li0, i0, next0, listener_is_valid0;
	  uword * pci0;
      
	  adj0 = ip_get_adjacency (im, from[0].adj_index);
	  ASSERT (adj0->lookup_next_index == IP_LOOKUP_NEXT_LOCAL);
      
	  pi0 = to_next[0].buffer = from[0].buffer;

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);
	  tcp0 = ip4_next_header (ip0);

	  pci0 = hash_get (lm->connection_index_by_address,
			   pointer_to_uword (ip0));
	  ci0 = pci0 ? pci0[0] : 0;
	  c0 = pool_elt_at_index (lm->connection_pool, ci0);

	  li0 = lm->listener_index_by_dst_port[tcp0->dst_port];
	  l0 = pool_elt_at_index (lm->listener_pool, li0);

	  listener_is_valid0 =
	    (l0->valid_local_adjacency_bitmap >> adj0->local_index) & 1;

	  i0 = ci0 ? ci0 : li0;
	  i0 = listener_is_valid0 ? i0 : unknown_port_error;

	  next0 = ci0 ? c0->next_index : l0->next_index;

	  to_next[0].adj_index = i0;

	  if (PREDICT_FALSE (p0->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      ip4_tcp_udp_lookup_trace_t * t;
	      t = vlib_add_trace (vm, node, p0, sizeof (t[0]));
	      t->listener_index = li0;
	      t->connection_index = ci0;
	    }

	  if (PREDICT_FALSE (next0 != next))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0].buffer = pi0;
	      to_next[0].adj_index = i0;
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
tcp4_lookup (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{ return ip4_tcp_udp_lookup (vm, node, frame, /* is_udp */ 0); }

static uword
udp4_lookup (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{ return ip4_tcp_udp_lookup (vm, node, frame, /* is_udp */ 1); }

static VLIB_REGISTER_NODE (tcp4_lookup_node) = {
  .function = tcp4_lookup,
  .name = "tcp4-lookup",

  .vector_size = sizeof (ip_adjacency_index_t),

  .format_trace = format_ip4_tcp_udp_lookup_trace,

  .n_next_nodes = TCP_UDP_LOOKUP_N_NEXT,
  .next_nodes = {
    [TCP_UDP_LOOKUP_NEXT_ERROR] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (udp4_lookup_node) = {
  .function = udp4_lookup,
  .name = "udp4-lookup",

  .vector_size = sizeof (ip_adjacency_index_t),

  .format_trace = format_ip4_tcp_udp_lookup_trace,

  .n_next_nodes = TCP_UDP_LOOKUP_N_NEXT,
  .next_nodes = {
    [TCP_UDP_LOOKUP_NEXT_ERROR] = "error-drop",
  },
};

static inline ip4_tcp_udp_address_t *
ip4_tcp_udp_key_to_address (ip4_tcp_udp_lookup_main_t * lm,
			    ip4_tcp_udp_address_t * a,
			    uword key)
{
  ip4_tcp_udp_connection_t * c;

  if (key & 1)
    {
      c = pool_elt_at_index (lm->connection_pool, key / 2);
      a = &c->address;
    }
  else
    {    
      ip4_header_t * ip = uword_to_pointer (key, ip4_header_t *);
      ip4_tcp_udp_address_from_header (a, ip);
    }				     

  return a;
}

static uword ip4_tcp_udp_address_key_sum (hash_t * h, uword key)
{
  ip4_tcp_udp_lookup_main_t * lm = ip4_tcp_udp_lookup_mains + h->user;
  ip4_tcp_udp_address_t _a, * a;
  u32 x0, x1, x2;

  a = ip4_tcp_udp_key_to_address (lm, &_a, key);

  x0 = lm->hash_seeds[0] ^ a->src_address32;
  x1 = lm->hash_seeds[1] ^ a->dst_address32;
  x2 = lm->hash_seeds[2] ^ a->src_dst_port32;

  hash_mix32 (x0, x1, x2);

  return x2;
}

static uword ip4_tcp_udp_address_key_equal (hash_t * h, uword key1, uword key2)
{
  ip4_tcp_udp_lookup_main_t * lm = ip4_tcp_udp_lookup_mains + h->user;
  ip4_tcp_udp_address_t _a[2], * a1, * a2;

  a1 = ip4_tcp_udp_key_to_address (lm, &_a[0], key1);
  a2 = ip4_tcp_udp_key_to_address (lm, &_a[1], key2);

  return (a1->src_dst_address64 == a2->src_dst_address64
	  && a1->src_dst_port32 == a2->src_dst_port32);
}

static void
ip4_tcp_udp_lookup_main_init (ip4_tcp_udp_lookup_main_t * lm,
			      ip4_lookup_tcp_or_udp_t tcp_or_udp)
{
  ip4_tcp_udp_listener_t * l;
  ip4_tcp_udp_connection_t * c;

  /* Listeners and connection should be cache aligned and sized. */
  ASSERT (sizeof (l[0]) % CLIB_CACHE_LINE_BYTES == 0);
  ASSERT (sizeof (c[0]) % CLIB_CACHE_LINE_BYTES == 0);

  lm->connection_index_by_address =
    hash_create2 (/* initial size */ 1024,
		  /* user opaque */ tcp_or_udp,
		  /* value size */ sizeof (uword),
		  ip4_tcp_udp_address_key_sum,
		  ip4_tcp_udp_address_key_equal,
		  /* format_pair */ 0,
		  /* format_pair_arg */ 0);

  pool_get_aligned (lm->listener_pool, l, CLIB_CACHE_LINE_BYTES);

  /* Null listener must always have zero index. */
  ASSERT (l - lm->listener_pool == 0);

  memset (l, 0xff, sizeof (l[0]));
  l->flags = 0;
  l->next_index = TCP_UDP_LOOKUP_NEXT_ERROR;

  /* No adjacencies are valid. */
  l->valid_local_adjacency_bitmap = 0;

  vec_validate_init_empty (lm->listener_index_by_dst_port,
			   (1 << 16) - 1,
			   l - lm->listener_pool);

  pool_get_aligned (lm->connection_pool, c, CLIB_CACHE_LINE_BYTES);
  /* Null connection must always have zero index. */
  ASSERT (c - lm->connection_pool == 0);

  memset (c, 0xff, sizeof (c[0]));
}

static clib_error_t *
tcp_udp_lookup_init (vlib_main_t * vm)
{
  ip4_main_t * im = &ip4_main;
  int i;

  /* Setup all IP protocols to be punted. */
  for (i = 0; i < 256; i++)
    im->lookup_main.local_next_by_ip_protocol[i] = IP_LOCAL_NEXT_PUNT;

  im->lookup_main.local_next_by_ip_protocol[IP_PROTOCOL_TCP]
    = IP_LOCAL_NEXT_TCP_LOOKUP;
  im->lookup_main.local_next_by_ip_protocol[IP_PROTOCOL_UDP]
    = IP_LOCAL_NEXT_UDP_LOOKUP;

  for (i = 0; i < ARRAY_LEN (ip4_tcp_udp_lookup_mains); i++)
    ip4_tcp_udp_lookup_main_init (ip4_tcp_udp_lookup_mains + i, i);

  return 0;
}

VLIB_INIT_FUNCTION (tcp_udp_lookup_init);
