/*
 * ip/icmp6.c: ip6 icmp
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

#include <vlib/vlib.h>
#include <vnet/ip/ip.h>
#include <vnet/pg/pg.h>

static u8 * format_ip6_icmp_type_and_code (u8 * s, va_list * args)
{
  icmp6_type_t type = va_arg (*args, int);
  u8 code = va_arg (*args, int);
  char * t = 0;

#define _(n,f) case n: t = #f; break;

  switch (type)
    {
      foreach_icmp6_type;

    default:
      break;
    }

#undef _

  if (! t)
    return format (s, "unknown 0x%x", type);

  s = format (s, "%s", t);

  t = 0;
  switch ((type << 8) | code)
    {
#define _(a,n,f) case (ICMP6_##a << 8) | (n): t = #f; break;

      foreach_icmp6_code;

#undef _
    }

  if (t)
    s = format (s, " %s", code);

  return s;
}

static u8 * format_icmp6_header (u8 * s, va_list * args)
{
  icmp46_header_t * icmp = va_arg (*args, icmp46_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);

  /* Nothing to do. */
  if (max_header_bytes < sizeof (icmp[0]))
    return format (s, "ICMP header truncated");

  s = format (s, "ICMP %U checksum 0x%x",
	      format_ip6_icmp_type_and_code, icmp->type, icmp->code,
	      clib_net_to_host_u16 (icmp->checksum));

  return s;
}

u8 * format_icmp6_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  icmp6_input_trace_t * t = va_arg (*va, icmp6_input_trace_t *);

  s = format (s, "%U",
	      format_ip6_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

static char * icmp_error_strings[] = {
#define _(f,s) s,
  foreach_icmp6_error
#undef _
};

typedef enum {
  ICMP_INPUT_NEXT_DROP,
  ICMP_INPUT_N_NEXT,
} icmp_input_next_t;

typedef struct {
  uword * type_and_code_by_name;

  uword * type_by_name;

  /* Vector dispatch table indexed by [icmp type]. */
  u8 input_next_index_by_type[256];

  /* Max valid code indexed by icmp type. */
  u8 max_valid_code_by_type[256];

  /* hop_limit must be >= this value for this icmp type. */
  u8 min_valid_hop_limit_by_type[256];

  u8 min_valid_length_by_type[256];
} icmp6_main_t;

icmp6_main_t icmp6_main;

static uword
ip6_icmp_input (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{
  icmp6_main_t * im = &icmp6_main;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next_index;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (icmp6_input_trace_t));

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * b0;
	  ip6_header_t * ip0;
	  icmp46_header_t * icmp0;
	  icmp6_type_t type0;
	  u32 bi0, next0, error0, len0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  b0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (b0);
	  icmp0 = ip6_next_header (ip0);
	  type0 = icmp0->type;

	  error0 = ICMP6_ERROR_NONE;

	  next0 = im->input_next_index_by_type[type0];
	  error0 = next0 == ICMP_INPUT_NEXT_DROP ? ICMP6_ERROR_UNKNOWN_TYPE : error0;

	  /* Check code is valid for type. */
	  error0 = icmp0->code > im->max_valid_code_by_type[type0] ? ICMP6_ERROR_INVALID_CODE_FOR_TYPE : error0;

	  /* Checksum is already validated by ip6_local node so we don't need to check that. */

	  /* Check that hop limit == 255 for certain types. */
	  error0 = ip0->hop_limit < im->min_valid_hop_limit_by_type[type0] ? ICMP6_ERROR_INVALID_HOP_LIMIT_FOR_TYPE : error0;

	  len0 = clib_net_to_host_u16 (ip0->payload_length);
	  error0 = len0 < im->min_valid_length_by_type[type0] ? ICMP6_ERROR_LENGTH_TOO_SMALL_FOR_TYPE : error0;

	  error0 = len0 % 8 ? ICMP6_ERROR_OPTIONS_WITH_ODD_LENGTH : error0;

	  b0->error = node->errors[error0];

	  next0 = error0 != ICMP6_ERROR_NONE ? ICMP_INPUT_NEXT_DROP : next0;

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

VLIB_REGISTER_NODE (ip6_icmp_input_node) = {
  .function = ip6_icmp_input,
  .name = "ip6-icmp-input",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp6_input_trace,

  .n_errors = ARRAY_LEN (icmp_error_strings),
  .error_strings = icmp_error_strings,

  .n_next_nodes = 1,
  .next_nodes = {
    [ICMP_INPUT_NEXT_DROP] = "error-drop",
  },
};

static uword
ip6_icmp_echo_request (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       vlib_frame_t * frame)
{
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  ip6_main_t * im = &ip6_main;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (icmp6_input_trace_t));

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 2 && n_left_to_next > 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip6_header_t * ip0, * ip1;
	  icmp46_header_t * icmp0, * icmp1;
	  ip6_address_t tmp0, tmp1;
	  ip_csum_t sum0, sum1;
	  u32 bi0, bi1;
      
	  bi0 = to_next[0] = from[0];
	  bi1 = to_next[1] = from[1];

	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  p1 = vlib_get_buffer (vm, bi1);
	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);
	  icmp0 = ip6_next_header (ip0);
	  icmp1 = ip6_next_header (ip1);

	  /* Check icmp type to echo reply and update icmp checksum. */
	  sum0 = icmp0->checksum;
	  sum1 = icmp1->checksum;

	  ASSERT (icmp0->type == ICMP6_echo_request);
	  ASSERT (icmp1->type == ICMP6_echo_request);
	  sum0 = ip_csum_update (sum0, ICMP6_echo_request, ICMP6_echo_reply,
				 icmp46_header_t, type);
	  sum1 = ip_csum_update (sum1, ICMP6_echo_request, ICMP6_echo_reply,
				 icmp46_header_t, type);

	  icmp0->checksum = ip_csum_fold (sum0);
	  icmp1->checksum = ip_csum_fold (sum1);

	  icmp0->type = ICMP6_echo_reply;
	  icmp1->type = ICMP6_echo_reply;

	  /* Swap source and destination address. */
	  tmp0 = ip0->src_address;
	  tmp1 = ip1->src_address;

	  ip0->src_address = ip0->dst_address;
	  ip1->src_address = ip1->dst_address;

	  ip0->dst_address = tmp0;
	  ip1->dst_address = tmp1;

	  /* New hop count. */
	  ip0->hop_limit = im->host_config.ttl;
	  ip1->hop_limit = im->host_config.ttl;

	  p0->flags |= VNET_BUFFER_LOCALLY_GENERATED;
	  p1->flags |= VNET_BUFFER_LOCALLY_GENERATED;
	}
  
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  icmp46_header_t * icmp0;
	  u32 bi0;
	  ip6_address_t tmp0;
	  ip_csum_t sum0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);
	  icmp0 = ip6_next_header (ip0);

	  /* Check icmp type to echo reply and update icmp checksum. */
	  sum0 = icmp0->checksum;

	  ASSERT (icmp0->type == ICMP6_echo_request);
	  sum0 = ip_csum_update (sum0, ICMP6_echo_request, ICMP6_echo_reply,
				 icmp46_header_t, type);

	  icmp0->checksum = ip_csum_fold (sum0);

	  icmp0->type = ICMP6_echo_reply;

	  /* Swap source and destination address. */
	  tmp0 = ip0->src_address;
	  ip0->src_address = ip0->dst_address;
	  ip0->dst_address = tmp0;

	  ip0->hop_limit = im->host_config.ttl;
	  p0->flags |= VNET_BUFFER_LOCALLY_GENERATED;
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  vlib_error_count (vm, ip6_icmp_input_node.index,
		    ICMP6_ERROR_ECHO_REPLIES_SENT,
		    frame->n_vectors);

  return frame->n_vectors;
}

