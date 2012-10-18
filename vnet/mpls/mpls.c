/*
 * mpls.c: mpls support
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
#include <vnet/mpls/mpls.h>

/* Global main structure. */
mpls_main_t mpls_main;

static u8 * format_mpls_label (u8 * s, va_list * args)
{
  u32 l = va_arg (*args, u32);

  if (l < MPLS_N_RESERVED_LABELS)
    switch (l)
      {
#define _(f,n) case n: s = format (s, "%s", #f); break;
	foreach_mpls_special_label
#undef _
      default:
	s = format (s, "unknown reserved 0x%x", l);
	break;
      }
  else
    s = format (s, "%d", l);

  return s;
}

u8 * format_mpls_header_with_length (u8 * s, va_list * args)
{
  mpls_header_t * h = va_arg (*args, mpls_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);
  uword indent, header_bytes;

  header_bytes = sizeof (h[0]);
  if (max_header_bytes != 0 && header_bytes > max_header_bytes)
    return format (s, "mpls header truncated");

  indent = format_get_indent (s);

  s = format (s, "MPLS label %U, cos %d",
	      format_mpls_label, h->label, h->traffic_class);

  /* Format inner header. */
  if (max_header_bytes != 0 && header_bytes > max_header_bytes)
    {
      format_function_t * f;

      if (h->is_final_label)
	switch (mpls_header_get_ip_version (h))
	  {
	  case 4: f = format_ip4_header; break;
	  case 6: f = format_ip6_header; break;
	  default: f = format_hex_bytes; break;
	  }
      else
	f = format_mpls_header_with_length;

      s = format (s, "\n%U%U",
		  format_white_space, indent,
		  f, (void *) (h + 1),
		  max_header_bytes - header_bytes);
    }

  return s;
}

u8 * format_mpls_header (u8 * s, va_list * args)
{
  mpls_header_t * h = va_arg (*args, mpls_header_t *);
  return format (s, "%U", format_mpls_header_with_length, h, 0);
}

uword
unformat_mpls_header (unformat_input_t * input, va_list * args)
{
  u8 ** result = va_arg (*args, u8 **);
  mpls_header_t * hs = 0, * h;
  u32 label, tc;

  tc = 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "traffic-class %d", &tc))
	;
      else if (unformat (input, "label %d", &label))
	{
	  vec_add2 (hs, h, 1);
	  h->label = label;
	  h->traffic_class = tc;
	}
      else
	break;
    }
      
  if (vec_len (hs) == 0)
    return 0;

  /* Mark final label. */
  vec_end (hs)[-1].is_final_label = 1;

  /* Add header to result. */
  {
    void * p;
    u32 n_bytes = vec_bytes (hs);

    vec_add2 (*result, p, n_bytes);
    memcpy (p, hs, n_bytes);
  }
  
  return 1;
}

typedef struct {
  pg_edit_t label;
  pg_edit_t traffic_class;
  pg_edit_t is_final_label;
  pg_edit_t ttl;
} pg_mpls_header_t;

typedef struct {
  mpls_header_union_t headers[0];
} mpls_header_vector_t;

static inline void
pg_mpls_header_init (pg_mpls_header_t * e, u32 i)
{
  pg_edit_init_bitfield (&e->ttl,
			 mpls_header_vector_t,
			 headers[i].as_u32,
			 0, 8);
  pg_edit_init_bitfield (&e->is_final_label,
			 mpls_header_vector_t,
			 headers[i].as_u32,
			 8, 1);
  pg_edit_init_bitfield (&e->traffic_class,
			 mpls_header_vector_t,
			 headers[i].as_u32,
			 9, 3);
  pg_edit_init_bitfield (&e->label,
			 mpls_header_vector_t,
			 headers[i].as_u32,
			 12, 20);
}

uword
unformat_pg_mpls_header (unformat_input_t * input, va_list * args)
{
#if 0
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_mpls_header_t * h;
  u32 group_index, error;
  
  h = pg_create_edit_group (s, sizeof (h[0]), sizeof (mpls_header_t),
			    &group_index);
  pg_mpls_header_init (h, 0);

  pg_edit_set_fixed (&h->address, 0xff);
  pg_edit_set_fixed (&h->control, 0x03);

  error = 1;
  if (! unformat (input, "%U",
		  unformat_pg_edit,
		    unformat_mpls_protocol_net_byte_order, &h->protocol))
    goto done;

  {
    mpls_main_t * pm = &mpls_main;
    mpls_protocol_info_t * pi = 0;
    pg_node_t * pg_node = 0;

    if (h->protocol.type == PG_EDIT_FIXED)
      {
	u16 t = *(u16 *) h->protocol.values[PG_EDIT_LO];
	pi = mpls_get_protocol_info (pm, clib_net_to_host_u16 (t));
	if (pi && pi->node_index != ~0)
	  pg_node = pg_get_node (pi->node_index);
      }

    if (pg_node && pg_node->unformat_edit
	&& unformat_user (input, pg_node->unformat_edit, s))
      ;

    else if (! unformat_user (input, unformat_pg_payload, s))
      goto done;
  }

  error = 0;
 done:
  if (error)
    pg_free_edit_group (s);
  return error == 0;
#else
  return 0;
#endif
}

static clib_error_t * mpls_init (vlib_main_t * vm)
{
  mpls_main_t * pm = &mpls_main;

  memset (pm, 0, sizeof (pm[0]));
  pm->vlib_main = vm;

  return vlib_call_init_function (vm, mpls_input_init);
}

VLIB_INIT_FUNCTION (mpls_init);

