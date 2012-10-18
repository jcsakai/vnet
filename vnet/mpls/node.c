/*
 * mpls_node.c: mpls packet processing
 *
 * Copyright (c) 2010 Eliot Dresselhaus
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

#include <vlib/vlib.h>
#include <vnet/pg/pg.h>
#include <vnet/mpls/mpls.h>
#include <clib/sparse_vec.h>

typedef struct {
  u8 packet_data[32];
} mpls_input_trace_t;

static u8 * format_mpls_input_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  mpls_input_trace_t * t = va_arg (*va, mpls_input_trace_t *);

  s = format (s, "%U", format_mpls_header, t->packet_data);

  return s;
}

always_inline void
mpls_fib_adj_index_for_label_x1 (mpls_main_t * mm, u32 fib_index0, u32 label0, u32 * adj_index0)
{
  mpls_fib_t * f0;
  u32 is_known0, a0;

  f0 = vec_elt_at_index (mm->fibs, fib_index0);

  is_known0 = label0 < vec_len (f0->adj_index_by_label);

  label0 = is_known0 ? label0 : 0;

  a0 = f0->adj_index_by_label[label0];

  a0 = is_known0 ? a0 : IP_LOOKUP_MISS_ADJ_INDEX;

  *adj_index0 = a0;
}

always_inline void
mpls_fib_adj_index_for_label_x2 (mpls_main_t * mm,
				 u32 fib_index0, u32 fib_index1,
				 u32 label0, u32 label1,
				 u32 * adj_index0, u32 * adj_index1)
{
  mpls_fib_t * f0, * f1;
  u32 is_known0, a0;
  u32 is_known1, a1;

  f0 = vec_elt_at_index (mm->fibs, fib_index0);
  f1 = vec_elt_at_index (mm->fibs, fib_index1);

  is_known0 = label0 < vec_len (f0->adj_index_by_label);
  is_known1 = label1 < vec_len (f1->adj_index_by_label);

  label0 = is_known0 ? label0 : 0;
  label1 = is_known1 ? label1 : 0;

  a0 = f0->adj_index_by_label[label0];
  a1 = f1->adj_index_by_label[label1];

  a0 = is_known0 ? a0 : IP_LOOKUP_MISS_ADJ_INDEX;
  a1 = is_known1 ? a1 : IP_LOOKUP_MISS_ADJ_INDEX;

  *adj_index0 = a0;
  *adj_index1 = a1;
}

#define foreach_mpls_input_next					\
  _ (DROP, "error-drop")					\
  _ (PUNT, "error-punt")					\
  _ (MPLS, "mpls-input")					\
  _ (IP4, "ip4-input")						\
  _ (IP6, "ip6-input")						\
  _ (REWRITE, "mpls-rewrite")					\
  _ (REWRITE_MULTIPATH, "mpls-rewrite-multipath")		\
  _ (REWRITE_MULTIPATH_IP4, "mpls-rewrite-multipath-ip4")	\
  _ (REWRITE_MULTIPATH_IP6, "mpls-rewrite-multipath-ip6")

typedef enum {
#define _(s,n) MPLS_INPUT_NEXT_##s,
  foreach_mpls_input_next
#undef _

  MPLS_INPUT_N_NEXT,

  /* Adjacency representation of label pop. */
  MPLS_INPUT_NEXT_POP = MPLS_INPUT_NEXT_MPLS,
} mpls_input_next_t;

