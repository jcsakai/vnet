/*
 * ip/icmp4.c: ipv4 icmp
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

static u8 * format_ip4_icmp_type_and_code (u8 * s, va_list * args)
{
  icmp4_type_t type = va_arg (*args, int);
  u8 code = va_arg (*args, int);
  char * t = 0;

#define _(n,f) case n: t = #f; break;

  switch (type)
    {
      foreach_icmp4_type;

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
#define _(a,n,f) case (ICMP4_##a << 8) | (n): t = #f; break;

      foreach_icmp4_code;

#undef _
    }

  if (t)
    s = format (s, " %s", code);

  return s;
}

static u8 * format_ip4_icmp_header (u8 * s, va_list * args)
{
  icmp46_header_t * icmp = va_arg (*args, icmp46_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);

  /* Nothing to do. */
  if (max_header_bytes < sizeof (icmp[0]))
    return format (s, "ICMP header truncated");

  s = format (s, "ICMP %U",
	      format_ip4_icmp_type_and_code, icmp->type, icmp->code);

  return s;
}

typedef struct {
  u8 packet_data[64];
} icmp_input_trace_t;

static u8 * format_icmp_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  icmp_input_trace_t * t = va_arg (*va, icmp_input_trace_t *);

  s = format (s, "%U",
	      format_ip4_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

typedef enum {
  ICMP4_ERROR_UNKNOWN_TYPE,
  ICMP4_ERROR_ECHO_REPLIES_SENT,
} icmp_error_t;

typedef enum {
  ICMP_INPUT_NEXT_ERROR,
  ICMP_INPUT_N_NEXT,
} icmp_input_next_t;

typedef struct {
  uword * type_and_code_by_name;

  uword * type_by_name;

  /* Vector dispatch table indexed by [icmp type]. */
  u8 ip4_input_next_index_by_type[256];
} icmp4_main_t;

icmp4_main_t icmp4_main;

static uword
ip4_icmp_input (vlib_main_t * vm,
		vlib_node_runtime_t * node,
		vlib_frame_t * frame)
{
  icmp4_main_t * im = &icmp4_main;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (icmp_input_trace_t));

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  icmp46_header_t * icmp0;
	  icmp4_type_t type0;
	  u32 bi0, next0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);
	  icmp0 = ip4_next_header (ip0);
	  type0 = icmp0->type;
	  next0 = im->ip4_input_next_index_by_type[type0];

	  p0->error = node->errors[ICMP4_ERROR_UNKNOWN_TYPE];
	  if (PREDICT_FALSE (next0 != next))
	    {
	      vlib_put_next_frame (vm, node, next, n_left_to_next + 1);
	      next = next0;
	      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
	      to_next[0] = bi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  return frame->n_vectors;
}

static char * icmp_error_strings[] = {
  [ICMP4_ERROR_UNKNOWN_TYPE] = "unknown type",
  [ICMP4_ERROR_ECHO_REPLIES_SENT] = "echo replies sent",
};

static VLIB_REGISTER_NODE (ip4_icmp_input_node) = {
  .function = ip4_icmp_input,
  .name = "ip4-icmp-input",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp_input_trace,

  .n_errors = ARRAY_LEN (icmp_error_strings),
  .error_strings = icmp_error_strings,

  .n_next_nodes = 1,
  .next_nodes = {
    [ICMP_INPUT_NEXT_ERROR] = "error-drop",
  },
};

