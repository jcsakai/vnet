/*
 * docsis/format.c: DOCSIS formatting
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

static u8 * format_docsis_packet_type (u8 * s, va_list * va)
{
  docsis_packet_type_t t = va_arg (*va, docsis_packet_type_t);
  char * n = 0;

  switch (t)
    {
#define _(f) case DOCSIS_PACKET_TYPE_##f: n = #f; break;
      foreach_docsis_packet_type;
#undef _

    default:
      return format (s, "unknown 0x%x", t);
    }
  
  return format (s, "%s", n);
}

static u8 * format_docsis_control_packet_type (u8 * s, va_list * va)
{
  docsis_control_packet_type_t t = va_arg (*va, docsis_control_packet_type_t);
  char * n = 0;

  switch (t)
    {
#define _(f,x) case DOCSIS_CONTROL_PACKET_TYPE_##f: n = #f; break;
      foreach_docsis_control_packet_type;
#undef _

    default:
      return format (s, "unknown 0x%x", t);
    }
  
  return format (s, "%s", n);
}

static u8 * format_docsis_packet_header (u8 * s, va_list * va)
{
  docsis_packet_header_t h = va_arg (*va, docsis_packet_header_t);

  if (h.packet_type == DOCSIS_PACKET_TYPE_control)
    s = format (s, "%U",
		format_docsis_control_packet_type,
		h.control_packet_type);
  else
    s = format (s, "%U",
		format_docsis_packet_type,
		h.packet_type);

  return s;
}

u8 * format_docsis_header_with_length (u8 * s, va_list * va)
{
  docsis_packet_t * d = va_arg (*va, docsis_packet_t *);
  u32 packet_len = va_arg (*va, u32);
  docsis_packet_header_t h;
  void * payload = docsis_packet_get_payload (d);
  uword indent = format_get_indent (s);

  h = d->generic.header;
  s = format (s, "DOCSIS: %U", format_docsis_packet_header, h);

  if (docsis_packet_header_is_ethernet_data_packet (h)
      || docsis_packet_header_is_management (h))
    s = format (s, "\n%U%U",
		format_white_space, indent,
		format_ethernet_header_with_length,
		payload,
		packet_len - (payload - (void *) d));

  if (docsis_packet_header_is_management (h))
    {
      ASSERT (0);
    }

  return s;
}

static uword unformat_docsis_packet_header (unformat_input_t * i, va_list * va)
{
  docsis_packet_header_t * h_result = va_arg (*va, docsis_packet_header_t *);
  docsis_management_packet_t * m_result = va_arg (*va, docsis_management_packet_t *);
  docsis_packet_header_t h;
  docsis_management_packet_type_t m;
  u8 docsis_version;

  h.as_u8 = 0;
  m = 0;			/* invalid */
  docsis_version = ~0;

  if (0)
    { /* never */ }

#define _(f) else if (unformat (i, #f)) h.packet_type = DOCSIS_PACKET_TYPE_##f;
  foreach_docsis_packet_type
#undef _

