/*
 * ip/ip4_forward.c: IP v4 forwarding
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
#include <vnet/ethernet/ethernet.h>	/* for ethernet_header_t */

/* This is really, really simple but stupid fib. */
static ip_lookup_next_t
ip4_fib_lookup (ip4_main_t * im, u32 fib_index, ip4_address_t * dst, u32 * adj_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip4_fib_t * fib = vec_elt_at_index (im->fibs, fib_index);
  ip_adjacency_t * adj;
  uword * p, * hash, key;
  i32 i, dst_address, ai;

  dst_address = clib_mem_unaligned (&dst->data_u32, u32);
  for (i = ARRAY_LEN (fib->adj_index_by_dst_address) - 1; i >= 0; i--)
    {
      hash = fib->adj_index_by_dst_address[i];
      if (! hash)
	continue;

      key = dst_address & fib->masks[i];
      if ((p = hash_get (hash, key)) != 0)
	{
	  ai = p[0];
	  goto done;
	}
    }
    
  /* Nothing matches in table. */
  ai = lm->miss_adj_index;

 done:
  *adj_index = ai;
  adj = ip_get_adjacency (lm, ai);
  return adj->lookup_next_index;
}

static ip4_fib_t *
find_fib_by_table_id (ip4_main_t * im, u32 table_id)
{
  ip4_fib_t * fib;
  uword * p = hash_get (im->fib_index_by_table_id, table_id);
  uword i;

  if (p)
    return vec_elt_at_index (im->fibs, p[0]);

  hash_set (im->fib_index_by_table_id, table_id, vec_len (im->fibs));
  vec_add2 (im->fibs, fib, 1);

  fib->table_id = table_id;

  /* Initialize masks on first call. */
  for (i = 0; i < ARRAY_LEN (fib->masks); i++)
    {
      u32 m;

      if (i < 32)
	m = pow2_mask (i) << (32 - i);
      else 
	m = ~0;
      fib->masks[i] = clib_host_to_net_u32 (m);
    }

  return fib;
}

void
ip4_add_del_route (ip4_main_t * im,
		   u32 table_id,
		   u8 * address,
		   u32 address_length,
		   u32 adj_index,
		   u32 is_del)
{
  ip4_fib_t * fib = find_fib_by_table_id (im, table_id);
  u32 dst_address = * (u32 *) address;
  uword * hash;

  ASSERT (address_length < ARRAY_LEN (fib->masks));
  dst_address &= fib->masks[address_length];

  if (! fib->adj_index_by_dst_address[address_length])
    {
      ip_lookup_main_t * lm = &im->lookup_main;
      ASSERT (lm->fib_result_n_bytes >= sizeof (uword));
      fib->adj_index_by_dst_address[address_length] =
	hash_create (32 /* elts */,
		     /* value size */ round_pow2 (lm->fib_result_n_bytes, sizeof (uword)));
    }

  hash = fib->adj_index_by_dst_address[address_length];

  if (is_del)
    hash_unset (hash, dst_address);
  else
    hash_set (hash, dst_address, adj_index);

  fib->adj_index_by_dst_address[address_length] = hash;
}

void *
ip4_get_route (ip4_main_t * im,
	       u32 table_id,
	       u8 * address,
	       u32 address_length)
{
  ip4_fib_t * fib = find_fib_by_table_id (im, table_id);
  u32 dst_address = * (u32 *) address;
  uword * hash, * p;

  ASSERT (address_length < ARRAY_LEN (fib->masks));
  dst_address &= fib->masks[address_length];

  hash = fib->adj_index_by_dst_address[address_length];
  p = hash_get (hash, dst_address);
  return (void *) p;
}

