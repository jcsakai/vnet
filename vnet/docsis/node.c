/*
 * docsis/node.c: DOCSIS packet processing
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

#include <vnet/vnet.h>
#include <vnet/docsis/docsis.h>
#include <vnet/ethernet/packet.h>

#define foreach_docsis_input_next		\
  _ (DROP, "error-drop")			\
  _ (IP4_INPUT, "ip4-input")			\
  _ (ETHERNET_INPUT, "ethernet-input")

typedef enum {
#define _(s,n) DOCSIS_INPUT_NEXT_##s,
  foreach_docsis_input_next
#undef _
  DOCSIS_INPUT_N_NEXT,
} docsis_input_next_t;

typedef struct {
  u8 packet_data[64];
} docsis_input_trace_t;

static u8 * format_docsis_input_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  docsis_input_trace_t * t = va_arg (*va, docsis_input_trace_t *);

  s = format (s, "%U", format_docsis_header_with_length, t->packet_data, sizeof (t->packet_data));

  return s;
}

/* CRC table for the CRC ITU-T V.41 0x0x1021 (x^16 + x^12 + x^15 + 1). */
u16 crc_itu_t_table[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
  0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
  0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
  0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
  0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
  0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
  0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
  0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
  0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
  0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
  0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
  0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
  0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
  0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
  0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
  0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
  0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
  0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

always_inline u16 crc_itu_t_update (u16 crc, u8 data)
{ return (crc << 8) ^ crc_itu_t_table[((crc >> 8) ^ data) & 0xff]; }

u16 docsis_header_crc_itu_t (u16 crc, const u8 * buffer, size_t len)
{
  while (len--)
    crc = crc_itu_t_update (crc, *buffer++);
  return crc;
}

static docsis_node_error_t
docsis_input_unknown_control (docsis_main_t * dm, vlib_buffer_t * b)
{ return DOCSIS_ERROR_UNKNOWN_CONTROL_PACKET_TYPE; }

static docsis_node_error_t
docsis_input_unexpected_control (docsis_main_t * dm, vlib_buffer_t * b)
{ return DOCSIS_ERROR_UNEXPECTED_CONTROL_PACKET_TYPE_FOR_ROLE; }

static docsis_node_error_t
docsis_input_unknown_management (docsis_main_t * dm, vlib_buffer_t * b)
{ return DOCSIS_ERROR_UNKNOWN_MANAGEMENT_PACKET_TYPE; }

static docsis_node_error_t
docsis_input_unexpected_management (docsis_main_t * dm, vlib_buffer_t * b)
{ return DOCSIS_ERROR_UNEXPECTED_MANAGEMENT_PACKET_TYPE_FOR_ROLE; }

static never_inline docsis_input_next_t
docsis_input_slow_path (vlib_main_t * vm, vlib_node_runtime_t * node, vlib_buffer_t * b0)
{
  docsis_main_t * dm = &docsis_main;
  docsis_packet_t * d0;
  docsis_input_next_t next0;
  docsis_packet_header_t h0;
  ethernet_header_t * e0;
  u8 error0, is_control0, length_is_valid0, is_ip40;
  u32 n_bytes_header0, skip_len0;

  d0 = (void *) (b0->data + b0->current_data);

  error0 = DOCSIS_ERROR_NONE;
  next0 = DOCSIS_INPUT_NEXT_DROP;

  /* Bye bye ATM packet. */
  h0 = d0->generic.header;
  if (h0.packet_type == DOCSIS_PACKET_TYPE_atm)
    {
      error0 = DOCSIS_ERROR_ATM_DROP;
      goto done;
    }

  is_control0 = h0.packet_type == DOCSIS_PACKET_TYPE_control;
  n_bytes_header0 = sizeof (d0->request_frame);
  length_is_valid0 = 1;

  if (is_control0)
    {
      switch (h0.control_packet_type)
	{
	case DOCSIS_CONTROL_PACKET_TYPE_queue_depth_request:
	  /* queue depth header has 1 more byte. */
	  n_bytes_header0 += 1;
	case DOCSIS_CONTROL_PACKET_TYPE_timing_management:
	case DOCSIS_CONTROL_PACKET_TYPE_request_frame:
	case DOCSIS_CONTROL_PACKET_TYPE_concatenation:
	  if (h0.extended_header_present)
	    {
	      error0 = DOCSIS_ERROR_EXTENDED_HEADER_NOT_ALLOWED;
	      goto done;
	    }
	  length_is_valid0 = h0.control_packet_type == DOCSIS_CONTROL_PACKET_TYPE_timing_management;
	  break;

	case DOCSIS_CONTROL_PACKET_TYPE_management:
	case DOCSIS_CONTROL_PACKET_TYPE_fragmentation:
	  n_bytes_header0 += d0->generic.n_bytes_in_extended_header;
	  break;

	default:
	  error0 = DOCSIS_ERROR_UNKNOWN_CONTROL_PACKET_TYPE;
	  goto done;
	}
    }
  else
    n_bytes_header0 += d0->generic.n_bytes_in_extended_header;

  /* Check header length. */
  if (length_is_valid0
      && (clib_net_to_host_u16 (d0->generic.n_bytes_in_payload_plus_extended_header)
	  + sizeof (d0->generic)
	  != vlib_buffer_length_in_chain (vm, b0)))
    {
      error0 = DOCSIS_ERROR_WRONG_LENGTH;
      goto done;
    }

  /* Check header CRC. */
  {
    u32 i;
    u16 computed_crc0, expected_header_crc0;

    computed_crc0 = crc_itu_t_update (0, d0->as_u8[0]);
    for (i = 1; i < n_bytes_header0 - sizeof (d0->generic.expected_header_crc); i++)
      computed_crc0 = crc_itu_t_update (computed_crc0, d0->as_u8[i]);

    expected_header_crc0 = (d0->as_u8[i + 0] << 8) | d0->as_u8[i + 1];

    if (expected_header_crc0 != computed_crc0)
      {
	error0 = DOCSIS_ERROR_WRONG_HEADER_CRC;
	goto done;
      }
  }

  /* Handle extended header if present. */
  if (h0.extended_header_present && d0->generic.n_bytes_in_extended_header > 0)
    {
      docsis_extended_header_tlv_t * t, * t_end;
      t = (void *) d0->generic.extended_header;
      t_end = (void *) t + d0->generic.n_bytes_in_extended_header;
      while (t < t_end)
	{
	  static u8 len_tab[16] = {
#define _(f,n,len) [n] = (len) + 1,
	    foreach_docsis_extended_header_tlv_type
#undef _
	  };
	  u8 lt = len_tab[t->type];
	  if (lt == 0)
	    {
	      error0 = DOCSIS_ERROR_EXTENDED_HEADER_TLV_UNKNOWN;
	      goto done;
	    }
	  if (lt != 1 && t->n_value_bytes != (lt - 1))
	    {
	      error0 = DOCSIS_ERROR_EXTENDED_HEADER_TLV_BAD_LENGTH;
	      goto done;
	    }

	  ASSERT (0);
	  switch (t->type)
	    {
	    default: break;
	    }

	  t = (void *) (t->value + t->n_value_bytes);
	}
    }

  if (is_control0)
    {
      if (h0.control_packet_type == DOCSIS_CONTROL_PACKET_TYPE_management
	  || h0.control_packet_type == DOCSIS_CONTROL_PACKET_TYPE_timing_management)
	{
	  docsis_management_packet_t * m0;

	  /* Skip to ethernet/llc/management header. */
	  m0 = ((void *) d0->generic.payload + 
		+ (d0->generic.header.extended_header_present ? d0->generic.n_bytes_in_extended_header : 0));
	  vlib_buffer_advance (b0, (void *) m0 - (void *) d0);

	  error0 = DOCSIS_ERROR_UNKNOWN_MANAGEMENT_PACKET_TYPE;
	  if (m0->type < ARRAY_LEN (dm->input_functions_for_role[dm->role].management))
	    error0 = dm->input_functions_for_role[dm->role].management[m0->type] (dm, b0);
	}
      else
	error0 = dm->input_functions_for_role[dm->role].control[h0.control_packet_type] (dm, b0);
    }

  else
    {
      /* Skip to ethernet header. */
      e0 = (void *) d0->generic.payload;

      is_ip40 = e0->type == clib_host_to_net_u16 (ETHERNET_TYPE_IP4);

      next0 = is_ip40 ? DOCSIS_INPUT_NEXT_IP4_INPUT : next0;

      skip_len0 = is_ip40 ? sizeof (e0[0]) : 0;

      /* Skip over DOCSIS mac header. */
      skip_len0 += (void *) e0 - (void*) d0;

      b0->current_data += skip_len0;

      /* Don't include ethernet CRC in packet length. */
      b0->current_length -= skip_len0 + sizeof (u32);

      next0 = error0 != DOCSIS_ERROR_NONE ? DOCSIS_INPUT_NEXT_DROP : next0;
    }

 done:
  b0->error = node->errors[error0];

  return next0;
}

static uword
docsis_input (vlib_main_t * vm,
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
				   sizeof (docsis_input_trace_t));

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
	  docsis_packet_t * d0, * d1;
	  docsis_input_next_t next0, next1;
	  ethernet_header_t * e0, * e1;
	  u16 crc0, crc1;
	  u8 error0, error1, fast_path0, fast_path1, is_ip40, is_ip41, enqueue_code;
	  u32 skip_len0, skip_len1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, LOAD);
	    CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, LOAD);
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

	  d0 = (void *) (b0->data + b0->current_data);
	  d1 = (void *) (b1->data + b1->current_data);

	  error0 = error1 = DOCSIS_ERROR_NONE;

	  fast_path0 = d0->generic.header.packet_type == DOCSIS_PACKET_TYPE_ethernet;
	  fast_path1 = d1->generic.header.packet_type == DOCSIS_PACKET_TYPE_ethernet;

	  fast_path0 |= d0->generic.n_bytes_in_extended_header == 0;
	  fast_path1 |= d1->generic.n_bytes_in_extended_header == 0;

	  if (! (fast_path0 & fast_path1))
	    goto slow_path_x2;

	  /* Check fast path header length. */
	  error0 = (clib_net_to_host_u16 (d0->generic.n_bytes_in_payload_plus_extended_header)
		    + sizeof (d0->generic)
		    != vlib_buffer_length_in_chain (vm, b0)
		    ? DOCSIS_ERROR_WRONG_LENGTH
		    : error0);
	  error1 = (clib_net_to_host_u16 (d1->generic.n_bytes_in_payload_plus_extended_header)
		    + sizeof (d1->generic)
		    != vlib_buffer_length_in_chain (vm, b1)
		    ? DOCSIS_ERROR_WRONG_LENGTH
		    : error1);

	  /* Check header CRC. */
	  crc0 = crc1 = 0;

	  ASSERT (4 == sizeof (d0->generic) - sizeof (d0->generic.expected_header_crc));
	  ASSERT (4 == sizeof (d1->generic) - sizeof (d1->generic.expected_header_crc));
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[0]);
	  crc1 = crc_itu_t_update (crc1, d1->as_u8[0]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[1]);
	  crc1 = crc_itu_t_update (crc1, d1->as_u8[1]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[2]);
	  crc1 = crc_itu_t_update (crc1, d1->as_u8[2]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[3]);
	  crc1 = crc_itu_t_update (crc1, d1->as_u8[3]);
	  error0 = (clib_net_to_host_u16 (d0->generic.expected_header_crc) != crc0
		    ? DOCSIS_ERROR_WRONG_HEADER_CRC
		    : error0);
	  error1 = (clib_net_to_host_u16 (d1->generic.expected_header_crc) != crc1
		    ? DOCSIS_ERROR_WRONG_HEADER_CRC
		    : error1);

	  /* Skip to ethernet header. */
	  e0 = (void *) d0->generic.payload;
	  e1 = (void *) d1->generic.payload;

	  next0 = next1 = DOCSIS_INPUT_NEXT_ETHERNET_INPUT;

	  is_ip40 = e0->type == clib_host_to_net_u16 (ETHERNET_TYPE_IP4);
	  is_ip41 = e1->type == clib_host_to_net_u16 (ETHERNET_TYPE_IP4);

	  next0 = is_ip40 ? DOCSIS_INPUT_NEXT_IP4_INPUT : next0;
	  next1 = is_ip41 ? DOCSIS_INPUT_NEXT_IP4_INPUT : next1;

	  skip_len0 = is_ip40 ? sizeof (e0[0]) : 0;
	  skip_len1 = is_ip41 ? sizeof (e1[0]) : 0;

	  /* Skip over DOCSIS mac header. */
	  skip_len0 += (void *) e0 - (void*) d0;
	  skip_len1 += (void *) e1 - (void*) d1;

	  b0->current_data += skip_len0;
	  b1->current_data += skip_len1;

	  /* Don't include ethernet CRC in packet length. */
	  b0->current_length -= skip_len0 + sizeof (u32);
	  b1->current_length -= skip_len1 + sizeof (u32);

	  next0 = error0 != DOCSIS_ERROR_NONE ? DOCSIS_INPUT_NEXT_DROP : next0;
	  next1 = error1 != DOCSIS_ERROR_NONE ? DOCSIS_INPUT_NEXT_DROP : next1;

	  b0->error = node->errors[error0];
	  b1->error = node->errors[error1];

	enqueue_x2:
	  enqueue_code = (next0 != next_index) + 2*(next1 != next_index);
	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      switch (enqueue_code)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = bi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next0, bi0);
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next1, bi1);
		  break;

		case 3:
		  /* A B B or A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  vlib_set_next_frame_buffer (vm, node, next0, bi0);
		  vlib_set_next_frame_buffer (vm, node, next1, bi1);
		  if (next0 == next1)
		    {
		      vlib_put_next_frame (vm, node, next_index,
					   n_left_to_next);
		      next_index = next1;
		      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
		    }
		}
	    }
	  continue;

	slow_path_x2:
	  next0 = docsis_input_slow_path (vm, node, b0);
	  next1 = docsis_input_slow_path (vm, node, b1);
	  goto enqueue_x2;
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t * b0;
	  docsis_packet_t * d0;
	  docsis_input_next_t next0;
	  ethernet_header_t * e0;
	  u16 crc0;
	  u8 error0, fast_path0, is_ip40;
	  u32 skip_len0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  b0 = vlib_get_buffer (vm, bi0);

	  d0 = (void *) (b0->data + b0->current_data);

	  error0 = DOCSIS_ERROR_NONE;
	  fast_path0 = d0->generic.header.packet_type == DOCSIS_PACKET_TYPE_ethernet;
	  fast_path0 |= d0->generic.n_bytes_in_extended_header == 0;

	  if (! fast_path0)
	    goto slow_path_x1;

	  /* Check fast path header length. */
	  error0 = (clib_net_to_host_u16 (d0->generic.n_bytes_in_payload_plus_extended_header)
		    + sizeof (d0->generic)
		    != vlib_buffer_length_in_chain (vm, b0)
		    ? DOCSIS_ERROR_WRONG_LENGTH
		    : error0);

	  /* Check header CRC. */
	  crc0 = 0;

	  ASSERT (4 == sizeof (d0->generic) - sizeof (d0->generic.expected_header_crc));
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[0]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[1]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[2]);
	  crc0 = crc_itu_t_update (crc0, d0->as_u8[3]);
	  error0 = (clib_net_to_host_u16 (d0->generic.expected_header_crc) != crc0
		    ? DOCSIS_ERROR_WRONG_HEADER_CRC
		    : error0);

	  /* Skip to ethernet header. */
	  e0 = (void *) d0->generic.payload;

	  next0 = DOCSIS_INPUT_NEXT_ETHERNET_INPUT;

	  is_ip40 = e0->type == clib_host_to_net_u16 (ETHERNET_TYPE_IP4);

	  next0 = is_ip40 ? DOCSIS_INPUT_NEXT_IP4_INPUT : next0;

	  skip_len0 = is_ip40 ? sizeof (e0[0]) : 0;

	  /* Skip over DOCSIS mac header. */
	  skip_len0 += (void *) e0 - (void*) d0;

	  b0->current_data += skip_len0;

	  /* Don't include ethernet CRC in packet length. */
	  b0->current_length -= skip_len0 + sizeof (u32);

	  next0 = error0 != DOCSIS_ERROR_NONE ? DOCSIS_INPUT_NEXT_DROP : next0;

	  b0->error = node->errors[error0];

	enqueue_x1:
	  /* Sent packet to wrong next? */
	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      /* Return old frame; remove incorrectly enqueued packet. */
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);

	      /* Send to correct next. */
	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index,
				   to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	  continue;

	slow_path_x1:
	  next0 = docsis_input_slow_path (vm, node, b0);
	  goto enqueue_x1;
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return from_frame->n_vectors;
}