static VLIB_REGISTER_NODE (ip6_icmp_echo_request_node) = {
  .function = ip6_icmp_echo_request,
  .name = "ip6-icmp-echo-request",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp6_input_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = CLIB_DEBUG > 0 ? "ip6-input" : "ip6-lookup",
  },
};

static uword unformat_icmp_type_and_code (unformat_input_t * input, va_list * args)
{
  icmp46_header_t * h = va_arg (*args, icmp46_header_t *);
  icmp6_main_t * cm = &icmp6_main;
  u32 i;

  if (unformat_user (input, unformat_vlib_number_by_name,
		     cm->type_and_code_by_name, &i))
    {
      h->type = (i >> 8) & 0xff;
      h->code = (i >> 0) & 0xff;
    }
  else if (unformat_user (input, unformat_vlib_number_by_name,
			  cm->type_by_name, &i))
    {
      h->type = i;
      h->code = 0;
    }
  else
    return 0;

  return 1;
}

static void
icmp6_pg_edit_function (pg_main_t * pg,
			pg_stream_t * s,
			pg_edit_group_t * g,
			u32 * packets,
			u32 n_packets)
{
  vlib_main_t * vm = pg->vlib_main;
  u32 ip_offset, icmp_offset;

  icmp_offset = g->start_byte_offset;
  ip_offset = (g-1)->start_byte_offset;

  while (n_packets >= 1)
    {
      vlib_buffer_t * p0;
      ip6_header_t * ip0;
      icmp46_header_t * icmp0;

      p0 = vlib_get_buffer (vm, packets[0]);
      n_packets -= 1;
      packets += 1;

      ASSERT (p0->current_data == 0);
      ip0 = (void *) (p0->data + ip_offset);
      icmp0 = (void *) (p0->data + icmp_offset);

      icmp0->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, p0, ip0);
    }
}

typedef struct {
  pg_edit_t type, code;
  pg_edit_t checksum;
} pg_icmp46_header_t;