static uword
mpls_input (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * from_frame)
{
  mpls_main_t * mm = &mpls_main;
  u32 n_left_from, next_index, * from, * to_next;

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (mpls_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1;
	  vlib_buffer_t * b0, * b1;
	  mpls_header_t * h0, * h1;
	  ip_adjacency_t * adj0, * adj1;
	  u32 adj_index0, fib_index0, error0;
	  u32 next0, adj_next0, pop_next0;
	  u32 ip_46_0, is_ip4_0, is_ip6_0, is_final0, is_pop0, is_multipath0;
	  u32 adj_index1, fib_index1, error1;
	  u32 next1, adj_next1, pop_next1;
	  u32 ip_46_1, is_ip4_1, is_ip6_1, is_final1, is_pop1, is_multipath1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->data, sizeof (h0[0]), LOAD);
	    CLIB_PREFETCH (p3->data, sizeof (h1[0]), LOAD);
	  }

	  bi0 = from[0];
	  bi1 = from[1];
	  to_next[0] = bi0;
	  to_next[1] = bi1;
	  from += 2;
	  to_next += 2;
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  b0 = vlib_get_buffer (vm, bi0);
	  b1 = vlib_get_buffer (vm, bi1);

	  h0 = (void *) (b0->data + b0->current_data);
	  h1 = (void *) (b1->data + b1->current_data);

	  fib_index0 = vec_elt (mm->fib_index_by_sw_if_index, vnet_buffer (b0)->sw_if_index[VLIB_RX]);
	  fib_index1 = vec_elt (mm->fib_index_by_sw_if_index, vnet_buffer (b1)->sw_if_index[VLIB_RX]);

	  mpls_fib_adj_index_for_label_x2 (mm, fib_index0, fib_index1,
					   h0->label, h1->label,
					   &adj_index0, &adj_index1);

	  vnet_buffer (b0)->ip.adj_index[VLIB_TX] = adj_index0;
	  vnet_buffer (b1)->ip.adj_index[VLIB_TX] = adj_index1;

	  adj0 = ip_get_adjacency (&mm->lookup_main, adj_index0);
	  adj1 = ip_get_adjacency (&mm->lookup_main, adj_index1);

	  adj_next0 = adj0->lookup_next_index_as_int;
	  adj_next1 = adj1->lookup_next_index_as_int;

	  is_pop0 = adj_next0 == MPLS_INPUT_NEXT_POP;
	  is_pop1 = adj_next1 == MPLS_INPUT_NEXT_POP;

	  ip_46_0 = mpls_header_get_ip_version (h0);
	  ip_46_1 = mpls_header_get_ip_version (h1);

	  is_ip4_0 = ip_46_0 == 4;
	  is_ip4_1 = ip_46_1 == 4;

	  is_ip6_0 = ip_46_0 == 6;
	  is_ip6_1 = ip_46_1 == 6;

	  is_final0 = h0->is_final_label != 0;
	  is_final1 = h1->is_final_label != 0;

	  pop_next0 = pop_next1 = MPLS_INPUT_NEXT_MPLS;

	  pop_next0 = is_final0 && is_ip4_0 ? MPLS_INPUT_NEXT_IP4 : pop_next0;
	  pop_next1 = is_final1 && is_ip4_1 ? MPLS_INPUT_NEXT_IP4 : pop_next1;

	  pop_next0 = is_final0 && is_ip6_0 ? MPLS_INPUT_NEXT_IP6 : pop_next0;
	  pop_next1 = is_final1 && is_ip6_1 ? MPLS_INPUT_NEXT_IP6 : pop_next1;

	  is_multipath0 = adj_next0 == MPLS_INPUT_NEXT_REWRITE_MULTIPATH;
	  is_multipath1 = adj_next1 == MPLS_INPUT_NEXT_REWRITE_MULTIPATH;

	  adj_next0 = is_ip4_0 && is_multipath0 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP4 : adj_next0;
	  adj_next1 = is_ip4_1 && is_multipath1 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP4 : adj_next1;

	  adj_next0 = is_ip6_0 && is_multipath0 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP6 : adj_next0;
	  adj_next1 = is_ip6_1 && is_multipath1 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP6 : adj_next1;

	  next0 = is_pop0 ? pop_next0 : adj_next0;
	  next1 = is_pop1 ? pop_next1 : adj_next1;

	  /* Drop packets with ttl = 0. */
	  error0 = h0->ttl == 0 ? MPLS_ERROR_NONE : MPLS_ERROR_TIME_EXPIRED;
	  error1 = h1->ttl == 0 ? MPLS_ERROR_NONE : MPLS_ERROR_TIME_EXPIRED;

	  /* Don't forward packets with ttl = 1; only pop labels and terminate them. */
	  error0 = h0->ttl == 1 && ! is_pop0 ? MPLS_ERROR_TIME_EXPIRED : error0;
	  error1 = h1->ttl == 1 && ! is_pop1 ? MPLS_ERROR_TIME_EXPIRED : error1;

	  next0 = error0 != MPLS_ERROR_NONE ? MPLS_INPUT_NEXT_DROP : next0;
	  next1 = error1 != MPLS_ERROR_NONE ? MPLS_INPUT_NEXT_DROP : next1;

	  b0->error = node->errors[error0];
	  b1->error = node->errors[error1];

	  /* Skip past mpls header when popping label. */
	  vlib_buffer_advance (b0, is_pop0 ? sizeof (h0[0]) : 0);
	  vlib_buffer_advance (b1, is_pop1 ? sizeof (h1[0]) : 0);

	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next,n_left_to_next,
					   bi0, bi1, next0, next1);
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  mpls_header_t * h0;
	  ip_adjacency_t * adj0;
	  u32 adj_index0, fib_index0, error0;
	  u32 next0, adj_next0, pop_next0;
	  u32 ip_46_0, is_ip4_0, is_ip6_0, is_final0, is_pop0, is_multipath0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  h0 = (void *) (b0->data + b0->current_data);

	  fib_index0 = vec_elt (mm->fib_index_by_sw_if_index, vnet_buffer (b0)->sw_if_index[VLIB_RX]);

	  mpls_fib_adj_index_for_label_x1 (mm, fib_index0, h0->label, &adj_index0);

	  vnet_buffer (b0)->ip.adj_index[VLIB_TX] = adj_index0;

	  adj0 = ip_get_adjacency (&mm->lookup_main, adj_index0);

	  adj_next0 = adj0->lookup_next_index_as_int;
	  is_pop0 = adj_next0 == MPLS_INPUT_NEXT_POP;

	  ip_46_0 = mpls_header_get_ip_version (h0);
	  is_ip4_0 = ip_46_0 == 4;
	  is_ip6_0 = ip_46_0 == 6;

	  is_final0 = h0->is_final_label != 0;
	  pop_next0 = MPLS_INPUT_NEXT_MPLS;
	  pop_next0 = is_final0 && is_ip4_0 ? MPLS_INPUT_NEXT_IP4 : pop_next0;
	  pop_next0 = is_final0 && is_ip6_0 ? MPLS_INPUT_NEXT_IP6 : pop_next0;

	  is_multipath0 = adj_next0 == MPLS_INPUT_NEXT_REWRITE_MULTIPATH;
	  adj_next0 = is_ip4_0 && is_multipath0 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP4 : adj_next0;
	  adj_next0 = is_ip6_0 && is_multipath0 ? MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP6 : adj_next0;

	  next0 = is_pop0 ? pop_next0 : adj_next0;

	  /* Drop packets with ttl = 0. */
	  error0 = h0->ttl == 0 ? MPLS_ERROR_NONE : MPLS_ERROR_TIME_EXPIRED;

	  /* Don't forward packets with ttl = 1; only pop labels and terminate them. */
	  error0 = h0->ttl == 1 && ! is_pop0 ? MPLS_ERROR_TIME_EXPIRED : error0;

	  next0 = error0 != MPLS_ERROR_NONE ? MPLS_INPUT_NEXT_DROP : next0;

	  b0->error = node->errors[error0];

	  /* Skip past mpls header when popping label. */
	  vlib_buffer_advance (b0, is_pop0 ? sizeof (h0[0]) : 0);

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static char * mpls_error_strings[] = {
#define mpls_error(n,s) s,
#include "error.def"
#undef mpls_error
};