static uword
ip4_lookup (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
  vlib_combined_counter_main_t * cm = &im->lookup_main.adjacency_counters;
  u32 n_left_from, n_left_to_next, * from;
  ip_buffer_and_adjacency_t * to_next;
  ip_lookup_next_t next;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next = node->cached_next_index;

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  u32 pi0, pi1, adj_index0, adj_index1, wrong_next;
	  ip_lookup_next_t next0, next1;
	  ip4_header_t * ip0, * ip1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);
	  }

	  pi0 = from[0];
	  pi1 = from[1];

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  next0 = ip4_fib_lookup (im, im->default_fib_table_id, &ip0->dst_address, &adj_index0);
	  next1 = ip4_fib_lookup (im, im->default_fib_table_id, &ip1->dst_address, &adj_index1);

	  to_next[0].buffer = pi0;
	  to_next[0].adj_index = adj_index0;
	  to_next[1].buffer = pi1;
	  to_next[1].adj_index = adj_index1;

	  vlib_buffer_increment_two_counters (vm, cm,
					      adj_index0, adj_index1,
					      p0, p1);

	  from += 2;
	  to_next += 2;
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  wrong_next = (next0 != next) + 2*(next1 != next);
	  if (PREDICT_FALSE (wrong_next != 0))
	    {
	      ip_buffer_and_adjacency_t * a;

	      switch (wrong_next)
		{
		case 1:
		  /* A B A */
		  to_next[-2].buffer = pi1;
		  to_next[-2].adj_index = adj_index1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  a = vlib_set_next_frame (vm, node, next0);
		  a[0].buffer = pi0;
		  a[0].adj_index = adj_index0;
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  a = vlib_set_next_frame (vm, node, next1);
		  a[0].buffer = pi1;
		  a[0].adj_index = adj_index1;
		  break;

		case 3:
		  /* A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  a = vlib_set_next_frame (vm, node, next0);
		  a[0].buffer = pi0;
		  a[0].adj_index = adj_index0;
		  a = vlib_set_next_frame (vm, node, next1);
		  a[0].buffer = pi1;
		  a[0].adj_index = adj_index1;

		  if (next0 == next1)
		    {
		      /* A B B */
		      vlib_put_next_frame (vm, node, next, n_left_to_next);
		      next = next1;
		      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
		    }
		}
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  u32 pi0, adj_index0;
	  ip_lookup_next_t next0;

	  pi0 = from[0];

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);
	  next0 = ip4_fib_lookup (im, im->default_fib_table_id, &ip0->dst_address, &adj_index0);

	  to_next[0].buffer = pi0;
	  to_next[0].adj_index = adj_index0;

	  vlib_buffer_increment_counter (vm, cm, adj_index0, p0);
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  if (PREDICT_FALSE (next0 != next))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next, n_left_to_next);
	      next = next0;
	      vlib_get_next_frame (vm, node, next,
				   to_next, n_left_to_next);
	      to_next[0].buffer = pi0;
	      to_next[0].adj_index = adj_index0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}

      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  return frame->n_vectors;
}

static VLIB_REGISTER_NODE (ip4_lookup_node) = {
  .function = ip4_lookup,
  .name = "ip4-lookup",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP_LOOKUP_N_NEXT,
  .next_nodes = {
    [IP_LOOKUP_NEXT_MISS] = "ip4-miss",
    [IP_LOOKUP_NEXT_DROP] = "ip4-drop",
    [IP_LOOKUP_NEXT_PUNT] = "ip4-punt",
    [IP_LOOKUP_NEXT_LOCAL] = "ip4-local",
    [IP_LOOKUP_NEXT_GLEAN] = "ip4-glean",
    [IP_LOOKUP_NEXT_REWRITE] = "ip4-rewrite",
    [IP_LOOKUP_NEXT_MULTIPATH] = "ip4-multipath",
  },
};

/* Global IP4 main. */
ip4_main_t ip4_main;

