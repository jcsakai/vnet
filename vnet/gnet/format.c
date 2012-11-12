/*
 * gnet/format.c: gnet formatting/parsing.
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

#include <vlib/vlib.h>
#include <vnet/gnet/gnet.h>
#include <vnet/ethernet/ethernet.h>

u8 * format_gnet_address (u8 * s, va_list * args)
{
  gnet_address_t * a = va_arg (*args, gnet_address_t *);
  u8 u[4];

  gnet_unpack_address (a, u);

  s = format (s, "%d/%d/%d/%d", u[3], u[2], u[1], u[0]);

  return s;
}

u8 * format_gnet_header_with_length (u8 * s, va_list * args)
{
  gnet_header_t * h = va_arg (*args, gnet_header_t *);
  u32 max_header_bytes = va_arg (*args, u32);
  ethernet_main_t * em = &ethernet_main;
  uword indent, header_bytes;

  header_bytes = sizeof (h[0]);
  if (max_header_bytes != 0 && header_bytes > max_header_bytes)
    return format (s, "gnet header truncated");

  indent = format_get_indent (s);

  s = format (s, "\n%U%U -> %U",
	      format_white_space, indent,
	      format_ethernet_type, clib_net_to_host_u16 (h->type),
	      format_gnet_address, &h->dst_address);

  if (max_header_bytes != 0 && header_bytes < max_header_bytes)
    {
      ethernet_type_info_t * ti;
      vlib_node_t * node;

      ti = ethernet_get_type_info (em, h->type);
      node = ti ? vlib_get_node (em->vlib_main, ti->node_index) : 0;
      if (node && node->format_buffer)
	s = format (s, "\n%U%U",
		    format_white_space, indent,
		    node->format_buffer, (void *) h + header_bytes,
		    max_header_bytes - header_bytes);
    }

  return s;
}

u8 * format_gnet_header (u8 * s, va_list * args)
{
  gnet_header_t * m = va_arg (*args, gnet_header_t *);
  return format (s, "%U", format_gnet_header_with_length, m, 0);
}

uword
unformat_gnet_address (unformat_input_t * input, va_list * args)
{
  gnet_address_t * a = va_arg (*args, gnet_address_t *);
  u32 u[4];
  u8 v[4];

  if (! unformat (input, "%d/%d/%d/%d", &u[3], &u[2], &u[1], &u[0]))
    return 0;
  if ((u[0] | u[1] | u[2] | u[3]) & ~0x3f)
    return 0;
  v[0] = u[0]; v[1] = u[1]; v[2] = u[2]; v[3] = u[3];
  gnet_pack_address (a, v);
  return 1;
}

uword
unformat_gnet_header (unformat_input_t * input, va_list * args)
{
  u8 ** result = va_arg (*args, u8 **);
  gnet_header_t * h;

  {
    void * p;
    vec_add2 (*result, p, sizeof (h[0]));
    h = p;
  }

  memset (h, 0, sizeof (h[0]));

  if (! unformat (input, "%U: -> %U",
		  unformat_ethernet_type_net_byte_order, &h->type,
		  unformat_gnet_address, &h->dst_address))
    return 0;

  /* Should always be set to coexist with ethernet packets. */
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "control"))
	h->is_control = 1;
      
      else
	return 0;
    }

  return 1;
}

static u8 * format_gnet_interface (u8 * s, va_list * args)
{
  gnet_interface_t * gi = va_arg (*args, gnet_interface_t *);

  s = format (s, "address %U",
	      format_gnet_address, gi->address);

  return s;
}

u8 * format_gnet_device (u8 * s, va_list * args)
{
  u32 hw_if_index = va_arg (*args, u32);
  vnet_main_t * vnm = &vnet_main;
  gnet_main_t * gm = &gnet_main;
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vnm, hw_if_index);
  gnet_interface_t * gi = pool_elt_at_index (gm->interface_pool, hi->hw_instance);
  return format (s, "%U", format_gnet_interface, gi);
}
