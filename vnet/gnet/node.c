/*
 * node.c: gnet packet processing
 *
 * Copyright (c) 2012 Eliot Dresselhaus
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
#include <vnet/gnet/gnet.h>

typedef struct {
  u8 packet_data[32];
} gnet_input_trace_t;

static u8 * format_gnet_input_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  gnet_input_trace_t * t = va_arg (*va, gnet_input_trace_t *);

  s = format (s, "%U", format_gnet_header, t->packet_data);

  return s;
}

always_inline uword
gnet_input_inline (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * from_frame,
		   gnet_interface_role_t role)
{
  vnet_main_t * vnm = &vnet_main;
  gnet_main_t * gm = &gnet_main;
  u32 n_left_from, next_index, * from, * to_next;

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (gnet_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

#if 0
      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1, sw_if_index0, sw_if_index1;
	  vlib_buffer_t * b0, * b1;
	  u8 next0, next1, error0, error1;
	  gnet_header_t * s0, * s1;
	  gnet_input_disposition_t * d0, * d1;
	  vnet_hw_interface_t * hi0, * hi1;
	  gnet_interface_t * si0, * si1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * b2, * b3;

	    b2 = vlib_get_buffer (vm, from[2]);
	    b3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (b2, LOAD);
	    vlib_prefetch_buffer_header (b3, LOAD);

	    CLIB_PREFETCH (b2->data, sizeof (gnet_header_t), LOAD);
	    CLIB_PREFETCH (b3->data, sizeof (gnet_header_t), LOAD);
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

	  s0 = (void *) (b0->data + b0->current_data);
	  s1 = (void *) (b1->data + b1->current_data);

	  /* Data packets are always assigned to side A (outer ring) interface. */
	  sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
	  sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

	  hi0 = vnet_get_sup_hw_interface (vnm, sw_if_index0);
	  hi1 = vnet_get_sup_hw_interface (vnm, sw_if_index1);

	  si0 = pool_elt_at_index (gm->interface_pool, hi0->hw_instance);
	  si1 = pool_elt_at_index (gm->interface_pool, hi1->hw_instance);

	  sw_if_index0 = (s0->mode == GNET_MODE_data
			  ? si0->rings[GNET_RING_OUTER].sw_if_index
			  : sw_if_index0);
	  sw_if_index1 = (s1->mode == GNET_MODE_data
			  ? si1->rings[GNET_RING_OUTER].sw_if_index
			  : sw_if_index1);
	    
	  vnet_buffer (b0)->sw_if_index[VLIB_RX] = sw_if_index0;
	  vnet_buffer (b1)->sw_if_index[VLIB_RX] = sw_if_index1;

	  d0 = gnet_input_disposition_by_mode + s0->mode;
	  d1 = gnet_input_disposition_by_mode + s1->mode;

	  next0 = d0->next_index;
	  next1 = d1->next_index;

	  error0 = d0->error;
	  error1 = d1->error;

	  vlib_buffer_advance (b0, d0->buffer_advance);
	  vlib_buffer_advance (b1, d1->buffer_advance);

	  b0->error = node->errors[error0];
	  b1->error = node->errors[error1];

	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, bi1, next0, next1);
	}