static clib_error_t *
ip4_lookup_init (vlib_main_t * vm)
{
  ip4_main_t * im = &ip4_main;

  /* Create FIB with table id of 0. */
  im->default_fib_table_id = 0;
  (void) find_fib_by_table_id (im, im->default_fib_table_id);

  ip_lookup_init (&im->lookup_main, ip4_lookup_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (ip4_lookup_init);

typedef struct {
  /* Adjacency taken. */
  u32 adj_index;

  /* Packet data, possibly *after* rewrite. */
  u8 packet_data[64 - 1*sizeof(u32)];
} ip4_forward_next_trace_t;

static u8 * format_ip4_forward_next_trace (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  ip4_forward_next_trace_t * t = va_arg (*args, ip4_forward_next_trace_t *);
  ip4_main_t * im = &ip4_main;
  ip_adjacency_t * adj;
  uword indent = format_get_indent (s);

  adj = ip_get_adjacency (&im->lookup_main, t->adj_index);
  s = format (s, "%U",
	      format_ip_adjacency,
	        vm, &im->lookup_main, t->adj_index);
  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_MULTIPATH:
    case IP_LOOKUP_NEXT_REWRITE:
      s = format (s, "\n%U%U",
		  format_white_space, indent,
		  format_ip_adjacency_packet_data,
		  vm, &im->lookup_main, t->adj_index,
		  t->packet_data, sizeof (t->packet_data));
      break;

    default:
      break;
    }

  return s;
}

/* Common trace function for all ip4-forward next nodes. */
static void
ip4_forward_next_trace (vlib_main_t * vm,
			vlib_node_runtime_t * node,
			vlib_frame_t * frame)
{
  ip_buffer_and_adjacency_t * from;
  u32 n_left;

  n_left = frame->n_vectors;
  from = vlib_frame_vector_args (frame);
  
  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      ip4_forward_next_trace_t * t0, * t1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, from[2].buffer, LOAD);
      vlib_prefetch_buffer_with_index (vm, from[3].buffer, LOAD);

      bi0 = from[0].buffer;
      bi1 = from[1].buffer;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = from[0].adj_index;
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      if (b1->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
	  t1->adj_index = from[1].adj_index;
	  memcpy (t1->packet_data,
		  vlib_buffer_get_current (b1),
		  sizeof (t1->packet_data));
	}
      from += 2;
      n_left -= 2;
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      ip4_forward_next_trace_t * t0;

      bi0 = from[0].buffer;

      b0 = vlib_get_buffer (vm, bi0);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = from[0].adj_index;
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      from += 1;
      n_left -= 1;
    }
}

static uword
ip4_drop_or_punt (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame,
		  ip4_error_t error_code)
{
  ip_buffer_and_adjacency_t * v = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   &v[0].buffer,
			   /* stride */ &v[1].buffer - &v[0].buffer,
			   n_packets,
			   /* next */ 0,
			   ip4_input_node.index,
			   error_code);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return n_packets;
}

static uword
ip4_drop (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_ADJACENCY_DROP); }

static uword
ip4_punt (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_ADJACENCY_PUNT); }

static uword
ip4_miss (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_LOOKUP_MISS); }

static VLIB_REGISTER_NODE (ip4_drop_node) = {
  .function = ip4_drop,
  .name = "ip4-drop",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (ip4_punt_node) = {
  .function = ip4_punt,
  .name = "ip4-punt",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-punt",
  },
};

