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

#include <vnet/vnet.h>

u8 * format_vnet_rewrite (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  vnet_rewrite_header_t * rw = va_arg (*args, vnet_rewrite_header_t *);
  u32 max_data_bytes = va_arg (*args, u32);
  vnet_main_t * vnm = &vnet_main;
  vlib_node_t * next;
  uword indent;

  next = vlib_get_next_node (vm, rw->node_index, rw->next_index);

  indent = format_get_indent (s);

  if (rw->sw_if_index != ~0)
    {
      vnet_sw_interface_t * si;
      si = vnet_get_sw_interface (vnm, rw->sw_if_index);
      s = format (s, "%U", format_vnet_sw_interface_name, vnm, si);
    }
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
  vnet_main_t * vnm = &vnet_main;
  vlib_node_t * next;
  vnet_hw_interface_class_t * hc = 0;
  vnet_hw_interface_t * hi = 0;
  u32 next_index, sw_if_index, max_packet_bytes, error;
  u8 * rw_data;

  rw_data = 0;
  sw_if_index = ~0;
  max_packet_bytes = ~0;
  error = 1;

  /* Parse sw interface. */
  if (unformat (input, "%U",
		unformat_vnet_sw_interface, vnm, &sw_if_index))
    {
      vnet_sw_interface_t * si = vnet_get_sw_interface (vnm, sw_if_index);

      next_index = ~0;
      if (si->type == VNET_SW_INTERFACE_TYPE_HARDWARE)
	{
	  hi = vnet_get_hw_interface (vnm, si->hw_if_index);
	  hc = vnet_get_hw_interface_class (vnm, hi->hw_class_index);

	  next_index =
	    (hc->rewrite_fixup_node_index != ~0
	     ? hc->rewrite_fixup_node_index
	     : hi->output_node_index);

	  max_packet_bytes = hi->max_l3_packet_bytes[VLIB_RX];
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

  else if (unformat_user (input, unformat_hex_string, &rw_data)
	   || unformat (input, "0x%U", unformat_hex_string, &rw_data))
    ;
      
  else
    goto done;

  /* Re-write does not fit. */
  if (vec_len (rw_data) >= max_data_bytes)
    goto done;

  {
    u32 tmp;

    if (unformat (input, "mtu %d", &tmp)
	&& tmp < (1 << BITS (rw->max_l3_packet_bytes)))
      max_packet_bytes = tmp;
  }

  error = 0;
  rw->sw_if_index = sw_if_index;
  rw->max_l3_packet_bytes = max_packet_bytes;
  rw->next_index = vlib_node_add_next (vm, rw->node_index, next_index);
  vnet_rewrite_set_data_internal (rw, max_data_bytes, rw_data, vec_len (rw_data));

  if (hc && hc->rewrite_for_hw_interface)
    hc->rewrite_for_hw_interface (vnm, hi->hw_if_index, rw_data);

 done:
  vec_free (rw_data);
  return error == 0;
}

void vnet_rewrite_for_sw_interface (vnet_main_t * vnm,
				    vnet_l3_packet_type_t packet_type,
				    u32 sw_if_index,
				    u32 node_index,
				    void * dst_address,
				    vnet_rewrite_header_t * rw,
				    u32 max_rewrite_bytes)
{
  vlib_main_t * vm = vnm->vlib_main;
  vnet_hw_interface_t * hw = vnet_get_sup_hw_interface (vnm, sw_if_index);
  vnet_hw_interface_class_t * hc = vnet_get_hw_interface_class (vnm, hw->hw_class_index);
  static u8 * rw_tmp = 0;
  u32 n_rw_tmp, next_node_index;

  rw->sw_if_index = sw_if_index;
  rw->node_index = node_index;

  next_node_index = hc->rewrite_fixup_node_index != ~0 ? hc->rewrite_fixup_node_index : hw->output_node_index;

  rw->next_index = vlib_node_add_next (vm, node_index, next_node_index);

  rw->max_l3_packet_bytes = hw->max_l3_packet_bytes[VLIB_TX];

  ASSERT (max_rewrite_bytes > 0);
  vec_reset_length (rw_tmp);
  vec_validate (rw_tmp, max_rewrite_bytes - 1);

  ASSERT (hc->rewrite_for_sw_interface);
  n_rw_tmp = hc->rewrite_for_sw_interface
    (vnm, sw_if_index, packet_type, dst_address, rw_tmp, max_rewrite_bytes);

  if (hc->rewrite_for_hw_interface)
    hc->rewrite_for_hw_interface (vnm, hw->hw_if_index, rw_tmp);

  ASSERT (n_rw_tmp > 0 && n_rw_tmp < max_rewrite_bytes);
  vnet_rewrite_set_data_internal (rw, max_rewrite_bytes, rw_tmp, n_rw_tmp);
}

void serialize_vnet_rewrite (serialize_main_t * m, va_list * va)
{
  vnet_rewrite_header_t * rw = va_arg (*va, vnet_rewrite_header_t *);
  u32 max_data_bytes = va_arg (*va, u32);
  u8 * p;

  serialize_integer (m, rw->sw_if_index, sizeof (rw->sw_if_index));
  serialize_integer (m, rw->data_bytes, sizeof (rw->data_bytes));
  serialize_integer (m, rw->max_l3_packet_bytes, sizeof (rw->max_l3_packet_bytes));
  p = serialize_get (m, rw->data_bytes);
  memcpy (p, vnet_rewrite_get_data_internal (rw, max_data_bytes), rw->data_bytes);
}

void unserialize_vnet_rewrite (serialize_main_t * m, va_list * va)
{
  vnet_rewrite_header_t * rw = va_arg (*va, vnet_rewrite_header_t *);
  u32 max_data_bytes = va_arg (*va, u32);
  u8 * p;

  /* It is up to user to fill these in. */
  rw->node_index = ~0;
  rw->next_index = ~0;

  unserialize_integer (m, &rw->sw_if_index, sizeof (rw->sw_if_index));
  unserialize_integer (m, &rw->data_bytes, sizeof (rw->data_bytes));
  unserialize_integer (m, &rw->max_l3_packet_bytes, sizeof (rw->max_l3_packet_bytes));
  p = unserialize_get (m, rw->data_bytes);
  memcpy (vnet_rewrite_get_data_internal (rw, max_data_bytes), p, rw->data_bytes);
}