#endif
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0, sw_if_index0;
	  vlib_buffer_t * b0;
	  u32 next0, fh0, ew_next0, ns_next0;
	  u8 g0_x0, g0_x1, g0_router_x0, g0_router_x1;
	  u8 r0, dst_this_rack0, both_valid0, is_local_control0;
	  gnet_header_t * g0;
	  gnet_interface_t * gi0;
	  vnet_hw_interface_t * hi0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  g0 = (void *) (b0->data + b0->current_data);

	  /* Data packets are always assigned to direction 0 interface. */
	  sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

	  hi0 = vnet_get_sup_hw_interface (vnm, sw_if_index0);

	  gi0 = pool_elt_at_index (gm->interface_pool, hi0->hw_instance);

	  fh0 = clib_net_to_host_u32 (g0->flow_hash);

	  g0_x0 = gnet_address_get (&g0->dst_address, 0);
	  g0_x1 = gnet_address_get (&g0->dst_address, 1);

	  dst_this_rack0 = gnet_address_get_23 (&g0->dst_address) == gi0->address_23;

	  if (role == GNET_INTERFACE_ROLE_x2x3_interconnect)
	    {
	      u32 ew0_next0, ew2_next0;
	      u32 ns1_next0, ns3_next0;
	      u8 g0_x2, g0_x3;

	      g0_x2 = gnet_address_get (&g0->dst_address, 2);
	      g0_x3 = gnet_address_get (&g0->dst_address, 3);

	      ew0_next0 = gi0->input_next_by_dst[0][g0_x0];
	      ns1_next0 = gi0->input_next_by_dst[1][g0_x1];

	      ew2_next0 = gi0->input_next_by_dst[2][g0_x2];
	      ns3_next0 = gi0->input_next_by_dst[3][g0_x3];

	      ew_next0 = dst_this_rack0 ? ew0_next0 : ew2_next0;
	      ns_next0 = dst_this_rack0 ? ns1_next0 : ns3_next0;
	    }
	  else
	    {
	      r0 = fh0 % ARRAY_LEN (gi0->router_x0);
	      g0_router_x0 = gi0->router_x0[r0];
	      g0_router_x1 = gi0->router_x1[r0];


	      g0_x0 = dst_this_rack0 ? g0_x0 : g0_router_x0;
	      g0_x1 = dst_this_rack0 ? g0_x1 : g0_router_x1;
	      ew_next0 = gi0->input_next_by_dst[0][g0_x0];
	      ns_next0 = gi0->input_next_by_dst[1][g0_x1];
	    }

	  both_valid0 = (ew_next0 >= GNET_INPUT_N_NEXT) && (ns_next0 >= GNET_INPUT_N_NEXT);

	  next0 = ew_next0 >= GNET_INPUT_N_NEXT ? ew_next0 : ns_next0;

	  /* If both directions are valid use flow hash to choose direction. */
	  next0 = (both_valid0 && (fh0 & (1 << g0->flow_hash_bit))
		   ? ns_next0
		   : next0);

	  g0->flow_hash_bit += both_valid0;

	  is_local_control0 = g0->is_control && gnet_address_is_equal (&g0->dst_address, &gi0->address);

	  next0 = is_local_control0 ? GNET_INPUT_NEXT_CONTROL : next0;

	  b0->error = node->errors[GNET_ERROR_INVALID_ADDRESS];

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static uword
gnet_input (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * from_frame)
{ return gnet_input_inline (vm, node, from_frame, GNET_INTERFACE_ROLE_x0x1_interconnect); }

static uword
gnet_x2x3_interconnect_input (vlib_main_t * vm,
			      vlib_node_runtime_t * node,
			      vlib_frame_t * from_frame)
{ return gnet_input_inline (vm, node, from_frame, GNET_INTERFACE_ROLE_x2x3_interconnect); }

static char * gnet_error_strings[] = {
#define _(f,s) s,
  foreach_gnet_error
#undef _
};

