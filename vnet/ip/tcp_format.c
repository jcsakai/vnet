/*
 * ip/tcp_format.c: tcp formatting
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

static u8 * format_tcp_flags (u8 * s, va_list * args)
{
  int flags = va_arg (*args, int);

#define _(f) if (flags & TCP_FLAG_##f) s = format (s, "%s, ", #f);
  foreach_tcp_flag
#undef _

  return s;
}

/* Format TCP header. */
u8 * format_tcp_header (u8 * s, va_list * args)
{
  tcp_header_t * tcp = va_arg (*args, tcp_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);
  u16 data_offset_and_flags;
  u32 header_bytes;
  uword indent;

  /* Nothing to do. */
  if (max_header_bytes < sizeof (tcp[0]))
    return format (s, "TCP header truncated");

  indent = format_get_indent (s);
  indent += 2;

  s = format (s, "TCP: %U -> %U",
	      format_tcp_udp_port, tcp->ports.src,
	      format_tcp_udp_port, tcp->ports.dst);

  data_offset_and_flags = clib_net_to_host_u16 (tcp->data_offset_and_flags);
  s = format (s, "\n%Useq. tx 0x%08x rx 0x%08x",
	      format_white_space, indent,
	      clib_net_to_host_u32 (tcp->seq_number),
	      clib_net_to_host_u32 (tcp->ack_number));

  s = format (s, "\n%Uflags %U",
	      format_white_space, indent,
	      format_tcp_flags, data_offset_and_flags);

  s = format (s, "\n%Uwindow %d, checksum 0x%04x",
	      format_white_space, indent,
	      clib_net_to_host_u16 (tcp->window),
	      clib_net_to_host_u16 (tcp->checksum));

  header_bytes = tcp_header_bytes (tcp);

  /* Format TCP options. */
#if 0
  {
    u8 * o;
    u8 * option_start = (void *) (tcp + 1);
    u8 * option_end = (void *) tcp + header_bytes;

    for (o = option_start; o < option_end; )
      {
	u32 length = o[1];
	switch (o[0])
	  {
	  case TCP_OPTION_END:
	    length = 1;
	    o = option_end;
	    break;

	  case TCP_OPTION_NOP:
	    length = 1;
	    break;

	  }
      }
  }
#endif

  /* Recurse into next protocol layer. */
  if (max_header_bytes != 0 && header_bytes < max_header_bytes)
    {
      ip_main_t * im = &ip_main;
      tcp_udp_port_info_t * pi;

      pi = ip_get_tcp_udp_port_info (im, tcp->ports.dst);

      if (pi && pi->format_header)
	s = format (s, "\n%U%U",
		    format_white_space, indent - 2,
		    pi->format_header,
		    /* next protocol header */ (void*) tcp + header_bytes,
		    max_header_bytes - header_bytes);
    }

  return s;
}
