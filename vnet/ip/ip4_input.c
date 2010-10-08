/*
 * ip/ip4_input.c: IP v4 input node
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
} ip4_input_trace_t;

static u8 * format_ip4_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ip4_input_trace_t * t = va_arg (*va, ip4_input_trace_t *);

  s = format (s, "%U",
	      format_ip4_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

typedef enum {
  IP4_INPUT_NEXT_DROP,
  IP4_INPUT_NEXT_PUNT,
  IP4_INPUT_NEXT_LOOKUP,
  IP4_INPUT_N_NEXT,
} ip4_input_next_t;

#define vlib_validate_enqueue_two_buffers(vm,node,next_index,to_next,n_left_to_next,bi0,bi1,next0,next1) \
do {									\
  int enqueue_code = (next0 != next_index) + 2*(next1 != next_index);	\
									\
  if (PREDICT_FALSE (enqueue_code != 0))				\
    {									\
      switch (enqueue_code)						\
	{								\
	case 1:								\
	  /* A B A */							\
	  to_next[-2] = bi1;						\
	  to_next -= 1;							\
	  n_left_to_next += 1;						\
	  vlib_set_next_frame_buffer (vm, node, next0, bi0);		\
	  break;							\
									\
	case 2:								\
	  /* A A B */							\
	  to_next -= 1;							\
	  n_left_to_next += 1;						\
	  vlib_set_next_frame_buffer (vm, node, next1, bi1);		\
	  break;							\
									\
	case 3:								\
	  /* A B B or A B C */						\
	  to_next -= 2;							\
	  n_left_to_next += 2;						\
	  vlib_set_next_frame_buffer (vm, node, next0, bi0);		\
	  vlib_set_next_frame_buffer (vm, node, next1, bi1);		\
	  if (next0 == next1)						\
	    {								\
	      vlib_put_next_frame (vm, node, next_index,		\
				   n_left_to_next);			\
	      next_index = next1;					\
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next); \
	    }								\
	}								\
    }									\
} while (0)

/* Validate IP v4 packets and pass them either to forwarding code
   or drop/punt exception packets. */