VLIB_REGISTER_NODE (gnet_input_node) = {
  .function = gnet_input,
  .name = "gnet-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_errors = GNET_N_ERROR,
  .error_strings = gnet_error_strings,

  .n_next_nodes = GNET_INPUT_N_NEXT,
  .next_nodes = {
    [GNET_INPUT_NEXT_ERROR] = "error-drop",
    [GNET_INPUT_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [GNET_INPUT_NEXT_CONTROL] = "gnet-control-input",
  },

  .format_buffer = format_gnet_header_with_length,
  .format_trace = format_gnet_input_trace,
  .unformat_buffer = unformat_gnet_header,
};

static VLIB_REGISTER_NODE (gnet_x2x3_interconnect_input_node) = {
  .function = gnet_x2x3_interconnect_input,
  .name = "gnet-x2x3-interconnect-input",
  .sibling_of = "gnet-input",

  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_next_nodes = GNET_INPUT_N_NEXT,
  .next_nodes = {
    [GNET_INPUT_NEXT_ERROR] = "error-drop",
    [GNET_INPUT_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [GNET_INPUT_NEXT_CONTROL] = "gnet-control-input",
  },

  .format_buffer = format_gnet_header_with_length,
  .format_trace = format_gnet_input_trace,
  .unformat_buffer = unformat_gnet_header,
};

typedef uword (gnet_control_handler_function_t) (vlib_main_t * vm,
						 u32 sw_if_index,
						 gnet_control_header_t * h);

static uword
gnet_control_input (vlib_main_t * vm,
		    vlib_node_runtime_t * node,
		    vlib_frame_t * from_frame)
{
  u32 n_left_from, next_index, * from, * to_next;
  vlib_node_runtime_t * error_node;

  error_node = vlib_node_get_runtime (vm, gnet_input_node.index);

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (gnet_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  u8 next0, error0;
	  gnet_control_header_t * h0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  h0 = (void *) (b0->data + b0->current_data) + sizeof (gnet_header_t);

	  if (error0 == GNET_ERROR_CONTROL_PACKETS_PROCESSED)
	    {
	      static gnet_control_handler_function_t * t[GNET_N_CONTROL_PACKET_TYPE] = {
	      };
	      gnet_control_handler_function_t * f;

	      f = 0;
	      if (h0->type < ARRAY_LEN (t))
		f = t[h0->type];

	      if (f)
		error0 = f (vm, vnet_buffer (b0)->sw_if_index[VLIB_RX], h0);
	      else
		error0 = GNET_ERROR_UNKNOWN_CONTROL;
	    }

	  b0->error = error_node->errors[error0];
	  next0 = 0;

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static VLIB_REGISTER_NODE (gnet_control_input_node) = {
  .function = gnet_control_input,
  .name = "gnet-control-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },

  .format_buffer = format_gnet_header_with_length,
  .format_trace = format_gnet_input_trace,
  .unformat_buffer = unformat_gnet_header,
};

#if 0
static void serialize_gnet_interface_state_msg (serialize_main_t * m, va_list * va)
{
  gnet_interface_t * si = va_arg (*va, gnet_interface_t *);
  gnet_main_t * gm = &gnet_main;
  int r;

  ASSERT (! pool_is_free (gm->interface_pool, si));
  serialize_integer (m, si - gm->interface_pool, sizeof (u32));
  serialize_likely_small_unsigned_integer (m, si->current_ips_state);
  for (r = 0; r < GNET_N_RING; r++)
    {
      gnet_interface_ring_t * ir = &si->rings[r];
      void * p;
      serialize_likely_small_unsigned_integer (m, ir->rx_neighbor_address_valid);
      if (ir->rx_neighbor_address_valid)
	{
	  p = serialize_get (m, sizeof (ir->rx_neighbor_address));
	  memcpy (p, ir->rx_neighbor_address, sizeof (ir->rx_neighbor_address));
	}
      serialize_likely_small_unsigned_integer (m, ir->waiting_to_restore);
      if (ir->waiting_to_restore)
	serialize (m, serialize_f64, ir->wait_to_restore_start_time);
    }
}

static void unserialize_gnet_interface_state_msg (serialize_main_t * m, va_list * va)
{
  CLIB_UNUSED (mc_main_t * mcm) = va_arg (*va, mc_main_t *);
  gnet_main_t * gm = &gnet_main;
  gnet_interface_t * si;
  u32 si_index, r;

  unserialize_integer (m, &si_index, sizeof (u32));
  si = pool_elt_at_index (gm->interface_pool, si_index);
  si->current_ips_state = unserialize_likely_small_unsigned_integer (m);
  for (r = 0; r < GNET_N_RING; r++)
    {
      gnet_interface_ring_t * ir = &si->rings[r];
      void * p;
      ir->rx_neighbor_address_valid = unserialize_likely_small_unsigned_integer (m);
      if (ir->rx_neighbor_address_valid)
	{
	  p = unserialize_get (m, sizeof (ir->rx_neighbor_address));
	  memcpy (ir->rx_neighbor_address, p, sizeof (ir->rx_neighbor_address));
	}
      ir->waiting_to_restore = unserialize_likely_small_unsigned_integer (m);
      if (ir->waiting_to_restore)
	unserialize (m, unserialize_f64, &ir->wait_to_restore_start_time);
    }
}

static MC_SERIALIZE_MSG (gnet_interface_state_msg) = {
  .name = "vnet_gnet_interface_state",
  .serialize = serialize_gnet_interface_state_msg,
  .unserialize = unserialize_gnet_interface_state_msg,
};
#endif

static clib_error_t * gnet_init (vlib_main_t * vm)
{
  gnet_main_t * gm = &gnet_main;

  gm->vlib_main = vm;
  gnet_setup_node (vm, gnet_input_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (gnet_init);
