/*
 * ethernet_interface.c: ethernet interfaces
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
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>

static clib_error_t *
ethernet_interface_link_up_down (vlib_main_t * vm,
				 u32 hw_if_index,
				 u32 flags);

VLIB_HW_INTERFACE_CLASS (ethernet_hw_interface_class) = {
  .name = "Ethernet",
  .format_address = format_ethernet_address,
  .format_header = format_ethernet_header_with_length,
  .link_up_down_function = ethernet_interface_link_up_down,
  .hw_address_len = 6,
  .unformat_hw_address = unformat_ethernet_address,
  .unformat_header = unformat_ethernet_header,
};

uword unformat_ethernet_interface (unformat_input_t * input, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  u32 * result = va_arg (*args, u32 *);
  u32 hw_if_index;
  ethernet_main_t * em = ethernet_get_main (vm);
  ethernet_interface_t * eif;

  if (! unformat_user (input, unformat_vlib_hw_interface, vm, &hw_if_index))
    return 0;

  eif = ethernet_get_interface (em, hw_if_index);
  if (eif)
    {
      *result =  hw_if_index;
      return 1;
    }
  return 0;
}

static void ethernet_interface_update_media (ethernet_interface_t * ei,
					     vlib_hw_interface_t * hi)
{
  switch (ei->phy.media.type)
    {
    case ETHERNET_MEDIA_1000T:
    case ETHERNET_MEDIA_1000X:
      hi->max_rate_bits_per_sec = 1e9;
      break;

    case ETHERNET_MEDIA_100TX:
    case ETHERNET_MEDIA_100T4:
      hi->max_rate_bits_per_sec = 100e6;
      break;

    case ETHERNET_MEDIA_10T:
      hi->max_rate_bits_per_sec = 10e6;
      break;

    default:
      break;
    }
}

clib_error_t *
ethernet_register_interface (vlib_main_t * vm,
			     u32 dev_class_index,
			     u32 dev_instance,
			     u8 * address,
			     ethernet_phy_t * phy,
			     u32 * hw_if_index_return)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  ethernet_interface_t * ei;
  vlib_hw_interface_t * hi;
  clib_error_t * error = 0;
  u32 hw_if_index;

  pool_get (em->interfaces, ei);

  if (phy)
    {
      ei->phy.vlib_main = vm;
      ei->phy.opaque = phy->opaque;
      ei->phy.read_write = phy->read_write;
      ei->phy.phy_address = phy->phy_address;
      error = ethernet_phy_init (&ei->phy);
      if (error)
	goto done;
    }

  hw_if_index = vlib_register_interface (vm,
					 dev_class_index, dev_instance,
					 ethernet_hw_interface_class.index,
					 ei - em->interfaces);
  *hw_if_index_return = hw_if_index;

  hi = vlib_get_hw_interface (vm, hw_if_index);

  ethernet_setup_node (vm, hi->output_node_index);

  hi->min_packet_bytes = 64;
  hi->per_packet_overhead_bytes =
    /* preamble */ 8 + /* inter frame gap */ 12;

  ethernet_interface_update_media (ei, hi);

  memcpy (ei->address, address, sizeof (ei->address));

 done:
  if (error)
    {
      pool_put (em->interfaces, ei);
    }

  return error;
}
			     
static clib_error_t *
ethernet_interface_link_up_down (vlib_main_t * vm,
				 u32 hw_if_index,
				 u32 flags)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  vlib_hw_interface_t * hi;
  ethernet_interface_t * ei;
  u32 sw_if_index;

  hi = vlib_get_hw_interface (vm, hw_if_index);
  ei = ethernet_get_interface (em, hw_if_index);

  if (flags & VLIB_HW_INTERFACE_FLAG_LINK_UP)
    ethernet_interface_update_media (ei, hi);

  sw_if_index = hi->sw_if_index;
  vec_validate (em->vlan_mapping_by_sw_if_index, sw_if_index);

  /* Initialize vlan mapping table to all vlans disabled. */
  {
    ethernet_vlan_mapping_t * m;

    m = em->vlan_mapping_by_sw_if_index + sw_if_index;
    vec_validate_init_empty (m->vlan_to_sw_if_index,
			     ETHERNET_N_VLAN,
			     sw_if_index);
    m->vlan_to_sw_if_index[ETHERNET_N_VLAN] = sw_if_index;
  }

  return 0;
}