always_inline void
pg_icmp_header_init (pg_icmp46_header_t * p)
{
  /* Initialize fields that are not bit fields in the IP header. */
#define _(f) pg_edit_init (&p->f, icmp46_header_t, f);
  _ (type);
  _ (code);
  _ (checksum);
#undef _
}

static uword
unformat_pg_icmp_header (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_icmp46_header_t * p;
  u32 group_index;
  
  p = pg_create_edit_group (s, sizeof (p[0]), sizeof (icmp46_header_t),
			    &group_index);
  pg_icmp_header_init (p);

  p->checksum.type = PG_EDIT_UNSPECIFIED;

  {
    icmp46_header_t tmp;

    if (! unformat (input, "ICMP %U", unformat_icmp_type_and_code, &tmp))
      goto error;

    pg_edit_set_fixed (&p->type, tmp.type);
    pg_edit_set_fixed (&p->code, tmp.code);
  }

  /* Parse options. */
  while (1)
    {
      if (unformat (input, "checksum %U",
		    unformat_pg_edit,
		    unformat_pg_number, &p->checksum))
	;

      /* Can't parse input: try next protocol level. */
      else
	break;
    }

  if (! unformat_user (input, unformat_pg_payload, s))
    goto error;

  if (p->checksum.type == PG_EDIT_UNSPECIFIED)
    {
      pg_edit_group_t * g = pg_stream_get_group (s, group_index);
      g->edit_function = icmp6_pg_edit_function;
      g->edit_function_opaque = 0;
    }

  return 1;

 error:
  /* Free up any edits we may have added. */
  pg_free_edit_group (s);
  return 0;
}

void icmp6_register_type (vlib_main_t * vm, icmp6_type_t type, u32 node_index)
{
  icmp6_main_t * im = &icmp6_main;

  ASSERT (type < ARRAY_LEN (im->input_next_index_by_type));
  im->input_next_index_by_type[type]
    = vlib_node_add_next (vm, ip6_icmp_input_node.index, node_index);
}

static clib_error_t *
icmp6_init (vlib_main_t * vm)
{
  ip_main_t * im = &ip_main;
  ip_protocol_info_t * pi;
  icmp6_main_t * cm = &icmp6_main;
  clib_error_t * error;

  error = vlib_call_init_function (vm, ip_main_init);

  if (error)
    return error;

  pi = ip_get_protocol_info (im, IP_PROTOCOL_ICMP6);
  pi->format_header = format_icmp6_header;
  pi->unformat_pg_edit = unformat_pg_icmp_header;

  cm->type_by_name = hash_create_string (0, sizeof (uword));
#define _(n,t) hash_set_mem (cm->type_by_name, #t, (n));
  foreach_icmp6_type;
#undef _

  cm->type_and_code_by_name = hash_create_string (0, sizeof (uword));
#define _(a,n,t) hash_set_mem (cm->type_by_name, #t, (n) | (ICMP6_##a << 8));
  foreach_icmp6_code;
#undef _

  memset (cm->input_next_index_by_type,
	  ICMP_INPUT_NEXT_DROP,
	  sizeof (cm->input_next_index_by_type));
  memset (cm->max_valid_code_by_type, 0, sizeof (cm->max_valid_code_by_type));

#define _(a,n,t) cm->max_valid_code_by_type[ICMP6_##a] = clib_max (cm->max_valid_code_by_type[ICMP6_##a], n);
  foreach_icmp6_code;
#undef _

  memset (cm->min_valid_hop_limit_by_type, 0, sizeof (cm->min_valid_hop_limit_by_type));
  cm->min_valid_hop_limit_by_type[ICMP6_router_solicitation] = 255;
  cm->min_valid_hop_limit_by_type[ICMP6_router_advertisement] = 255;
  cm->min_valid_hop_limit_by_type[ICMP6_neighbor_solicitation] = 255;
  cm->min_valid_hop_limit_by_type[ICMP6_neighbor_advertisement] = 255;
  cm->min_valid_hop_limit_by_type[ICMP6_redirect] = 255;

  memset (cm->min_valid_length_by_type, sizeof (icmp46_header_t), sizeof (cm->min_valid_length_by_type));
  cm->min_valid_length_by_type[ICMP6_router_solicitation] = sizeof (icmp6_neighbor_discovery_header_t);
  cm->min_valid_length_by_type[ICMP6_router_advertisement] = sizeof (icmp6_router_advertisement_header_t);
  cm->min_valid_length_by_type[ICMP6_neighbor_solicitation]
    = sizeof (icmp6_neighbor_solicitation_or_advertisement_header_t);
  cm->min_valid_length_by_type[ICMP6_neighbor_advertisement]
    = sizeof (icmp6_neighbor_solicitation_or_advertisement_header_t);
  cm->min_valid_length_by_type[ICMP6_redirect] = sizeof (icmp6_redirect_header_t);

  icmp6_register_type (vm, ICMP6_echo_request, ip6_icmp_echo_request_node.index);

  return vlib_call_init_function (vm, ip6_neighbor_init);
}

VLIB_INIT_FUNCTION (icmp6_init);