VLIB_REGISTER_NODE (mpls_input_node) = {
  .function = mpls_input,
  .name = "mpls-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_errors = MPLS_N_ERROR,
  .error_strings = mpls_error_strings,

  .n_next_nodes = MPLS_INPUT_N_NEXT,
  .next_nodes = {
#define _(s,n) [MPLS_INPUT_NEXT_##s] = n,
    foreach_mpls_input_next
#undef _
  },

  .format_buffer = format_mpls_header_with_length,
  .format_trace = format_mpls_input_trace,
  .unformat_buffer = unformat_mpls_header,
};

static u8 * format_mpls_input_next (u8 * s, va_list * args)
{
  mpls_input_next_t n = va_arg (*args, ip_lookup_next_t);

  switch (n)
    {
    default:
      s = format (s, "unknown %d", n);
      return s;

#define _(f,n) case MPLS_INPUT_NEXT_##f: s = format (s, "%s", #f); break;
      foreach_mpls_input_next;
#undef _
    }

  return s;
}

static u8 * format_mpls_ip_adjacency (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  ip_lookup_main_t * lm = va_arg (*args, ip_lookup_main_t *);
  u32 adj_index = va_arg (*args, u32);
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);

  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_REWRITE:
      s = format (s, "%U",
		  format_vnet_rewrite,
		  vm->vlib_main, &adj->rewrite_header, sizeof (adj->rewrite_data));
      break;

    default:
      s = format (s, "%U", format_mpls_input_next, adj->lookup_next_index_as_int);
      break;
    }

  return s;
}