static char * docsis_error_strings[] = {
#define _(n,s) s,
#include "error.def"
#undef _
};

VLIB_REGISTER_NODE (docsis_input_node) = {
  .function = docsis_input,
  .name = "docsis-input",
  /* Takes a vector of packets. */
  .vector_size = sizeof (u32),

  .n_errors = DOCSIS_N_ERROR,
  .error_strings = docsis_error_strings,

  .n_next_nodes = DOCSIS_INPUT_N_NEXT,
  .next_nodes = {
#define _(s,n) [DOCSIS_INPUT_NEXT_##s] = n,
    foreach_docsis_input_next
#undef _
  },

  .format_buffer = format_docsis_header_with_length,
  .format_trace = format_docsis_input_trace,
  .unformat_buffer = unformat_docsis_header,
};

static clib_error_t * docsis_input_init (vlib_main_t * vm)
{
  docsis_main_t * dm = &docsis_main;
  clib_error_t * error;
  int i, j;

  /* Basic sanity. */
  ASSERT (sizeof (docsis_packet_header_t) == 1);
  ASSERT (STRUCT_SIZE_OF (docsis_packet_t, generic) == 6);

  dm->role = DOCSIS_ROLE_CMTS;

  /* Fill in input functions for each role to drop all unknown control/management packets.
     Mark all known control/management packet types as unexpected.
     CM/CMTS init function will fill them in appropriately for role. */
  for (i = 0; i < DOCSIS_N_ROLE; i++)
    {
      for (j = 0; j < ARRAY_LEN (dm->input_functions_for_role[i].control); j++)
	dm->input_functions_for_role[i].control[j] = docsis_input_unknown_control;
      for (j = 0; j < ARRAY_LEN (dm->input_functions_for_role[i].management); j++)
	dm->input_functions_for_role[i].management[j] = docsis_input_unknown_management;

#define _(f,n) dm->input_functions_for_role[i].control[n] = docsis_input_unexpected_control;
      foreach_docsis_control_packet_type
#undef _
#define _(f,n,v) dm->input_functions_for_role[i].management[n] = docsis_input_unexpected_management;
      foreach_docsis_management_packet_type
#undef _
    }

  if ((error = vlib_call_init_function (vm, docsis_input_cm_init)))
    return error;
  if ((error = vlib_call_init_function (vm, docsis_input_cmts_init)))
    return error;

  docsis_setup_node (vm, docsis_input_node.index);
  return 0;
}

VLIB_INIT_FUNCTION (docsis_input_init);
