/*
 * ip/ip6_input.c: IP v6 input node
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
#include <vnet/ethernet/ethernet.h>
#include <vnet/ppp/ppp.h>
#include <vnet/hdlc/hdlc.h>

typedef struct {
  u8 packet_data[64];
} ip6_input_trace_t;

static u8 * format_ip6_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ip6_input_trace_t * t = va_arg (*va, ip6_input_trace_t *);

  s = format (s, "%U",
	      format_ip6_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

typedef enum {
  IP6_INPUT_NEXT_DROP,
  IP6_INPUT_NEXT_LOOKUP,
  IP6_INPUT_N_NEXT,
} ip6_input_next_t;

/* Validate IP v6 packets and pass them either to forwarding code
   or drop exception packets. */
static uword
ip6_input (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  u32 n_left_from, * from, * to_next;
  ip6_input_next_t next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip6_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (ip6_input_trace_t));

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip6_header_t * ip0, * ip1;
	  ip6_input_next_t next0, next1;
	  u32 pi0, pi1;
	  u8 error0, error1, enqueue_code;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->data, sizeof (ip0[0]), LOAD);
	    CLIB_PREFETCH (p3->data, sizeof (ip1[0]), LOAD);
	  }

	  pi0 = from[0];
	  pi1 = from[1];

	  to_next[0] = pi0;
	  to_next[1] = pi1;
	  from += 2;
	  to_next += 2;
	  n_left_from -= 2;
	  n_left_to_next -= 2;

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  error0 = error1 = IP6_ERROR_NONE;

	  /* Version != 6?  Drop it. */
	  error0 = (clib_net_to_host_u32 (ip0->ip_version_traffic_class_and_flow_label) >> 28) != 6 ? IP6_ERROR_VERSION : error0;
	  error1 = (clib_net_to_host_u32 (ip1->ip_version_traffic_class_and_flow_label) >> 28) != 6 ? IP6_ERROR_VERSION : error1;

	  /* TTL <= 1? Drop it. */
	  error0 = ip0->ttl <= 1 ? IP6_ERROR_TIME_EXPIRED : error0;
	  error1 = ip1->ttl <= 1 ? IP6_ERROR_TIME_EXPIRED : error1;

	  /* L2 length must be at least minimal IP header. */
	  error0 = p0->current_length < sizeof (ip0[0]) ? IP6_ERROR_TOO_SHORT : error0;
	  error1 = p1->current_length < sizeof (ip1[0]) ? IP6_ERROR_TOO_SHORT : error1;

	  next0 = error0 != IP6_ERROR_NONE ? IP6_INPUT_NEXT_DROP : IP6_INPUT_NEXT_LOOKUP;
	  next1 = error1 != IP6_ERROR_NONE ? IP6_INPUT_NEXT_DROP : IP6_INPUT_NEXT_LOOKUP;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

	  enqueue_code = (next0 != next_index) + 2*(next1 != next_index);

	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      switch (enqueue_code)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = pi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next0, pi0);
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next1, pi1);
		  break;

		case 3:
		  /* A B B or A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  vlib_set_next_frame_buffer (vm, node, next0, pi0);
		  vlib_set_next_frame_buffer (vm, node, next1, pi1);
		  if (next0 == next1)
		    {
		      vlib_put_next_frame (vm, node, next_index,
					   n_left_to_next);
		      next_index = next1;
		      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
		    }
		}
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  ip6_input_next_t next0;
	  u32 pi0;
	  u8 error0;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_buffer (vm, pi0);
	  ip0 = vlib_buffer_get_current (p0);

	  error0 = IP6_ERROR_NONE;

	  /* Version != 6?  Drop it. */
	  error0 = (clib_net_to_host_u32 (ip0->ip_version_traffic_class_and_flow_label) >> 28) != 6 ? IP6_ERROR_VERSION : error0;

	  /* TTL <= 1? Drop it. */
	  error0 = ip0->ttl <= 1 ? IP6_ERROR_TIME_EXPIRED : error0;

	  /* L2 length must be at least minimal IP header. */
	  error0 = p0->current_length < sizeof (ip0[0]) ? IP6_ERROR_TOO_SHORT : error0;

	  next0 = error0 != IP6_ERROR_NONE ? IP6_INPUT_NEXT_DROP : IP6_INPUT_NEXT_LOOKUP;

	  p0->error = error_node->errors[error0];

	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);

	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	      to_next[0] = pi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

static char * ip6_error_strings[] = {
#define _(sym,string) string,
  foreach_ip6_error
#undef _
};

VLIB_REGISTER_NODE (ip6_input_node) = {
  .function = ip6_input,
  .name = "ip6-input",
  .vector_size = sizeof (u32),

  .n_errors = IP6_N_ERROR,
  .error_strings = ip6_error_strings,

  .n_next_nodes = IP6_INPUT_N_NEXT,
  .next_nodes = {
    [IP6_INPUT_NEXT_DROP] = "error-drop",
    [IP6_INPUT_NEXT_LOOKUP] = "ip6-lookup",
  },

  .format_buffer = format_ip6_header,
  .format_trace = format_ip6_input_trace,
};

static clib_error_t * ip6_init (vlib_main_t * vm)
{
  ethernet_register_input_type (vm, ETHERNET_TYPE_IP6,
				ip6_input_node.index);
  ppp_register_input_protocol (vm, PPP_PROTOCOL_ip6,
			       ip6_input_node.index);
  hdlc_register_input_protocol (vm, HDLC_PROTOCOL_ip6,
				ip6_input_node.index);

  {
    pg_node_t * pn;
    pn = pg_get_node (ip6_input_node.index);
    pn->unformat_edit = unformat_pg_ip6_header;
  }

  /* Set flow hash to something non-zero. */
  ip6_main.flow_hash_seed = 0xdeadbeef;

  /* Default TTL for packets we generate. */
  ip6_main.host_config.ttl = 64;

  return /* no error */ 0;
}

VLIB_INIT_FUNCTION (ip6_init);