static u8 * format_mpls_ip_adjacency_packet_data (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  ip_lookup_main_t * lm = va_arg (*args, ip_lookup_main_t *);
  u32 adj_index = va_arg (*args, u32);
  u8 * packet_data = va_arg (*args, u8 *);
  u32 n_packet_data_bytes = va_arg (*args, u32);
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);

  switch (adj->lookup_next_index_as_int)
    {
    case MPLS_INPUT_NEXT_REWRITE:
    case MPLS_INPUT_NEXT_REWRITE_MULTIPATH:
    case MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP4:
    case MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP6:
      s = format (s, "%U",
		  format_vnet_rewrite_header,
		  vm->vlib_main, &adj->rewrite_header, packet_data, n_packet_data_bytes);
      break;

    default:
      break;
    }

  return s;
}

typedef struct {
  /* Adjacency taken. */
  u32 adj_index;

  /* Packet data, possibly *after* rewrite. */
  u8 packet_data[64 - 1*sizeof(u32)];
} mpls_forward_next_trace_t;

static u8 * format_mpls_forward_next_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  mpls_forward_next_trace_t * t = va_arg (*args, mpls_forward_next_trace_t *);
  vnet_main_t * vnm = &vnet_main;
  mpls_main_t * im = &mpls_main;
  ip_adjacency_t * adj;
  uword indent = format_get_indent (s);

  adj = ip_get_adjacency (&im->lookup_main, t->adj_index);
  s = format (s, "adjacency: %U",
	      format_mpls_ip_adjacency,
	      vnm, &im->lookup_main, t->adj_index);
  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_REWRITE:
      s = format (s, "\n%U%U",
		  format_white_space, indent,
		  format_mpls_ip_adjacency_packet_data,
		  vnm, &im->lookup_main, t->adj_index,
		  t->packet_data, sizeof (t->packet_data));
      break;

    default:
      break;
    }

  return s;
}

/* Common trace function for all mpls-input next nodes. */
static void
mpls_forward_next_trace (vlib_main_t * vm,
			vlib_node_runtime_t * node,
			vlib_frame_t * frame,
			vlib_rx_or_tx_t which_adj_index)
{
  u32 * from, n_left;

  n_left = frame->n_vectors;
  from = vlib_frame_vector_args (frame);
  
  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      mpls_forward_next_trace_t * t0, * t1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, from[2], LOAD);
      vlib_prefetch_buffer_with_index (vm, from[3], LOAD);

      bi0 = from[0];
      bi1 = from[1];

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = vnet_buffer (b0)->ip.adj_index[which_adj_index];
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      if (b1->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
	  t1->adj_index = vnet_buffer (b1)->ip.adj_index[which_adj_index];
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
      mpls_forward_next_trace_t * t0;

      bi0 = from[0];

      b0 = vlib_get_buffer (vm, bi0);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = vnet_buffer (b0)->ip.adj_index[which_adj_index];
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      from += 1;
      n_left -= 1;
    }
}

typedef enum {
  MPLS_REWRITE_NEXT_DROP,
} mpls_rewrite_next_t;