static uword
ip4_icmp_echo_request (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       vlib_frame_t * frame)
{
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next;
  ip4_main_t * i4m = &ip4_main;
  u16 * fragment_ids, * fid;

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (icmp_input_trace_t));

  /* Get random fragment IDs for replies. */
  fid = fragment_ids = clib_random_buffer_get_data (&vm->random_buffer,
						    n_packets * sizeof (fragment_ids[0]));

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);

      while (n_left_from > 2 && n_left_to_next > 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip4_header_t * ip0, * ip1;
	  icmp46_header_t * icmp0, * icmp1;
	  u32 bi0, src0, dst0;
	  u32 bi1, src1, dst1;
	  ip_csum_t sum0, sum1;
      
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
	  icmp0 = ip4_next_header (ip0);
	  icmp1 = ip4_next_header (ip1);

	  p0->flags |= VNET_BUFFER_LOCALLY_GENERATED;
	  p1->flags |= VNET_BUFFER_LOCALLY_GENERATED;

	  icmp0->type = ICMP4_echo_reply;
	  icmp1->type = ICMP4_echo_reply;

	  src0 = ip0->src_address.data_u32;
	  src1 = ip1->src_address.data_u32;
	  dst0 = ip0->dst_address.data_u32;
	  dst1 = ip1->dst_address.data_u32;

	  /* Swap source and destination address.
	     Does not change checksum. */
	  ip0->src_address.data_u32 = dst0;
	  ip1->src_address.data_u32 = dst1;
	  ip0->dst_address.data_u32 = src0;
	  ip1->dst_address.data_u32 = src1;

	  sum0 = ip0->checksum;
	  sum1 = ip1->checksum;

	  /* Remove old ttl & fragment id from checksum. */
	  sum0 = ip_csum_sub_even (sum0, ip0->ttl);
	  sum1 = ip_csum_sub_even (sum1, ip1->ttl);
	  sum0 = ip_csum_sub_even (sum0, ip0->fragment_id);
	  sum1 = ip_csum_sub_even (sum1, ip1->fragment_id);

	  /* New ttl. */
	  ip0->ttl = i4m->host_config.ttl;
	  ip1->ttl = i4m->host_config.ttl;
	  sum0 = ip_csum_add_even (sum0, ip0->ttl);
	  sum1 = ip_csum_add_even (sum1, ip1->ttl);

	  /* New fragment id. */
	  ip0->fragment_id = fid[0];
	  ip1->fragment_id = fid[1];
	  sum0 = ip_csum_add_even (sum0, ip0->fragment_id);
	  sum1 = ip_csum_add_even (sum1, ip1->fragment_id);
	  fid += 2;

	  ip0->checksum = ip_csum_fold (sum0);
	  ip1->checksum = ip_csum_fold (sum1);
	}
  
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  icmp46_header_t * icmp0;
	  u32 bi0, src0, dst0;
	  ip_csum_t sum0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);
	  icmp0 = ip4_next_header (ip0);

	  p0->flags |= VNET_BUFFER_LOCALLY_GENERATED;

	  icmp0->type = ICMP4_echo_reply;
	  src0 = ip0->src_address.data_u32;
	  dst0 = ip0->dst_address.data_u32;

	  sum0 = ip0->checksum;
	  sum0 = ip_csum_sub_even (sum0, ip0->ttl);
	  sum0 = ip_csum_sub_even (sum0, ip0->fragment_id);

	  ip0->ttl = i4m->host_config.ttl;
	  sum0 = ip_csum_add_even (sum0, ip0->ttl);

	  ip0->fragment_id = fid[0];
	  sum0 = ip_csum_add_even (sum0, ip0->fragment_id);
	  fid += 1;

	  ip0->checksum = ip_csum_fold (sum0);

	  ip0->src_address.data_u32 = dst0;
	  ip0->dst_address.data_u32 = src0;
	}
  
      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  vlib_error_count (vm, ip4_icmp_input_node.index,
		    ICMP4_ERROR_ECHO_REPLIES_SENT,
		    frame->n_vectors);

  return frame->n_vectors;
}

static VLIB_REGISTER_NODE (ip4_icmp_echo_request_node) = {
  .function = ip4_icmp_echo_request,
  .name = "ip4-icmp-echo-request",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp_input_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = DEBUG > 0 ? "ip4-input" : "ip4-lookup",
  },
};

static uword unformat_icmp_type_and_code (unformat_input_t * input, va_list * args)
{
  icmp46_header_t * h = va_arg (*args, icmp46_header_t *);
  icmp4_main_t * cm = &icmp4_main;
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

  /* Defaults. */
  pg_edit_set_fixed (&p->checksum, 0);

  {
    icmp46_header_t tmp;

    if (! unformat (input, "ICMP %U", unformat_icmp_type_and_code, &tmp))
      goto error;

    pg_edit_set_fixed (&p->type, tmp.type);
    pg_edit_set_fixed (&p->code, tmp.code);
  }

  if (! unformat_user (input, unformat_pg_payload, s))
    goto error;

  return 1;

 error:
  /* Free up any edits we may have added. */
  pg_free_edit_group (s);
  return 0;
}

static void ip4_icmp_register_type (vlib_main_t * vm, icmp4_type_t type, u32 node_index)
{
  icmp4_main_t * im = &icmp4_main;

  ASSERT (type < ARRAY_LEN (im->ip4_input_next_index_by_type));
  im->ip4_input_next_index_by_type[type]
    = vlib_node_add_next (vm, ip4_icmp_input_node.index, node_index);
}

static clib_error_t *
icmp4_init (vlib_main_t * vm)
{
  ip_main_t * im = &ip_main;
  ip_protocol_info_t * pi;
  icmp4_main_t * cm = &icmp4_main;
  clib_error_t * error;

  error = vlib_call_init_function (vm, ip_main_init);

  if (error)
    return error;

  pi = ip_get_protocol_info (im, IP_PROTOCOL_ICMP);
  pi->format_header = format_ip4_icmp_header;
  pi->unformat_pg_edit = unformat_pg_icmp_header;

  cm->type_by_name = hash_create_string (0, sizeof (uword));
#define _(n,t) hash_set_mem (cm->type_by_name, #t, (n));
  foreach_icmp4_type;
#undef _

  cm->type_and_code_by_name = hash_create_string (0, sizeof (uword));
#define _(a,n,t) hash_set_mem (cm->type_by_name, #t, (n) | (ICMP4_##a << 8));
  foreach_icmp4_code;
#undef _

  memset (cm->ip4_input_next_index_by_type,
	  ICMP_INPUT_NEXT_ERROR,
	  sizeof (cm->ip4_input_next_index_by_type));

  ip4_icmp_register_type (vm, ICMP4_echo_request, ip4_icmp_echo_request_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (icmp4_init);
