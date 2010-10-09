/*
 * ip/ip4_source_check.c: IP v4 check source address (unicast RPF check)
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
  u8 packet_data[64];
} ip4_source_check_trace_t;

static u8 * format_ip4_source_check_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ip4_source_check_trace_t * t = va_arg (*va, ip4_source_check_trace_t *);

  s = format (s, "%U",
	      format_ip4_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

typedef enum {
  IP4_SOURCE_CHECK_NEXT_DROP,
  IP4_SOURCE_CHECK_N_NEXT,
} ip4_source_check_next_t;

typedef enum {
  IP4_SOURCE_CHECK_REACHABLE_VIA_RX,
  IP4_SOURCE_CHECK_REACHABLE_VIA_ANY,
} ip4_source_check_type_t;

typedef struct {
  u32 no_default_route : 1;
  u32 fib_index : 31;
} ip4_source_check_config_t;

always_inline uword
ip4_source_check_inline (vlib_main_t * vm,
			 vlib_node_runtime_t * node,
			 vlib_frame_t * frame,
			 ip4_source_check_type_t source_check_type)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  vnet_config_main_t * cm = &im->config_mains[VLIB_RX];
  u32 n_left_from, * from, * to_next;
  u32 next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip4_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (ip4_source_check_trace_t));

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

#if 0
      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, pi1, next0, next1);
	}
#endif
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  ip4_source_check_config_t * c0;
	  ip_adjacency_t * adj0;
	  u32 pi0, next0, adj_index0;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_buffer (vm, pi0);
	  ip0 = vlib_buffer_get_current (p0);
	  i0 = vlib_get_buffer_opaque (p0);

	  c0 = vnet_get_config_data (cm, &i0->current_config_index,
				     &next0,
				     sizeof (c0[0]));

	  adj_index0 = ip4_fib_lookup_with_table (im, c0->fib_index,
						  &ip0->src_address,
						  c0->no_default_route);
	  adj0 = ip_get_adjacency (lm, adj_index0);

	  /* FIXME accept if multicast. */
	  next0 = ((adj0->rewrite_header.next_index == IP_LOOKUP_NEXT_REWRITE
		   && (source_check_type == IP4_SOURCE_CHECK_REACHABLE_VIA_ANY
		       || p0->sw_if_index[VLIB_RX] == adj0->rewrite_header.sw_if_index))
		   ? next0
		   : IP4_SOURCE_CHECK_NEXT_DROP);
	  p0->error = error_node->errors[IP4_ERROR_UNICAST_SOURCE_CHECK_FAILS];

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

static uword
ip4_source_check_reachable_via_any (vlib_main_t * vm,
				    vlib_node_runtime_t * node,
				    vlib_frame_t * frame)
{
  return ip4_source_check_inline (vm, node, frame, IP4_SOURCE_CHECK_REACHABLE_VIA_ANY);
}

static uword
ip4_source_check_reachable_via_rx (vlib_main_t * vm,
				    vlib_node_runtime_t * node,
				    vlib_frame_t * frame)
{
  return ip4_source_check_inline (vm, node, frame, IP4_SOURCE_CHECK_REACHABLE_VIA_RX);
}

VLIB_REGISTER_NODE (ip4_check_source_reachable_via_any) = {
  .function = ip4_source_check_reachable_via_any,
  .name = "ip4-source-check-via-any",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP4_SOURCE_CHECK_N_NEXT,
  .next_nodes = {
    [IP4_SOURCE_CHECK_NEXT_DROP] = "error-drop",
  },

  .format_buffer = format_ip4_header,
  .format_trace = format_ip4_source_check_trace,
};

VLIB_REGISTER_NODE (ip4_check_source_reachable_via_rx) = {
  .function = ip4_source_check_reachable_via_rx,
  .name = "ip4-source-check-via-rx",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP4_SOURCE_CHECK_N_NEXT,
  .next_nodes = {
    [IP4_SOURCE_CHECK_NEXT_DROP] = "error-drop",
  },

  .format_buffer = format_ip4_header,
  .format_trace = format_ip4_source_check_trace,
};

/* Dummy init function to get us linked in. */
clib_error_t * ip4_source_check_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (ip4_source_check_init);