always_inline uword
ip4_input_inline (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame,
		  int verify_checksum)
{
  ip4_main_t * im = &ip4_main;
  u32 n_left_from, * from, * to_next;
  ip4_input_next_t next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip4_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (ip4_input_trace_t));

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip4_header_t * ip0, * ip1;
	  ip_buffer_opaque_t * i0, * i1;
	  u32 sw_if_index0, pi0, ip_len0, cur_len0, next0, error0;
	  u32 sw_if_index1, pi1, ip_len1, cur_len1, next1, error1;
	  i32 len_diff0, len_diff1;

	  to_next[0] = pi0 = from[0];
	  to_next[1] = pi1 = from[1];
	  from += 2;
	  to_next += 2;
	  n_left_from -= 2;
	  n_left_to_next -= 2;

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  sw_if_index0 = p0->sw_if_index[VLIB_RX];
	  sw_if_index1 = p1->sw_if_index[VLIB_RX];

	  i0->current_config_index = vec_elt (im->config_index_by_sw_if_index[VLIB_RX], sw_if_index0);
	  i1->current_config_index = vec_elt (im->config_index_by_sw_if_index[VLIB_RX], sw_if_index1);

	  vnet_get_config_data (&im->config_mains[VLIB_RX],
				&i0->current_config_index,
				&next0,
				/* # bytes of config data */ 0);
	  vnet_get_config_data (&im->config_mains[VLIB_RX],
				&i1->current_config_index,
				&next1,
				/* # bytes of config data */ 0);

	  error0 = error1 = IP4_ERROR_NONE;

	  /* Punt packets with options. */
	  error0 = (ip0->ip_version_and_header_length & 0xf) != 5 ? IP4_ERROR_OPTIONS : error0;
	  error1 = (ip1->ip_version_and_header_length & 0xf) != 5 ? IP4_ERROR_OPTIONS : error1;

	  /* Version != 4?  Drop it. */
	  error0 = (ip0->ip_version_and_header_length >> 4) != 4 ? IP4_ERROR_VERSION : error0;
	  error1 = (ip1->ip_version_and_header_length >> 4) != 4 ? IP4_ERROR_VERSION : error1;

	  /* Verify header checksum. */
	  if (verify_checksum)
	    {
	      ip_csum_t sum0, sum1;

	      ip4_partial_header_checksum_x1 (ip0, sum0);
	      ip4_partial_header_checksum_x1 (ip1, sum1);

	      error0 = 0xffff != ip_csum_fold (sum0) ? IP4_ERROR_BAD_CHECKSUM : error0;
	      error1 = 0xffff != ip_csum_fold (sum1) ? IP4_ERROR_BAD_CHECKSUM : error1;
	    }

	  /* Drop fragmentation offset 1 packets. */
	  error0 = ip4_get_fragment_offset (ip0) == 1 ? IP4_ERROR_FRAGMENT_OFFSET_ONE : error0;
	  error1 = ip4_get_fragment_offset (ip1) == 1 ? IP4_ERROR_FRAGMENT_OFFSET_ONE : error1;

	  /* TTL <= 1? Drop it. */
	  error0 = ip0->ttl <= 1 ? IP4_ERROR_TIME_EXPIRED : error0;
	  error1 = ip1->ttl <= 1 ? IP4_ERROR_TIME_EXPIRED : error1;

	  /* Verify lengths. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);
	  ip_len1 = clib_net_to_host_u16 (ip1->length);

	  /* IP length must be at least minimal IP header. */
	  error0 = ip_len0 < sizeof (ip0[0]) ? IP4_ERROR_TOO_SHORT : error0;
	  error1 = ip_len1 < sizeof (ip1[0]) ? IP4_ERROR_TOO_SHORT : error1;

	  cur_len0 = vlib_buffer_length_in_chain2 (vm, p0, pi0);
	  cur_len1 = vlib_buffer_length_in_chain2 (vm, p1, pi1);

	  len_diff0 = cur_len0 - ip_len0;
	  len_diff1 = cur_len1 - ip_len1;

	  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;
	  error1 = len_diff1 < 0 ? IP4_ERROR_BAD_LENGTH : error1;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

	  next0 = (error0 != IP4_ERROR_NONE
		   ? (error0 == IP4_ERROR_OPTIONS
		      ? IP4_INPUT_NEXT_PUNT
		      : IP4_INPUT_NEXT_DROP)
		   : next0);
	  next1 = (error1 != IP4_ERROR_NONE
		   ? (error1 == IP4_ERROR_OPTIONS
		      ? IP4_INPUT_NEXT_PUNT
		      : IP4_INPUT_NEXT_DROP)
		   : next1);

	  vlib_validate_enqueue_two_buffers (vm, node, next_index,
					     to_next, n_left_to_next,
					     pi0, pi1, next0, next1);
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  u32 sw_if_index0, pi0, ip_len0, cur_len0, next0, error0;
	  i32 len_diff0;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_buffer (vm, pi0);
	  ip0 = vlib_buffer_get_current (p0);
	  i0 = vlib_get_buffer_opaque (p0);

	  sw_if_index0 = p0->sw_if_index[VLIB_RX];
	  i0->current_config_index = vec_elt (im->config_index_by_sw_if_index[VLIB_RX], sw_if_index0);
	  vnet_get_config_data (&im->config_mains[VLIB_RX],
				&i0->current_config_index,
				&next0,
				/* # bytes of config data */ 0);

	  error0 = IP4_ERROR_NONE;

	  /* Punt packets with options. */
	  error0 = (ip0->ip_version_and_header_length & 0xf) != 5 ? IP4_ERROR_OPTIONS : error0;

	  /* Version != 4?  Drop it. */
	  error0 = (ip0->ip_version_and_header_length >> 4) != 4 ? IP4_ERROR_VERSION : error0;

	  /* Verify header checksum. */
	  if (verify_checksum)
	    {
	      ip_csum_t sum0;

	      ip4_partial_header_checksum_x1 (ip0, sum0);
	      error0 = 0xffff != ip_csum_fold (sum0) ? IP4_ERROR_BAD_CHECKSUM : error0;
	    }

	  /* Drop fragmentation offset 1 packets. */
	  error0 = ip4_get_fragment_offset (ip0) == 1 ? IP4_ERROR_FRAGMENT_OFFSET_ONE : error0;

	  /* TTL <= 1? Drop it. */
	  error0 = ip0->ttl <= 1 ? IP4_ERROR_TIME_EXPIRED : error0;

	  /* Verify lengths. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);

	  /* IP length must be at least minimal IP header. */
	  error0 = ip_len0 < sizeof (ip0[0]) ? IP4_ERROR_TOO_SHORT : error0;

	  cur_len0 = vlib_buffer_length_in_chain2 (vm, p0, pi0);
	  len_diff0 = cur_len0 - ip_len0;
	  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;

	  p0->error = error_node->errors[error0];
	  next0 = (error0 != IP4_ERROR_NONE
		   ? (error0 == IP4_ERROR_OPTIONS
		      ? IP4_INPUT_NEXT_PUNT
		      : IP4_INPUT_NEXT_DROP)
		   : next0);

	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);
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