#define _(f,n)						\
  else if (unformat (i, #f))				\
    {							\
      h.packet_type = DOCSIS_PACKET_TYPE_control;	\
      h.control_packet_type = n;			\
    }

  foreach_docsis_control_packet_type
#undef _

#define _(f,n,v)							\
  else if (unformat (i, #f))						\
    {									\
      h.packet_type = DOCSIS_PACKET_TYPE_control;			\
      h.control_packet_type = DOCSIS_CONTROL_PACKET_TYPE_management;	\
      m = n;								\
      docsis_version = v;						\
    }

  foreach_docsis_management_packet_type
#undef _

  else
    return 0;

  *h_result = h;
  m_result->type = m;
  m_result->docsis_version = docsis_version;
  return 1;
}

uword unformat_docsis_header (unformat_input_t * i, va_list * va)
{
  ASSERT (0);
  return 0;
}

typedef struct {
  pg_edit_t packet_type;
  pg_edit_t control_packet_type;
  pg_edit_t extended_header_present;
} pg_docsis_packet_header_t;

static void
pg_docsis_packet_header_init (pg_docsis_packet_header_t * e)
{
  pg_edit_init_bitfield (&e->extended_header_present,
			 docsis_packet_t,
			 generic.header.as_u8,
			 0, 1);
  pg_edit_init_bitfield (&e->control_packet_type,
			 docsis_packet_t,
			 generic.header.as_u8,
			 1, 5);
  pg_edit_init_bitfield (&e->packet_type,
			 docsis_packet_t,
			 generic.header.as_u8,
			 6, 2);
}

typedef struct {
  pg_edit_t n_bytes_in_extended_header;
  pg_edit_t n_bytes_in_payload_plus_extended_header;
} pg_docsis_packet_generic_t;

static void
pg_docsis_packet_header_generic_init (pg_docsis_packet_generic_t * e)
{
  pg_edit_init (&e->n_bytes_in_extended_header,
		docsis_packet_t, generic.n_bytes_in_extended_header);
  pg_edit_init (&e->n_bytes_in_payload_plus_extended_header,
		docsis_packet_t, generic.n_bytes_in_payload_plus_extended_header);
}

typedef struct {
  pg_edit_t ethernet_type;
  pg_edit_t ethernet_src_address;
  pg_edit_t ethernet_dst_address;

  pg_edit_t llc_dst_sap;
  pg_edit_t llc_src_sap;
  pg_edit_t llc_control;

  pg_edit_t docsis_version;
  pg_edit_t type;
} pg_docsis_management_packet_t;

static void
pg_docsis_management_packet_init (pg_docsis_management_packet_t * e)
{
  pg_edit_init (&e->ethernet_type,
		docsis_management_packet_t, ethernet.type);
  pg_edit_init (&e->ethernet_src_address,
		docsis_management_packet_t, ethernet.src_address);
  pg_edit_init (&e->ethernet_dst_address,
		docsis_management_packet_t, ethernet.dst_address);

  pg_edit_init (&e->llc_dst_sap,
		docsis_management_packet_t, llc.dst_sap);
  pg_edit_init (&e->llc_src_sap,
		docsis_management_packet_t, llc.src_sap);
  pg_edit_init (&e->llc_control,
		docsis_management_packet_t, llc.control);

  pg_edit_init (&e->docsis_version,
		docsis_management_packet_t, docsis_version);
  pg_edit_init (&e->type,
		docsis_management_packet_t, type_as_u8);
}

uword unformat_pg_docsis_header (unformat_input_t * i, va_list * va)
{
  pg_stream_t * pg_stream = va_arg (*va, pg_stream_t *);
  docsis_packet_t dp;
  docsis_packet_header_t dh;
  docsis_management_packet_t dm;
  pg_docsis_packet_header_t * he;
  pg_docsis_packet_generic_t * ge;
  pg_edit_t * crc_edit;
  u32 pg_edit_group_index, error, sizeof_edits, sizeof_packet;

  error = 1;

  memset (&dm, 0, sizeof (dm));
  if (! unformat_user (i, unformat_docsis_packet_header, &dh, &dm))
    goto done;

  /* Always include DOCSIS packet header. */
  sizeof_edits = 0;
  sizeof_packet = 0;

  switch (dh.packet_type)
    {
    case DOCSIS_PACKET_TYPE_atm:
      /* bye bye ATM. */
      goto done;

    case DOCSIS_PACKET_TYPE_control:
      /* other control packets are not supported for now. */
      if (! docsis_packet_header_is_management (dh))
	goto done;
      sizeof_packet += sizeof (docsis_management_packet_t);
      sizeof_edits += sizeof (pg_docsis_management_packet_t);
      break;

    default:
      break;
    }

  dp.generic.header = dh;
  memset (&dp, 0, sizeof (dp));
  dp.generic.n_bytes_in_extended_header = 0;
  dp.generic.n_bytes_in_payload_plus_extended_header = 0;

  sizeof_packet += STRUCT_SIZE_OF (docsis_packet_t, generic);
  sizeof_edits += (sizeof (pg_docsis_packet_header_t)
		   + sizeof (pg_docsis_packet_generic_t)
		   /* docsis crc */
		   + sizeof (pg_edit_t));

  he = pg_create_edit_group
    (pg_stream,
     sizeof_edits,
     sizeof_packet,
     &pg_edit_group_index);

  {
    unformat_function_t * f;

    if (docsis_packet_header_is_management (dh))
      {
	pg_docsis_management_packet_t * me = (void *) (crc_edit + 1);
	pg_docsis_management_packet_init (me);
	pg_edit_set_fixed (&me->docsis_version, dm.docsis_version);
	pg_edit_set_fixed (&me->type, dm.type);
	f = unformat_pg_payload;
      }
    else
      f = unformat_pg_ethernet_header_with_crc;

    if (! unformat_user (i, f, pg_stream))
      goto done;
  }

  he = pg_get_edit_group (pg_stream, pg_edit_group_index);
  pg_docsis_packet_header_init (he);

  pg_edit_set_fixed (&he->packet_type, dh.packet_type);
  pg_edit_set_fixed (&he->control_packet_type, dh.control_packet_type);
  pg_edit_set_fixed (&he->extended_header_present, dh.extended_header_present);

  ge = (void *) (he + 1);
  pg_docsis_packet_header_generic_init (ge);
  pg_edit_set_fixed (&ge->n_bytes_in_extended_header, dp.generic.n_bytes_in_extended_header);

  if (pg_stream->min_packet_bytes == pg_stream->max_packet_bytes
      && pg_edit_group_index + 1 < vec_len (pg_stream->edit_groups))
    {
      u32 nb;

      nb = (pg_edit_group_n_bytes (pg_stream, pg_edit_group_index)
	    - sizeof_packet
	    + dp.generic.n_bytes_in_payload_plus_extended_header);
      pg_edit_set_fixed (&ge->n_bytes_in_payload_plus_extended_header, nb);
      dp.generic.n_bytes_in_payload_plus_extended_header
	= clib_host_to_net_u16 (nb);
    }

  else
    /* fixme implement edit function to set length. */
    goto done;

  crc_edit = (void *) (ge + 1);
  pg_edit_init (crc_edit, docsis_packet_t, generic.expected_header_crc);
  pg_edit_set_fixed (crc_edit, docsis_header_crc_itu_t (0, dp.as_u8, sizeof (dp.generic) - sizeof (u16)));

  error = 0;

 done:
  if (error)
    pg_free_edit_group (pg_stream);
  return error == 0;
}
