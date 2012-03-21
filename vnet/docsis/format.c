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

  h = d->generic.header;
  s = format (s, "%U", format_docsis_packet_header, h);

  if (docsis_packet_header_is_ethernet_data_packet (h)
      || docsis_packet_header_is_management (h))
    s = format (s, "%U",
		format_ethernet_header_with_length,
		payload,
		packet_len - (payload - (void *) d));

  if (docsis_packet_header_is_management (h))
    {
      ASSERT (0);
    }

  return s;
}

uword unformat_docsis_header (unformat_input_t * i, va_list * va)
{
  ASSERT (0);
  return 0;
}

uword unformat_pg_docsis_header (unformat_input_t * i, va_list * va)
{
  ASSERT (0);
  return 0;
}