static uword
ip4_input (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  return ip4_input_inline (vm, node, frame, /* verify_checksum */ 1);
}

static uword
ip4_input_no_checksum (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       vlib_frame_t * frame)
{
  return ip4_input_inline (vm, node, frame, /* verify_checksum */ 0);
}

static char * ip4_error_strings[] = {
#define _(sym,string) string,
  foreach_ip4_error
#undef _
};

VLIB_REGISTER_NODE (ip4_input_node) = {
  .function = ip4_input,
  .name = "ip4-input",
  .vector_size = sizeof (u32),

  .n_errors = IP4_N_ERROR,
  .error_strings = ip4_error_strings,

  .n_next_nodes = IP4_INPUT_N_NEXT,
  .next_nodes = {
    [IP4_INPUT_NEXT_DROP] = "error-drop",
    [IP4_INPUT_NEXT_PUNT] = "error-punt",
    [IP4_INPUT_NEXT_LOOKUP] = "ip4-lookup",
  },

  .format_buffer = format_ip4_header,
  .format_trace = format_ip4_input_trace,
};

static VLIB_REGISTER_NODE (ip4_input_no_checksum_node) = {
  .function = ip4_input_no_checksum,
  .name = "ip4-input-no-csum",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP4_INPUT_N_NEXT,
  .next_nodes = {
    [IP4_INPUT_NEXT_DROP] = "error-drop",
    [IP4_INPUT_NEXT_PUNT] = "error-punt",
    [IP4_INPUT_NEXT_LOOKUP] = "ip4-lookup",
  },

  .format_buffer = format_ip4_header,
  .format_trace = format_ip4_input_trace,
};

static clib_error_t * ip4_init (vlib_main_t * vm)
{
  clib_error_t * error;

  ethernet_register_input_type (vm, ETHERNET_TYPE_IP4,
				ip4_input_node.index);
  ppp_register_input_protocol (vm, PPP_PROTOCOL_ip4,
			       ip4_input_node.index);
  hdlc_register_input_protocol (vm, HDLC_PROTOCOL_ip4,
				ip4_input_node.index);

  {
    pg_node_t * pn;
    pn = pg_get_node (ip4_input_node.index);
    pn->unformat_edit = unformat_pg_ip4_header;
    pn = pg_get_node (ip4_input_no_checksum_node.index);
    pn->unformat_edit = unformat_pg_ip4_header;
  }

  if ((error = vlib_call_init_function (vm, ip4_cli_init)))
    return error;

  /* Set flow hash to something non-zero. */
  ip4_main.flow_hash_seed = 0xdeadbeef;

  /* Default TTL for packets we generate. */
  ip4_main.host_config.ttl = 64;

  return error;
}

VLIB_INIT_FUNCTION (ip4_init);
