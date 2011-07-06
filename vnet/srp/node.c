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

#define foreach_srp_error					\
  _ (NONE, "no error")						\
  _ (UNKNOWN_MODE, "unknown mode in SRP header")		\
  _ (KEEP_ALIVE_DROPPED, "v1 keep alive mode in SRP header")	\
  _ (UNKNOWN_CONTROL, "unknown control packet")

typedef enum {
#define _(n,s) SRP_ERROR_##n,
  foreach_srp_error
#undef _
  SRP_N_ERROR,
} srp_error_t;

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
srp_control_input (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * from_frame)
{
  return vlib_error_drop_buffers (vm, node,
				  vlib_frame_vector_args (from_frame),
				  /* stride */ 1,
				  from_frame->n_vectors,
				  /* next index */ 0,
				  srp_input_node.index,
				  SRP_ERROR_UNKNOWN_CONTROL);
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
};

static clib_error_t * srp_init (vlib_main_t * vm)
{
  srp_main_t * sm = &srp_main;

  sm->default_data_ttl = 255;
  srp_setup_node (vm, srp_input_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (srp_init);