always_inline uword
mpls_rewrite_inline (vlib_main_t * vm,
		     vlib_node_runtime_t * node,
		     vlib_frame_t * frame,
		     mpls_input_next_t rewrite_type,
		     int rewrite_for_locally_received_packets)
{
  ip_lookup_main_t * lm = &mpls_main.lookup_main;
  u32 * from = vlib_frame_vector_args (frame);
  u32 n_left_from, n_left_to_next, * to_next, next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, mpls_input_node.index);
  vlib_rx_or_tx_t adj_rx_tx = rewrite_for_locally_received_packets ? VLIB_RX : VLIB_TX;

  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  ip_adjacency_t * adj0, * adj1;
	  vlib_buffer_t * p0, * p1;
	  mpls_header_t * h0, * h1;
	  u32 pi0, rw_len0, next0, error0, adj_index0;
	  u32 pi1, rw_len1, next1, error1, adj_index1;
      
	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->pre_data, 32, STORE);
	    CLIB_PREFETCH (p3->pre_data, 32, STORE);

	    CLIB_PREFETCH (p2->data, sizeof (h0[0]), STORE);
	    CLIB_PREFETCH (p3->data, sizeof (h0[0]), STORE);
	  }

	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];

	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  adj_index0 = vnet_buffer (p0)->ip.adj_index[adj_rx_tx];
	  adj_index1 = vnet_buffer (p1)->ip.adj_index[adj_rx_tx];

	  h0 = vlib_buffer_get_current (p0);
	  h1 = vlib_buffer_get_current (p1);

	  error0 = error1 = MPLS_ERROR_NONE;

	  /* Decrement TTL. */
	  if (! rewrite_for_locally_received_packets)
	    {
	      i32 ttl0 = h0->ttl, ttl1 = h1->ttl;

	      /* Input node should have reject packets with ttl 0. */
	      ASSERT (h0->ttl > 0);
	      ASSERT (h1->ttl > 0);

	      ttl0 -= 1;
	      ttl1 -= 1;

	      h0->ttl = ttl0;
	      h1->ttl = ttl1;

	      error0 = ttl0 <= 0 ? MPLS_ERROR_TIME_EXPIRED : error0;
	      error1 = ttl1 <= 0 ? MPLS_ERROR_TIME_EXPIRED : error1;
	    }

	  /* Rewrite packet header and updates lengths. */
	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);
      
	  rw_len0 = adj0[0].rewrite_header.data_bytes;
	  rw_len1 = adj1[0].rewrite_header.data_bytes;

	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index0,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len0);
	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index1,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len1);

	  /* Check MTU of outgoing interface. */
	  error0 = (vlib_buffer_length_in_chain (vm, p0) > adj0[0].rewrite_header.max_l3_packet_bytes
		    ? MPLS_ERROR_MTU_EXCEEDED
		    : error0);
	  error1 = (vlib_buffer_length_in_chain (vm, p1) > adj1[0].rewrite_header.max_l3_packet_bytes
		    ? MPLS_ERROR_MTU_EXCEEDED
		    : error1);

	  p0->current_data -= rw_len0;
	  p1->current_data -= rw_len1;

	  p0->current_length += rw_len0;
	  p1->current_length += rw_len1;

	  vnet_buffer (p0)->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
	  vnet_buffer (p1)->sw_if_index[VLIB_TX] = adj1[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;
	  next1 = adj1[0].rewrite_header.next_index;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_two_headers (adj0[0], adj1[0],
				    h0, h1,
				    sizeof (ethernet_header_t));
      
	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, pi1, next0, next1);
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  ip_adjacency_t * adj0;
	  vlib_buffer_t * p0;
	  mpls_header_t * h0;
	  u32 pi0, rw_len0, adj_index0, next0, error0;
      
	  pi0 = to_next[0] = from[0];

	  p0 = vlib_get_buffer (vm, pi0);

	  adj_index0 = vnet_buffer (p0)->ip.adj_index[adj_rx_tx];

	  adj0 = ip_get_adjacency (lm, adj_index0);
      
	  h0 = vlib_buffer_get_current (p0);

	  error0 = MPLS_ERROR_NONE;

	  /* Decrement TTL. */
	  if (! rewrite_for_locally_received_packets)
	    {
	      i32 ttl0 = h0->ttl;

	      ASSERT (h0->ttl > 0);

	      ttl0 -= 1;

	      h0->ttl = ttl0;

	      error0 = ttl0 <= 0 ? MPLS_ERROR_TIME_EXPIRED : error0;
	    }

	  p0->error = error_node->errors[error0];

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_one_header (adj0[0], h0, sizeof (ethernet_header_t));
      
	  /* Update packet buffer attributes/set output interface. */
	  rw_len0 = adj0[0].rewrite_header.data_bytes;

	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index0,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len0);

	  /* Check MTU of outgoing interface. */
	  error0 = (vlib_buffer_length_in_chain (vm, p0) > adj0[0].rewrite_header.max_l3_packet_bytes
		    ? MPLS_ERROR_MTU_EXCEEDED
		    : error0);

	  p0->current_data -= rw_len0;
	  p0->current_length += rw_len0;
	  vnet_buffer (p0)->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, next0);
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Need to do trace after rewrites to pick up new packet data. */
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    mpls_forward_next_trace (vm, node, frame, adj_rx_tx);

  return frame->n_vectors;
}

