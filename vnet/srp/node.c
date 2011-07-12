/*
 * node.c: srp packet processing
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

#include <vlib/vlib.h>
#include <vnet/ip/ip_packet.h>	/* for ip_csum_fold */
#include <vnet/srp/srp.h>

typedef struct {
  u8 packet_data[32];
} srp_input_trace_t;

static u8 * format_srp_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  srp_input_trace_t * t = va_arg (*va, srp_input_trace_t *);

  s = format (s, "%U", format_srp_header, t->packet_data);

  return s;
}

typedef enum {
  SRP_INPUT_NEXT_ERROR,
  SRP_INPUT_NEXT_ETHERNET_INPUT,
  SRP_INPUT_NEXT_CONTROL,
  SRP_INPUT_N_NEXT,
} srp_input_next_t;

typedef struct {
  u8 next_index;
  u8 buffer_advance;
  u16 error;
} srp_input_disposition_t;

static srp_input_disposition_t srp_input_disposition_by_mode[8] = {
  [SRP_MODE_reserved0] = {
    .next_index = SRP_INPUT_NEXT_ERROR,
    .error = SRP_ERROR_UNKNOWN_MODE,
  },
  [SRP_MODE_reserved1] = {
    .next_index = SRP_INPUT_NEXT_ERROR,
    .error = SRP_ERROR_UNKNOWN_MODE,
  },
  [SRP_MODE_reserved2] = {
    .next_index = SRP_INPUT_NEXT_ERROR,
    .error = SRP_ERROR_UNKNOWN_MODE,
  },
  [SRP_MODE_reserved3] = {
    .next_index = SRP_INPUT_NEXT_ERROR,
    .error = SRP_ERROR_UNKNOWN_MODE,
  },
  [SRP_MODE_keep_alive] = {
    .next_index = SRP_INPUT_NEXT_ERROR,
    .error = SRP_ERROR_KEEP_ALIVE_DROPPED,
  },
  [SRP_MODE_data] = {
    .next_index = SRP_INPUT_NEXT_ETHERNET_INPUT,
    .buffer_advance = sizeof (srp_header_t),
  },
  [SRP_MODE_control_pass_to_host] = {
    .next_index = SRP_INPUT_NEXT_CONTROL,
  },
  [SRP_MODE_control_locally_buffered_for_host] = {
    .next_index = SRP_INPUT_NEXT_CONTROL,
  },
};

