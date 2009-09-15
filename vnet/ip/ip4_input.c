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

/* Validate IP v4 packets and pass them either to forwarding code
   or drop/punt exception packets. */
static always_inline uword
ip4_input_inline (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame,
		  int verify_checksum)
{
  u32 n_left_from, * from, * to_next;
  ip4_input_next_t next_index;

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
	  u32 pi0, sw_if_index0, ip_len0, l2_len0;
	  u32 pi1, sw_if_index1, ip_len1, l2_len1;
	  i32 len_diff0, len_diff1;
	  u8 is_slow_path, error0, error1;

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
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = (void *) (p0->data + p0->current_data);
	  ip1 = (void *) (p1->data + p1->current_data);

	  /* Lookup forwarding table by input interface. */
	  sw_if_index0 = p0->sw_if_index[VLIB_RX];
	  sw_if_index1 = p1->sw_if_index[VLIB_RX];

	  error0 = error1 = IP4_ERROR_NONE;
	  is_slow_path = next_index != IP4_INPUT_NEXT_LOOKUP;

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

	      sum0 = clib_mem_unaligned (&ip0->data64[0], u64);
	      sum1 = clib_mem_unaligned (&ip1->data64[0], u64);

	      sum0 = ip_csum_with_carry
		(sum0, clib_mem_unaligned (&ip0->data64[1], u64));
	      sum1 = ip_csum_with_carry
		(sum1, clib_mem_unaligned (&ip1->data64[1], u64));

	      sum0 = ip_csum_with_carry
		(sum0, clib_mem_unaligned (&ip0->data32[0], u32));
	      sum1 = ip_csum_with_carry
		(sum1, clib_mem_unaligned (&ip1->data32[0], u32));

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

	  /* Take slow path if current buffer length
	     is not equal to packet length. */
	  is_slow_path += (p0->flags | p1->flags) & VLIB_BUFFER_NEXT_PRESENT;

	  l2_len0 = p0->current_length;
	  l2_len1 = p1->current_length;

	  len_diff0 = l2_len0 - ip_len0;
	  len_diff1 = l2_len1 - ip_len1;

	  /* L2 length must be >= L3 length. */
	  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;
	  error1 = len_diff1 < 0 ? IP4_ERROR_BAD_LENGTH : error1;

	  /* Trim padding at end of packet. */
	  p0->current_length = l2_len0 - len_diff0;
	  p1->current_length = l2_len1 - len_diff1;

	  is_slow_path += error0 != IP4_ERROR_NONE || error1 != IP4_ERROR_NONE;

	  if (PREDICT_FALSE (is_slow_path))
	    {
	      ip4_input_next_t next0, next1;

	      to_next -= 2;
	      n_left_to_next += 2;

	      /* Re-do length check for packets with multiple buffers. */
	      if (p0->flags & VLIB_BUFFER_NEXT_PRESENT)
		{
		  l2_len0 = vlib_buffer_n_bytes_in_chain (vm, pi0);
		  len_diff0 = l2_len0 - ip_len0;
		  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;
		  p0->current_length = error0 ? l2_len0 : ip_len0;
		}
	      if (p1->flags & VLIB_BUFFER_NEXT_PRESENT)
		{
		  l2_len1 = vlib_buffer_n_bytes_in_chain (vm, pi1);
		  len_diff1 = l2_len1 - ip_len1;
		  error1 = len_diff1 < 0 ? IP4_ERROR_BAD_LENGTH : error1;
		  p1->current_length = error1 ? l2_len1 : ip_len1;
		}

	      next0 = (error0 == IP4_ERROR_NONE
		       ? IP4_INPUT_NEXT_LOOKUP
		       : (error0 == IP4_ERROR_OPTIONS
			  ? IP4_INPUT_NEXT_PUNT
			  : IP4_INPUT_NEXT_DROP));
	      next1 = (error1 == IP4_ERROR_NONE
		       ? IP4_INPUT_NEXT_LOOKUP
		       : (error1 == IP4_ERROR_OPTIONS
			  ? IP4_INPUT_NEXT_PUNT
			  : IP4_INPUT_NEXT_DROP));

	      if (next0 != next_index)
		{
		  vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		  vlib_get_next_frame (vm, node, next0, to_next, n_left_to_next);
		  next_index = next0;
		}

	      to_next[0] = pi0;
	      to_next[1] = vlib_error_set (ip4_input_node.index, error0);
	      to_next += 1 + (error0 != IP4_ERROR_NONE);
	      n_left_to_next -= 1;

	      if (next1 != next_index)
		{
		  vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		  vlib_get_next_frame (vm, node, next1, to_next, n_left_to_next);
		  next_index = next1;
		}

	      to_next[0] = pi1;
	      to_next[1] = vlib_error_set (ip4_input_node.index, error1);
	      to_next += 1 + (error1 != IP4_ERROR_NONE);
	      n_left_to_next -= 1;
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  u32 pi0, sw_if_index0, ip_len0, l2_len0;
	  i32 len_diff0;
	  u8 error0, is_slow_path;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_buffer (vm, pi0);
	  ip0 = vlib_buffer_get_current (p0);

	  /* Lookup forwarding table by input interface. */
	  sw_if_index0 = p0->sw_if_index[VLIB_RX];

	  error0 = IP4_ERROR_NONE;
	  is_slow_path = next_index != IP4_INPUT_NEXT_LOOKUP;

	  /* Punt packets with options. */
	  error0 = (ip0->ip_version_and_header_length & 0xf) != 5 ? IP4_ERROR_OPTIONS : error0;

	  /* Version != 4?  Drop it. */
	  error0 = (ip0->ip_version_and_header_length >> 4) != 4 ? IP4_ERROR_VERSION : error0;

	  /* Verify header checksum. */
	  if (verify_checksum)
	    {
	      ip_csum_t sum0;

	      sum0 = clib_mem_unaligned (&ip0->data64[0], u64);

	      sum0 = ip_csum_with_carry
		(sum0, clib_mem_unaligned (&ip0->data64[1], u64));

	      sum0 = ip_csum_with_carry
		(sum0, clib_mem_unaligned (&ip0->data32[0], u32));

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

	  /* Take slow path if current buffer length
	     is not equal to packet length. */
	  is_slow_path += p0->flags & VLIB_BUFFER_NEXT_PRESENT;

	  l2_len0 = p0->current_length;
	  len_diff0 = l2_len0 - ip_len0;
	  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;
	  p0->current_length -= len_diff0;

	  is_slow_path += error0 != IP4_ERROR_NONE;

	  if (PREDICT_FALSE (is_slow_path))
	    {
	      ip4_input_next_t next0;

	      to_next -= 1;
	      n_left_to_next += 1;

	      /* Re-do length check for packets with multiple buffers. */
	      if (p0->flags & VLIB_BUFFER_NEXT_PRESENT)
		{
		  l2_len0 = vlib_buffer_n_bytes_in_chain (vm, pi0);
		  len_diff0 = l2_len0 - ip_len0;
		  error0 = len_diff0 < 0 ? IP4_ERROR_BAD_LENGTH : error0;
		  p0->current_length = error0 ? l2_len0 : ip_len0;
		}

	      next0 = (error0 == IP4_ERROR_NONE
		       ? IP4_INPUT_NEXT_LOOKUP
		       : (error0 == IP4_ERROR_OPTIONS
			  ? IP4_INPUT_NEXT_PUNT
			  : IP4_INPUT_NEXT_DROP));

	      if (next0 != next_index)
		{
		  vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		  vlib_get_next_frame (vm, node, next0, to_next, n_left_to_next);
		  next_index = next0;
		}

	      to_next[0] = pi0;
	      to_next[1] = vlib_error_set (ip4_input_node.index, error0);
	      to_next += 1 + (error0 != IP4_ERROR_NONE);
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
  ethernet_register_input_type (vm, ETHERNET_TYPE_IP,
				ip4_input_node.index);

  {
    pg_node_t * pn;
    pn = pg_get_node (ip4_input_node.index);
    pn->unformat_edit = unformat_pg_ip4_header;
    pn = pg_get_node (ip4_input_no_checksum_node.index);
    pn->unformat_edit = unformat_pg_ip4_header;
  }

  return 0;
}

VLIB_INIT_FUNCTION (ip4_init);
