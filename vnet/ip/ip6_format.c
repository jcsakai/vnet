/*
 * ip/ip6_format.c: ip6 formatting
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

/* Format an IP6 address. */
u8 * format_ip6_address (u8 * s, va_list * args)
{
  ip6_address_t * a = va_arg (*args, ip6_address_t *);
  u32 i, i_max_n_zero, max_n_zeros, i_first_zero, n_zeros, last_double_colon;

  i_max_n_zero = ARRAY_LEN (a->as_u16);
  max_n_zeros = 0;
  i_first_zero = i_max_n_zero;
  n_zeros = 0;
  for (i = 0; i < ARRAY_LEN (a->as_u16); i++)
    {
      u32 is_zero = a->as_u16[i] == 0;
      if (is_zero && i_first_zero >= ARRAY_LEN (a->as_u16))
	{
	  i_first_zero = i;
	  n_zeros = 0;
	}
      n_zeros += is_zero;
      if ((! is_zero && n_zeros > max_n_zeros)
	  || (i + 1 >= ARRAY_LEN (a->as_u16) && n_zeros > max_n_zeros))
	{
	  i_max_n_zero = i_first_zero;
	  max_n_zeros = n_zeros;
	  i_first_zero = ARRAY_LEN (a->as_u16);
	  n_zeros = 0;
	}
    }

  last_double_colon = 0;
  for (i = 0; i < ARRAY_LEN (a->as_u16); i++)
    {
      if (i == i_max_n_zero && max_n_zeros > 1)
	{
	  s = format (s, "::");
	  i += max_n_zeros - 1;
	  last_double_colon = 1;
	}
      else
	{
	  s = format (s, "%s%x",
		      (last_double_colon || i == 0) ? "" : ":",
		      clib_net_to_host_u16 (a->as_u16[i]));
	  last_double_colon = 0;
	}
    }

  return s;
}

/* Format an IP6 route destination and length. */
u8 * format_ip6_address_and_length (u8 * s, va_list * args)
{
  ip6_address_t * a = va_arg (*args, ip6_address_t *);
  u8 l = va_arg (*args, u32);
  return format (s, "%U/%d", format_ip6_address, a, l);
}

/* Parse an IP6 address. */
uword unformat_ip6_address (unformat_input_t * input, va_list * args)
{
  ip6_address_t * result = va_arg (*args, ip6_address_t *);
  u16 hex_quads[8];
  uword hex_quad, n_hex_quads, hex_digit, n_hex_digits;
  uword c, n_colon, double_colon_index;

  n_hex_quads = hex_quad = n_hex_digits = n_colon = 0;
  double_colon_index = ARRAY_LEN (hex_quads);
  while ((c = unformat_get_input (input)) != UNFORMAT_END_OF_INPUT)
    {
      hex_digit = 16;
      if (c >= '0' && c <= '9')
	hex_digit = c - '0';
      else if (c >= 'a' && c <= 'f')
	hex_digit = c + 10 - 'a';
      else if (c >= 'A' && c <= 'F')
	hex_digit = c + 10 - 'A';
      else if (c == ':' && n_colon < 2)
	n_colon++;
      else
	{
	  unformat_put_input (input);
	  break;
	}

      /* Too many hex quads. */
      if (n_hex_quads >= ARRAY_LEN (hex_quads))
	return 0;

      if (hex_digit < 16)
	{
	  hex_quad = (hex_quad << 4) | hex_digit;

	  /* Hex quad must fit in 16 bits. */
	  if (n_hex_digits >= 4)
	    return 0;

	  n_colon = 0;
	  n_hex_digits++;
	}
      
      /* Save position of :: */
      if (n_colon == 2)
	{
	  /* More than one :: ? */
	  if (double_colon_index < ARRAY_LEN (hex_quads))
	    return 0;
	  double_colon_index = n_hex_quads;
	}

      if (n_colon > 0 && n_hex_digits > 0)
	{
	  hex_quads[n_hex_quads++] = hex_quad;
	  hex_quad = 0;
	  n_hex_digits = 0;
	}
    }

  if (n_hex_digits > 0)
    hex_quads[n_hex_quads++] = hex_quad;

  {
    word i;

    /* Expand :: to appropriate number of zero hex quads. */
    if (double_colon_index < ARRAY_LEN (hex_quads))
      {
	word n_zero = ARRAY_LEN (hex_quads) - n_hex_quads;

	for (i = n_hex_quads - 1; i >= (signed) double_colon_index; i--)
	  hex_quads[n_zero + i] = hex_quads[i];

	for (i = 0; i < n_zero; i++)
	  hex_quads[double_colon_index + i] = 0;

	n_hex_quads = ARRAY_LEN (hex_quads);
      }

    /* Too few hex quads given. */
    if (n_hex_quads < ARRAY_LEN (hex_quads))
      return 0;

    for (i = 0; i < ARRAY_LEN (hex_quads); i++)
      result->as_u16[i] = clib_host_to_net_u16 (hex_quads[i]);

    return 1;
  }
}