static VLIB_REGISTER_NODE (ip4_miss_node) = {
  .function = ip4_miss,
  .name = "ip4-miss",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

/* Compute TCP/UDP checksum in software. */
u32 ip4_tcp_udp_checksum (vlib_buffer_t * p0)
{
  ip4_header_t * ip0;
  ip_csum_t sum0;
  u16 sum16;

  ip0 = vlib_buffer_get_current (p0);

  ASSERT (ip0->protocol == IP_PROTOCOL_TCP
	  || ip0->protocol == IP_PROTOCOL_UDP);

  if (ip0->protocol == IP_PROTOCOL_TCP)
    {
      tcp_header_t * tcp0 = ip4_next_header (ip0);
      u32 tcp_len0;

      tcp_len0 = clib_net_to_host_u16 (ip0->length) - ip4_header_bytes (ip0);

      /* Initialize checksum with header. */
      sum0 = clib_mem_unaligned (&ip0->src_address, u64);

      sum0 = ip_csum_with_carry
	(sum0, clib_host_to_net_u32 (tcp_len0 + (ip0->protocol << 16)));

      sum0 = ip_incremental_checksum (sum0, tcp0, tcp_len0);

      sum16 = ~ ip_csum_fold (sum0);
    }
  else
    {
      udp_header_t * udp0 = ip4_next_header (ip0);
      u32 udp_len0;

      if (udp0->checksum == 0)
	{
	  sum16 = 0;
	  goto done;
	}

      sum0 = clib_mem_unaligned (&ip0->src_address, u64);

      udp_len0 = clib_net_to_host_u16 (udp0->length);
      sum0 = ip_csum_with_carry
	(sum0, clib_host_to_net_u32 (udp_len0 + (ip0->protocol << 16)));

      sum0 = ip_incremental_checksum (sum0, udp0, udp_len0);

      sum16 = ~ ip_csum_fold (sum0);
    }

 done:
  p0->flags |= IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED;
  if (sum16 == 0)
    p0->flags |= IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT;

  return p0->flags;
}

static uword
ip4_local (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
#if 0
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  ip_local_next_t next;
  ip_buffer_and_adjacency_t * from, * to_next;
  u32 n_left_from, n_left_to_next;
  vlib_error_t unknown_protocol, tcp_checksum, udp_checksum, udp_length;

  unknown_protocol = vlib_error_set (ip4_input_node.index, IP4_ERROR_UNKNOWN_PROTOCOL);
  tcp_checksum = vlib_error_set (ip4_input_node.index, IP4_ERROR_UNKNOWN_PROTOCOL);
  unknown_protocol = vlib_error_set (ip4_input_node.index, IP4_ERROR_UNKNOWN_PROTOCOL);
  unknown_protocol = vlib_error_set (ip4_input_node.index, IP4_ERROR_UNKNOWN_PROTOCOL);

  tcp_checksum.node = ip4_input_node.index;
  tcp_checksum.code = IP4_ERROR_TCP_CHECKSUM;
  udp_checksum.node = ip4_input_node.index;
  udp_checksum.code = IP4_ERROR_UDP_CHECKSUM;
  udp_length.node = ip4_input_node.index;
  udp_length.code = IP4_ERROR_UDP_LENGTH;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame, from_adj_indices, n_packets);

while (n_left_from > 0)
    {
      to_next_meta =
	vlib_get_next_frame_meta (vm, node, next, &n_left_to_next, &to_next,
				sizeof (to_next_meta[0]));

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip4_header_t * ip0, * ip1;
	  udp_header_t * udp0, * udp1;
	  u32 pi0, ip_len0, udp_len0, flags0, adj_index0, next0, meta0;
	  u32 pi1, ip_len1, udp_len1, flags1, adj_index1, next1, meta1;
	  i32 len_diff0, len_diff1;
	  u8 is_error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
	  u8 is_error1, is_udp1, is_tcp_udp1, good_tcp_udp1, proto1;
	  u8 enqueue_code;
      
	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];
	  adj_index0 = from_adj_indices[0];
	  adj_index1 = from_adj_indices[1];
	  from += 2;
	  from_adj_indices += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);
	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  proto0 = ip0->protocol;
	  proto1 = ip1->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_udp1 = proto1 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;
	  is_tcp_udp1 = is_udp1 || proto1 == IP_PROTOCOL_TCP;

	  flags0 = p0->flags;
	  flags1 = p1->flags;

	  good_tcp_udp0 = (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
	  good_tcp_udp1 = (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  udp0 = ip4_next_header (ip0);
	  udp1 = ip4_next_header (ip1);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);
	  ip_len1 = clib_net_to_host_u16 (ip1->length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);
	  udp_len1 = clib_net_to_host_u16 (udp1->length);

	  len_diff0 = ip_len0 - udp_len0;
	  len_diff1 = ip_len1 - udp_len1;

	  len_diff0 = is_udp0 ? len_diff0 : 0;
	  len_diff1 = is_udp1 ? len_diff1 : 0;

	  p0->data_end_pad_bytes += len_diff0;
	  p1->data_end_pad_bytes += len_diff1;

	  if (PREDICT_FALSE (! (is_tcp_udp0 & is_tcp_udp1
				& good_tcp_udp0 & good_tcp_udp1)))
	    {
	      if (is_tcp_udp0)
		{
		  if (! (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags0 = ip4_tcp_udp_checksum (p0);
		  good_tcp_udp0 =
		    (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
		}
	      if (is_tcp_udp1)
		{
		  if (! (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags1 = ip4_tcp_udp_checksum (p1);
		  good_tcp_udp1 =
		    (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;
		}
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;
	  good_tcp_udp1 &= len_diff1 >= 0;

	  meta0 = meta1 = unknown_protocol.error32;

	  meta0 = len_diff0 < 0 ? udp_length.error32 : meta0;
	  meta1 = len_diff1 < 0 ? udp_length.error32 : meta1;

	  ASSERT (tcp_checksum.error32 + 1 == udp_checksum.error32);
	  meta0 = (is_tcp_udp0 && ! good_tcp_udp0
		   ? tcp_checksum.error32 + is_udp0
		   : meta0);
	  meta1 = (is_tcp_udp1 && ! good_tcp_udp1
		   ? tcp_checksum.error32 + is_udp1
		   : meta1);

	  is_error0 = meta0 != unknown_protocol.error32;
	  is_error1 = meta1 != unknown_protocol.error32;

	  next0 = lm->local_next_by_ip_protocol[proto0];
	  next1 = lm->local_next_by_ip_protocol[proto1];

 	  meta0 = (is_error0 || next0 == IP_LOCAL_NEXT_PUNT
		   ? meta0 : adj_index0);
 	  meta1 = (is_error1 || next1 == IP_LOCAL_NEXT_PUNT
		   ? meta1 : adj_index1);

	  next0 = is_error0 ? IP_LOCAL_NEXT_DROP : next0;
	  next1 = is_error1 ? IP_LOCAL_NEXT_DROP : next1;

	  to_next_meta[0] = meta0;
	  to_next_meta[1] = meta1;
	  to_next_meta += 2;

	  enqueue_code = (next0 != next) + 2*(next1 != next);

	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      u32 * e;

	      switch (enqueue_code)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = pi1;
		  to_next_meta[-2] = meta1;
		  to_next -= 1;
		  to_next_meta -= 1;
		  n_left_to_next += 1;
		  e = vlib_set_next_frame_meta (vm, node, next0, pi0,
					      sizeof (e[0]));
		  e[0] = meta0;
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  to_next_meta -= 1;
		  n_left_to_next += 1;
		  e = vlib_set_next_frame_meta (vm, node, next1, pi1,
					      sizeof (e[0]));
		  e[0] = meta1;
		  break;

		case 3:
		  to_next -= 2;
		  to_next_meta -= 2;
		  n_left_to_next += 2;
		  e = vlib_set_next_frame_meta (vm, node, next0, pi0,
					      sizeof (e[0]));
		  e[0] = meta0;

		  e = vlib_set_next_frame_meta (vm, node, next1, pi1,
					      sizeof (e[0]));
		  e[0] = meta1;

		  if (next0 == next1)
		    {
		      vlib_put_next_frame (vm, node, next, n_left_to_next);
		      next = next1;
		      to_next_meta
			= vlib_get_next_frame_meta (vm, node, next,
						  &n_left_to_next, &to_next,
						  sizeof (to_next_meta[0]));
		    }
		  break;
		}
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  udp_header_t * udp0;
	  u32 pi0, next0, ip_len0, udp_len0, flags0, adj_index0, meta0;
	  i32 len_diff0;
	  u8 is_error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
      
	  pi0 = to_next[0] = from[0];
	  adj_index0 = from_adj_indices[0];
	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  ip0 = vlib_buffer_get_current (p0);
	  udp0 = ip4_next_header (ip0);

	  proto0 = ip0->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;
	  next0 = lm->local_next_by_ip_protocol[proto0];

	  flags0 = p0->flags;

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 = (is_udp0 && udp0->checksum == 0);
	  flags0 |= ((good_tcp_udp0 << LOG2_IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED)
		     | (good_tcp_udp0 << LOG2_IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED));

	  /* Compute TCP/UDP checksum only if hardware hasn't done it for us. */
	  if (PREDICT_FALSE (is_tcp_udp0 &&
			     ! (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED)))
	    flags0 = ip4_tcp_udp_checksum (p0);

	  is_slow_path0 = (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);

	  len_diff0 = ip_len0 - udp_len0;
	  len_diff0 = is_udp0 ? len_diff0 : 0;
	  p0->current_length -= len_diff0;
	  is_slow_path0 = p0->flags & VLIB_BUFFER_NEXT_PRESENT;

	  good_tcp_udp0 &= len_diff0 >= 0;

	  meta0 = unknown_protocol.error32;

	  meta0 = len_diff0 < 0 ? udp_length.error32 : meta0;

	  ASSERT (tcp_checksum.error32 + 1 == udp_checksum.error32);
	  meta0 = (is_tcp_udp0 && ! good_tcp_udp0
		   ? tcp_checksum.error32 + is_udp0
		   : meta0);

	  is_error0 = meta0 != unknown_protocol.error32;

	  next0 = lm->local_next_by_ip_protocol[proto0];

 	  meta0 = (is_error0 || next0 == IP_LOCAL_NEXT_PUNT
		   ? meta0 : adj_index0);

	  next0 = is_error0 ? IP_LOCAL_NEXT_DROP : next0;

	  to_next_meta[0] = meta0;
	  to_next_meta += 1;

	  if (PREDICT_FALSE (next0 != next))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next, n_left_to_next);

	      next = next0;
	      to_next_meta =
		vlib_get_next_frame_meta (vm, node, next,
					&n_left_to_next, &to_next,
					sizeof (to_next_meta[0]));

	      to_next[0] = pi0;
	      to_next_meta[0] = meta0;
	      to_next += 1;
	      to_next_meta += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

 return n_packets;
#else
 ASSERT (0);
 return 0;
#endif
}

static VLIB_REGISTER_NODE (ip4_local_node) = {
  .function = ip4_local,
  .name = "ip4-local",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = IP_LOCAL_N_NEXT,
  .next_nodes = {
    [IP_LOCAL_NEXT_DROP] = "error-drop",
    [IP_LOCAL_NEXT_PUNT] = "error-punt",
    [IP_LOCAL_NEXT_TCP_LOOKUP] = "tcp4-lookup",
    [IP_LOCAL_NEXT_UDP_LOOKUP] = "udp4-lookup",
  },
};

#if 0
/* Outline of glean code. */
{
  static f64 time_last_seed_change = -1e100;
  static u32 hash_seeds[3];
  static uword hash_bitmap[256 / BITS (uword)]; 
  f64 time_now;

  time_now = vlib_time_now (vm);
  if (time_now - time_last_seed_change > 1e-3)
    {
      u32 * r = clib_random_buffer_get_data (&vm->random_buffer,
					     sizeof (hash_seeds));
      for (i = 0; i < ARRAY_LEN (hash_seeds); i++)
	hash_seeds[i] = r[i];

      /* Mark all hash keys as been not-seen before. */
      for (i = 0; i < ARRAY_LEN (hash_bitmap); i++)
	hash_bitmap[i] = 0;

      time_last_seed_change = time_now;
    }

  for each packet {
    a = hash_seeds[0];
    b = hash_seeds[1];
    c = hash_seeds[2];

    a ^= ip-header->dst_address;

    /* Include interface in hash? */
    b ^= buffer-header->sw_if_index[VLIB_RX];

    hash_mix32 (a, b, c);

    c &= BITS (hash_bitmap) - 1;
    c0 = c / BITS (uword);
    c1 = (uword) 1 << (c % BITS (uword));

    bm = hash_bitmap[c0];
    seen_already = (bm & c1) != 0;

    /* Mark it as seen. */
    hash_bitmap[c0] = bm | c1;

    next_index = seen_already ? IP4_GLEAN_NEXT_DROP : IP4_GLEAN_NEXT_PUNT;

    ...;
  }
}
#endif

static uword
ip4_glean (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  ip_buffer_and_adjacency_t * v = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   &v[0].buffer,
			   /* stride */ &v[1].buffer - &v[0].buffer,
			   n_packets,
			   /* next */ 0,
			   ip4_input_node.index,
			   IP4_ERROR_ADJACENCY_DROP);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return n_packets;
}

static VLIB_REGISTER_NODE (ip4_glean_node) = {
  .function = ip4_glean,
  .name = "ip4-glean",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

typedef enum {
  IP4_REWRITE_NEXT_DROP,
} ip4_rewrite_next_t;

static uword
ip4_rewrite_slow_path (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       ip_buffer_and_adjacency_t * from)
{
  ip_adjacency_t * adj0;
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  u32 * to_next;
  vlib_buffer_t * p0;
  ip4_header_t * ip0;
  u32 pi0, next0, rw_len0, error_code0, adj_index0;

  adj_index0 = from[0].adj_index;
  adj0 = ip_get_adjacency (lm, adj_index0);
      
  ASSERT (adj0[0].n_adj == 1);

  pi0 = from[0].buffer;
  p0 = vlib_get_buffer (vm, pi0);

  rw_len0 = adj0[0].rewrite_header.data_bytes;

  ip0 = vlib_buffer_get_current (p0) + rw_len0;
  next0 = adj0[0].rewrite_header.next_index;
  error_code0 = ~0;
  
  if (ip0->ttl == 0 || ip0->ttl == 255)
    {
      error_code0 = IP4_ERROR_TIME_EXPIRED;

      /* FIXME send an ICMP. */
    }

  else if (p0->current_length > adj0[0].rewrite_header.max_packet_bytes)
    {
      /* MTU check failed. */
      error_code0 = IP4_ERROR_MTU_EXCEEDED;

      /* FIXME fragment packet. */
    }

  /* Now put the packet on the appropriate next frame. */
  to_next = vlib_set_next_frame (vm, node, next0);
  to_next[0] = pi0;
  if (error_code0 != ~0)
    {
      next0 = IP4_REWRITE_NEXT_DROP;
      to_next[1] = vlib_error_set (ip4_input_node.index, error_code0);
    }

  return 0;
}

static uword
ip4_rewrite (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  ip_buffer_and_adjacency_t * from = vlib_frame_vector_args (frame);
  u32 n_left_from, n_left_to_next, * to_next, next_index;

  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  ip_adjacency_t * adj0, * adj1;
	  vlib_buffer_t * p0, * p1;
	  ip4_header_t * ip0, * ip1;
	  u32 pi0, rw_len0, len0, next0, checksum0, adj_index0;
	  u32 pi1, rw_len1, len1, next1, checksum1, adj_index1;
	  u8 is_slow_path;
      
	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2].buffer);
	    p3 = vlib_get_buffer (vm, from[3].buffer);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->pre_data, 32, STORE);
	    CLIB_PREFETCH (p3->pre_data, 32, STORE);

	    CLIB_PREFETCH (p2->data, sizeof (ip0[0]), STORE);
	    CLIB_PREFETCH (p3->data, sizeof (ip0[0]), STORE);
	  }

	  pi0 = to_next[0] = from[0].buffer;
	  pi1 = to_next[1] = from[1].buffer;
	  adj_index0 = from[0].adj_index;
	  adj_index1 = from[1].adj_index;

	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  /* Decrement TTL & update checksum.
	     Works either endian, so no need for byte swap. */
	  {
	    i32 ttl0 = ip0->ttl, ttl1 = ip1->ttl;

	    /* Input node should have reject packets with ttl 0. */
	    ASSERT (ip0->ttl > 0);
	    ASSERT (ip1->ttl > 0);

	    checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);
	    checksum1 = ip1->checksum + clib_host_to_net_u16 (0x0100);

	    ip0->checksum = checksum0 + (checksum0 >= 0xffff);
	    ip1->checksum = checksum1 + (checksum1 >= 0xffff);

	    ttl0 -= 1;
	    ttl1 -= 1;

	    ip0->ttl = ttl0;
	    ip1->ttl = ttl1;

	    is_slow_path = ttl0 <= 0 || ttl1 <= 0;

	    /* Verify checksum. */
	    ASSERT (ip0->checksum == ip4_header_checksum (ip0));
	    ASSERT (ip1->checksum == ip4_header_checksum (ip1));
	  }

	  /* Rewrite packet header and updates lengths. */
	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);
      
	  /* Multi-path should go elsewhere. */
	  ASSERT (adj0[0].n_adj == 1);
	  ASSERT (adj1[0].n_adj == 1);

	  rw_len0 = adj0[0].rewrite_header.data_bytes;
	  rw_len1 = adj1[0].rewrite_header.data_bytes;

	  p0->current_data -= rw_len0;
	  p1->current_data -= rw_len1;

	  len0 = p0->current_length;
	  len1 = p1->current_length;

	  len0 += rw_len0;
	  len1 += rw_len1;

	  p0->current_length = len0;
	  p1->current_length = len1;

	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
	  p1->sw_if_index[VLIB_TX] = adj1[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;
	  next1 = adj1[0].rewrite_header.next_index;

	  /* Check MTU of outgoing interface. */
	  is_slow_path += ((len0 > adj0[0].rewrite_header.max_packet_bytes)
			   || (len1 > adj1[0].rewrite_header.max_packet_bytes));

	  is_slow_path += (next0 != next_index || next1 != next_index);

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_two_headers (adj0[0], adj1[0],
				    ip0, ip1,
				    sizeof (ethernet_header_t));
      
	  if (PREDICT_FALSE (is_slow_path))
	    {
	      to_next -= 2;
	      n_left_to_next += 2;

	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);

	      ip4_rewrite_slow_path (vm, node, from - 2);
	      ip4_rewrite_slow_path (vm, node, from - 1);

	      next_index = next0 == next1 ? next1 : next_index;

	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  ip_adjacency_t * adj0;
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  u32 pi0, rw_len0, len0;
	  u8 is_slow_path;
	  u32 next0, checksum0;
      
	  adj0 = ip_get_adjacency (lm, from[0].adj_index);
      
	  /* Multi-path should go elsewhere. */
	  ASSERT (adj0[0].n_adj == 1);

	  pi0 = to_next[0] = from[0].buffer;

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);

	  /* Decrement TTL & update checksum. */
	  checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);
	  ip0->checksum = checksum0 + (checksum0 >= 0xffff);

	  /* Check TTL */
	  {
	    i32 ttl0 = ip0->ttl;

	    ASSERT (ip0->ttl > 0);

	    ttl0 -= 1;

	    ip0->ttl = ttl0;

	    is_slow_path = ttl0 <= 0;
	  }

	  ASSERT (ip0->checksum == ip4_header_checksum (ip0));

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_one_header (adj0[0], ip0, sizeof (ethernet_header_t));
      
	  /* Update packet buffer attributes/set output interface. */
	  rw_len0 = adj0[0].rewrite_header.data_bytes;
	  p0->current_data -= rw_len0;
	  len0 = p0->current_length += rw_len0;
	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;

	  /* Check MTU of outgoing interface. */
	  is_slow_path += len0 > adj0[0].rewrite_header.max_packet_bytes;

	  is_slow_path += next0 != next_index;

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  if (PREDICT_FALSE (is_slow_path))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
	      ip4_rewrite_slow_path (vm, node, from - 1);
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Need to do trace after rewrites to pick up new packet data. */
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return frame->n_vectors;
}

VLIB_REGISTER_NODE (ip4_rewrite_node) = {
  .function = ip4_rewrite,
  .name = "ip4-rewrite",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [IP4_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
ip4_multipath (vlib_main_t * vm,
	       vlib_node_runtime_t * node,
	       vlib_frame_t * frame)
{
  ip_buffer_and_adjacency_t * v = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   &v[0].buffer,
			   /* stride */ &v[1].buffer - &v[0].buffer,
			   n_packets,
			   /* next */ 0,
			   ip4_input_node.index,
			   IP4_ERROR_ADJACENCY_DROP);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return n_packets;
}

static VLIB_REGISTER_NODE (ip4_multipath_node) = {
  .function = ip4_multipath,
  .name = "ip4-multipath",
  .vector_size = sizeof (ip_buffer_and_adjacency_t),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "ip4-rewrite",
  },
};