static uword
mpls_rewrite_transit (vlib_main_t * vm,
		      vlib_node_runtime_t * node,
		      vlib_frame_t * frame)
{
  return mpls_rewrite_inline (vm, node, frame,
			      MPLS_INPUT_NEXT_REWRITE,
			      /* rewrite_for_locally_received_packets */ 0);
}

static VLIB_REGISTER_NODE (mpls_rewrite_node) = {
  .function = mpls_rewrite_transit,
  .name = "mpls-rewrite",
  .vector_size = sizeof (u32),

  .format_trace = format_mpls_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [MPLS_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
mpls_rewrite_multipath (vlib_main_t * vm,
			vlib_node_runtime_t * node,
			vlib_frame_t * frame)
{
  return mpls_rewrite_inline (vm, node, frame,
			      MPLS_INPUT_NEXT_REWRITE_MULTIPATH,
			      /* rewrite_for_locally_received_packets */ 0);
}

static VLIB_REGISTER_NODE (mpls_rewrite_multipath_node) = {
  .function = mpls_rewrite_multipath,
  .name = "mpls-rewrite-multipath",
  .vector_size = sizeof (u32),

  .format_trace = format_mpls_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [MPLS_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
mpls_rewrite_multipath_ip4 (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  return mpls_rewrite_inline (vm, node, frame,
			      MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP4,
			      /* rewrite_for_locally_received_packets */ 0);
}

static VLIB_REGISTER_NODE (mpls_rewrite_multipath_ip4_node) = {
  .function = mpls_rewrite_multipath_ip4,
  .name = "mpls-rewrite-multipath-ip4",
  .vector_size = sizeof (u32),

  .format_trace = format_mpls_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [MPLS_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
mpls_rewrite_multipath_ip6 (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  return mpls_rewrite_inline (vm, node, frame,
			      MPLS_INPUT_NEXT_REWRITE_MULTIPATH_IP6,
			      /* rewrite_for_locally_received_packets */ 0);
}

static VLIB_REGISTER_NODE (mpls_rewrite_multipath_ip6_node) = {
  .function = mpls_rewrite_multipath_ip6,
  .name = "mpls-rewrite-multipath-ip6",
  .vector_size = sizeof (u32),

  .format_trace = format_mpls_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [MPLS_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
mpls_rewrite_local (vlib_main_t * vm,
		    vlib_node_runtime_t * node,
		    vlib_frame_t * frame)
{
  return mpls_rewrite_inline (vm, node, frame,
			      MPLS_INPUT_NEXT_REWRITE,
			     /* rewrite_for_locally_received_packets */ 1);
}

static VLIB_REGISTER_NODE (mpls_rewrite_local_node) = {
  .function = mpls_rewrite_local,
  .name = "mpls-rewrite-local",
  .vector_size = sizeof (u32),

  .format_trace = format_mpls_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [MPLS_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static clib_error_t * mpls_input_init (vlib_main_t * vm)
{
  {
    clib_error_t * error = vlib_call_init_function (vm, mpls_init);
    if (error)
      clib_error_report (error);
  }

  mpls_setup_node (vm, mpls_input_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (mpls_input_init);
