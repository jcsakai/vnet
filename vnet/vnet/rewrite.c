/*
 * rewrite.c: packet rewrite
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

#include <vnet/vnet/rewrite.h>

u8 * format_vnet_rewrite (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  vnet_rewrite_header_t * rw = va_arg (*args, vnet_rewrite_header_t *);
  vlib_sw_interface_t * si = vlib_get_sw_interface (vm, rw->sw_if_index);
  u32 max_data_bytes = va_arg (*args, u32);
  vlib_node_t * next;
  uword indent;

  next = vlib_get_next_node (vm, rw->node_index, rw->next_index);

  indent = format_get_indent (s);

  if (rw->sw_if_index != ~0)
    s = format (s, "%U", format_vlib_sw_interface_name, vm, si);
  else
    s = format (s, "%v", next->name);

  /* Format rewrite string. */
  if (rw->data_bytes > 0)
    s = format (s, "\n%U%U",
		format_white_space, indent,
		next->format_buffer ? next->format_buffer : format_hex_bytes,
		rw->data + max_data_bytes - rw->data_bytes,
		rw->data_bytes);

  return s;
}

u8 * format_vnet_rewrite_header (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  vnet_rewrite_header_t * rw = va_arg (*args, vnet_rewrite_header_t *);
  u8 * packet_data = va_arg (*args, u8 *);
  u32 packet_data_bytes = va_arg (*args, u32);
  vlib_node_t * next;

  next = vlib_get_next_node (vm, rw->node_index, rw->next_index);

  /* Format rewrite string. */
  s = format (s, "%U",
	      next->format_buffer ? next->format_buffer : format_hex_bytes,
	      packet_data, packet_data_bytes);

  return s;
}

uword unformat_vnet_rewrite (unformat_input_t * input, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  vnet_rewrite_header_t * rw = va_arg (*args, vnet_rewrite_header_t *);
  u32 max_data_bytes = va_arg (*args, u32);
  vlib_node_t * next;
  u32 next_index, sw_if_index, max_packet_bytes, error;
  u8 * rw_data;

  rw_data = 0;
  sw_if_index = ~0;
  max_packet_bytes = ~0;
  error = 1;

  /* Parse sw interface. */
  if (unformat (input, "%U",
		unformat_vlib_sw_interface, vm, &sw_if_index))
    {
      vlib_sw_interface_t * si = vlib_get_sw_interface (vm, sw_if_index);

      next_index = ~0;
      if (si->type == VLIB_SW_INTERFACE_TYPE_HARDWARE)
	{
	  vlib_hw_interface_t * hi;

	  hi = vlib_get_hw_interface (vm, si->hw_if_index);

	  next_index = hi->output_node_index;
	  max_packet_bytes = hi->max_packet_bytes[VLIB_RX];
	}
      else
	ASSERT (0);
    }

  else if (unformat (input, "%U",
		     unformat_vlib_node, vm, &next_index))
    ;

  else
    goto done;

  next = vlib_get_node (vm, next_index);

  if (next->unformat_buffer
      && unformat_user (input, next->unformat_buffer, &rw_data))
    ;

  else if (unformat_user (input, unformat_hex_string, &rw_data))
    ;
      
  else
    goto done;

  /* Re-write does not fit. */
  if (vec_len (rw_data) >= max_data_bytes)
    goto done;

  {
    u32 tmp;

    if (unformat (input, "mtu %d", &tmp)
	&& tmp < (1 << BITS (rw->max_packet_bytes)))
      max_packet_bytes = tmp;
  }

  error = 0;
  rw->sw_if_index = sw_if_index;
  rw->max_packet_bytes = max_packet_bytes;
  rw->next_index = vlib_node_add_next (vm, rw->node_index, next_index);
  vnet_rewrite_set_data_internal (rw, max_data_bytes, rw_data, vec_len (rw_data));

 done:
  vec_free (rw_data);
  return error == 0;
}