static uword
srp_input (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * from_frame)
{
  u32 n_left_from, next_index, * from, * to_next;

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (srp_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  u32 bi0, bi1;
	  vlib_buffer_t * b0, * b1;
	  u8 next0, next1, error0, error1;
	  srp_header_t * s0, * s1;
	  srp_input_disposition_t * d0, * d1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * b2, * b3;

	    b2 = vlib_get_buffer (vm, from[2]);
	    b3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (b2, LOAD);
	    vlib_prefetch_buffer_header (b3, LOAD);

	    CLIB_PREFETCH (b2->data, sizeof (srp_header_t), LOAD);
	    CLIB_PREFETCH (b3->data, sizeof (srp_header_t), LOAD);
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

	  d0 = srp_input_disposition_by_mode + s0->mode;
	  d1 = srp_input_disposition_by_mode + s1->mode;

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
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  u8 next0, error0;
	  srp_header_t * s0;
	  srp_input_disposition_t * d0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  s0 = (void *) (b0->data + b0->current_data);

	  d0 = srp_input_disposition_by_mode + s0->mode;

	  next0 = d0->next_index;

	  error0 = d0->error;

	  vlib_buffer_advance (b0, d0->buffer_advance);

	  b0->error = node->errors[error0];

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static char * srp_error_strings[] = {
#define _(f,s) s,
  foreach_srp_error
#undef srp_error
};

static VLIB_REGISTER_NODE (srp_input_node) = {
  .function = srp_input,
  .name = "srp-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_errors = SRP_N_ERROR,
  .error_strings = srp_error_strings,

  .n_next_nodes = SRP_INPUT_N_NEXT,
  .next_nodes = {
    [SRP_INPUT_NEXT_ERROR] = "error-drop",
    [SRP_INPUT_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [SRP_INPUT_NEXT_CONTROL] = "srp-control",
  },

  .format_buffer = format_srp_header_with_length,
  .format_trace = format_srp_input_trace,
  .unformat_buffer = unformat_srp_header,
};

static uword
srp_topology_packet (vlib_main_t * vm, u32 sw_if_index, u8 ** contents)
{
  vlib_hw_interface_t * hi = vlib_get_sup_hw_interface (vm, sw_if_index);
  srp_topology_header_t * t;
  srp_topology_mac_binding_t * mb;
  u32 nb, nmb;

  t = (void *) *contents;

  nb = clib_net_to_host_u16 (t->n_bytes_of_data_that_follows);
  nmb = (nb - sizeof (t->originator_address)) / sizeof (mb[0]);
  if (vec_len (*contents) < sizeof (t[0]) + nmb * sizeof (mb[0]))
    return SRP_ERROR_TOPOLOGY_BAD_LENGTH;

  /* Fill in our source MAC address. */
  memcpy (t->ethernet.src_address, hi->hw_address, vec_len (hi->hw_address));

  /* Make space for our MAC binding. */
  vec_resize (*contents, sizeof (srp_topology_mac_binding_t));
  t = (void *) *contents;
  t->n_bytes_of_data_that_follows = clib_host_to_net_u16 (nb + sizeof (mb[0]));

  mb = t->bindings + nmb;

  mb->flags =
    ((t->srp.is_inner_ring ? SRP_TOPOLOGY_MAC_BINDING_FLAG_IS_INNER_RING : 0)
     | (/* is wrapped FIXME */ 0));
  memcpy (mb->address, hi->hw_address, vec_len (hi->hw_address));

  t->control.checksum
    = ~ip_csum_fold (ip_incremental_checksum (0, &t->control,
					      vec_len (*contents) - STRUCT_OFFSET_OF (srp_generic_control_header_t, control)));

  {
    vlib_frame_t * f = vlib_get_frame_to_node (vm, hi->output_node_index);
    vlib_buffer_t * b;
    u32 * to_next = vlib_frame_vector_args (f);
    u32 bi;

    bi = vlib_buffer_add_data (vm, VLIB_BUFFER_DEFAULT_FREE_LIST_INDEX,
			       /* buffer to append to */ 0,
			       *contents, vec_len (*contents));
    b = vlib_get_buffer (vm, bi);
    b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX] = sw_if_index;
    to_next[0] = bi;
    f->n_vectors = 1;
    vlib_put_frame_to_node (vm, hi->output_node_index, f);
  }

  return SRP_ERROR_CONTROL_PACKETS_PROCESSED;
}

typedef uword (srp_control_handler_function_t) (vlib_main_t * vm,
						u32 sw_if_index,
						u8 ** contents);

static uword
srp_control_input (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * from_frame)
{
  u32 n_left_from, next_index, * from, * to_next;
  vlib_node_runtime_t * error_node;
  static u8 * contents;

  error_node = vlib_node_get_runtime (vm, srp_input_node.index);

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node,
				   from,
				   n_left_from,
				   sizeof (from[0]),
				   sizeof (srp_input_trace_t));

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0, l2_len0, l3_len0;
	  vlib_buffer_t * b0;
	  u8 next0, error0;
	  srp_generic_control_header_t * s0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  s0 = (void *) (b0->data + b0->current_data);
	  l2_len0 = vlib_buffer_length_in_chain (vm, b0);
	  l3_len0 = l2_len0 - STRUCT_OFFSET_OF (srp_generic_control_header_t, control);

	  error0 = SRP_ERROR_CONTROL_PACKETS_PROCESSED;

	  error0 = s0->control.version != 0 ? SRP_ERROR_CONTROL_VERSION_NON_ZERO : error0;

	  {
	    u16 save0 = s0->control.checksum;
	    u16 computed0;
	    s0->control.checksum = 0;
	    computed0 = ~ip_csum_fold (ip_incremental_checksum (0, &s0->control, l3_len0));
	    error0 = save0 != computed0 ? SRP_ERROR_CONTROL_BAD_CHECKSUM : error0;
	  }

	  if (error0 == SRP_ERROR_CONTROL_PACKETS_PROCESSED)
	    {
	      static srp_control_handler_function_t * t[SRP_N_CONTROL_PACKET_TYPE] = {
		[SRP_CONTROL_PACKET_TYPE_topology] = srp_topology_packet,
	      };
	      srp_control_handler_function_t * f;

	      f = 0;
	      if (s0->control.type < ARRAY_LEN (t))
		f = t[s0->control.type];

	      if (f)
		{
		  vec_validate (contents, l2_len0 - 1);
		  vlib_buffer_contents (vm, bi0, contents);
		  error0 = f (vm, b0->sw_if_index[VLIB_RX], &contents);
		}
	      else
		error0 = SRP_ERROR_UNKNOWN_CONTROL;
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

static VLIB_REGISTER_NODE (srp_control_input_node) = {
  .function = srp_control_input,
  .name = "srp-control",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },

  .format_buffer = format_srp_header_with_length,
  .format_trace = format_srp_input_trace,
  .unformat_buffer = unformat_srp_header,
};

static clib_error_t * srp_init (vlib_main_t * vm)
{
  srp_main_t * sm = &srp_main;

  sm->default_data_ttl = 255;
  srp_setup_node (vm, srp_input_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (srp_init);