/* Format an IP6 header. */
u8 * format_ip6_header (u8 * s, va_list * args)
{
  ip6_header_t * ip = va_arg (*args, ip6_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);
  u32 i, ip_version, traffic_class, flow_label;
  uword indent;

  /* Nothing to do. */
  if (max_header_bytes < sizeof (ip[0]))
    return format (s, "IP header truncated");

  indent = format_get_indent (s);
  indent += 2;

  s = format (s, "%U: %U -> %U",
	      format_ip_protocol, ip->protocol,
	      format_ip6_address, &ip->src_address,
	      format_ip6_address, &ip->dst_address);

  i = clib_net_to_host_u32 (ip->ip_version_traffic_class_and_flow_label);
  ip_version = (i >> 28);
  traffic_class = (i >> 20) & 0xff;
  flow_label = i & pow2_mask (20);

  if (ip_version != 6)
    s = format (s, "\n%Uversion %d",
		format_white_space, indent, ip_version);
    
  s = format (s, "\n%Utos 0x%02x, flow label 0x%x, hop limit %d, payload length %d",
	      format_white_space, indent,
	      traffic_class, flow_label, ip->hop_limit,
	      clib_net_to_host_u16 (ip->payload_length));

  /* Recurse into next protocol layer. */
  if (max_header_bytes != 0 && sizeof (ip[0]) < max_header_bytes)
    {
      ip_main_t * im = &ip_main;
      ip_protocol_info_t * pi = ip_get_protocol_info (im, ip->protocol);

      if (pi && pi->format_header)
	s = format (s, "\n%U%U",
		    format_white_space, indent - 2,
		    pi->format_header,
		    /* next protocol header */ (void*) (ip + 1),
		    max_header_bytes - sizeof (ip[0]));
    }

  return s;
}

/* Parse an IP6 header. */
uword unformat_ip6_header (unformat_input_t * input, va_list * args)
{
  u8 ** result = va_arg (*args, u8 **);
  ip6_header_t * ip;
  int old_length;

  /* Allocate space for IP header. */
  {
    void * p;

    old_length = vec_len (*result);
    vec_add2 (*result, p, sizeof (ip[0]));
    ip = p;
  }

  memset (ip, 0, sizeof (ip[0]));
  ip->ip_version_traffic_class_and_flow_label = clib_host_to_net_u32 (6 << 28);

  if (! unformat (input, "%U: %U -> %U",
		  unformat_ip_protocol, &ip->protocol,
		  unformat_ip6_address, &ip->src_address,
		  unformat_ip6_address, &ip->dst_address))
    return 0;

  /* Parse options. */
  while (1)
    {
      int i;

      if (unformat (input, "tos %U", unformat_vlib_number, &i))
	ip->ip_version_traffic_class_and_flow_label |= clib_host_to_net_u32 ((i & 0xff) << 20);

      else if (unformat (input, "hop-limit %U", unformat_vlib_number, &i))
	ip->hop_limit = i;

      /* Can't parse input: try next protocol level. */
      else
	break;
    }

  /* Recurse into next protocol layer. */
  {
    ip_main_t * im = &ip_main;
    ip_protocol_info_t * pi = ip_get_protocol_info (im, ip->protocol);

    if (pi && pi->unformat_header)
      {
	if (! unformat_user (input, pi->unformat_header, result))
	  return 0;

	/* Result may have moved. */
	ip = (void *) *result + old_length;
      }
  }

  ip->payload_length = clib_host_to_net_u16 (vec_len (*result) - (old_length + sizeof (ip[0])));

  return 1;
}
