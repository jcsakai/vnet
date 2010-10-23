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
#include <vnet/vnet/l3_types.h>
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>

static clib_error_t *
ethernet_interface_link_up_down (vlib_main_t * vm,
				 u32 hw_if_index,
				 u32 flags);

static uword ethernet_set_rewrite (vlib_main_t * vm,
				   u32 sw_if_index,
				   u32 l3_type,
				   void * rewrite,
				   uword max_rewrite_bytes)
{
  vlib_sw_interface_t * sub_sw = vlib_get_sw_interface (vm, sw_if_index);
  vlib_sw_interface_t * sup_sw = vlib_get_sup_sw_interface (vm, sw_if_index);
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  ethernet_main_t * em = ethernet_get_main (vm);
  ethernet_interface_t * ei;
  ethernet_header_t * h = rewrite;
  ethernet_type_t type;
  uword n_bytes = sizeof (h[0]);

  if (sub_sw != sup_sw)
    n_bytes += sizeof (ethernet_vlan_header_t);

  if (n_bytes > max_rewrite_bytes)
    return 0;

  switch (l3_type) {
#define _(a,b) case VNET_L3_PACKET_TYPE_##a: type = ETHERNET_TYPE_##b; break
    _ (IP4, IP4);
    _ (IP6, IP6);
    _ (MPLS_UNICAST, MPLS_UNICAST);
    _ (MPLS_MULTICAST, MPLS_MULTICAST);
#undef _
  default:
    return 0;
  }

  ei = pool_elt_at_index (em->interfaces, hw->hw_instance);
  memcpy (h->src_address, ei->address, sizeof (h->src_address));
  memset (h->dst_address, 0, sizeof (h->dst_address));

  if (sub_sw != sup_sw)
    {
      ethernet_vlan_header_t * vh = (void *) (h + 1);

      h->type = clib_host_to_net_u16 (ETHERNET_TYPE_VLAN);
      ASSERT (sub_sw->sub.id < 4096);
      vh->priority_cfi_and_id = clib_host_to_net_u16 (sub_sw->sub.id);
      vh->type = clib_host_to_net_u16 (type);
    }
  else
    h->type = clib_host_to_net_u16 (type);

  return n_bytes;
}

static u8 * format_ethernet_interface (u8 * s, va_list * args)
{
  u32 hw_instance = va_arg (*args, u32);
  ethernet_main_t * em = &ethernet_main;
  ethernet_interface_t * eif;

  eif = pool_elt_at_index (em->interfaces, hw_instance);

  s = format (s, "Ethernet: address %U",
	      format_ethernet_address, eif->address);

  return s;
}

VLIB_HW_INTERFACE_CLASS (ethernet_hw_interface_class) = {
  .name = "Ethernet",
  .format_address = format_ethernet_address,
  .format_header = format_ethernet_header_with_length,
  .format_device = format_ethernet_interface,
  .link_up_down_function = ethernet_interface_link_up_down,
  .hw_address_len = 6,
  .unformat_hw_address = unformat_ethernet_address,
  .unformat_header = unformat_ethernet_header,
  .set_rewrite = ethernet_set_rewrite,
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
			     
void
ethernet_delete_interface (vlib_main_t * vm, u32 hw_if_index)
{
  ethernet_main_t * em = ethernet_get_main (vm);
  ethernet_interface_t * ei;
  vlib_hw_interface_t * hi;

  hi = vlib_get_hw_interface (vm, hw_if_index);
  ei = pool_elt_at_index (em->interfaces, hi->hw_instance);

  /* Delete vlan mapping table. */
  if (hi->sw_if_index < vec_len (em->vlan_mapping_by_sw_if_index))
    vec_free (em->vlan_mapping_by_sw_if_index[hi->sw_if_index].vlan_to_sw_if_index);

  vlib_delete_hw_interface (vm, hw_if_index);
  pool_put (em->interfaces, ei);
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

#if DEBUG > 0

#define VNET_SIMULATED_ETHERNET_TX_NEXT_ETHERNET_INPUT VLIB_INTERFACE_TX_N_NEXT

/* Echo packets back to ethernet input. */
static uword
simulated_ethernet_interface_tx (vlib_main_t * vm,
				 vlib_node_runtime_t * node,
				 vlib_frame_t * frame)
{
  u32 n_left_from, n_left_to_next, n_copy, * from, * to_next;
  u32 next_index = VNET_SIMULATED_ETHERNET_TX_NEXT_ETHERNET_INPUT;
  u32 i;
  vlib_buffer_t * b;

  n_left_from = frame->n_vectors;
  from = vlib_frame_args (frame);

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      n_copy = clib_min (n_left_from, n_left_to_next);

      memcpy (to_next, from, n_copy * sizeof (from[0]));
      n_left_to_next -= n_copy;
      n_left_from -= n_copy;
      for (i = 0; i < n_copy; i++)
	{
	  b = vlib_get_buffer (vm, from[i]);
	  /* TX interface will be fake eth; copy to RX for benefit of ethernet-input. */
	  b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX];
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_left_from;
}

static u8 * format_simulated_ethernet_name (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  return format (s, "fake-eth%d", dev_instance);
}

static VLIB_DEVICE_CLASS (ethernet_simulated_device_class) = {
  .name = "Simulated ethernet",
  .format_device_name = format_simulated_ethernet_name,
  .tx_function = simulated_ethernet_interface_tx,
};

static clib_error_t *
create_simulated_ethernet_interfaces (vlib_main_t * vm,
				      unformat_input_t * input,
				      vlib_cli_command_t * cmd)
{
  clib_error_t * error;
  static u32 instance;
  u8 address[6];
  u32 hw_if_index;

  if (! unformat_user (input, unformat_ethernet_address, &address))
    {
      memset (address, 0, sizeof (address));
      address[0] = 0xde;
      address[1] = 0xad;
      address[5] = instance;
    }

  error = ethernet_register_interface
    (vm,
     ethernet_simulated_device_class.index,
     instance++,
     address,
     /* phy */ 0,
     &hw_if_index);

  {
    vlib_hw_interface_t * hw_if;
    u32 slot;

    hw_if = vlib_get_hw_interface (vm, hw_if_index);
    slot = vlib_node_add_named_next_with_slot
      (vm, hw_if->tx_node_index,
       "ethernet-input",
       VNET_SIMULATED_ETHERNET_TX_NEXT_ETHERNET_INPUT);
    ASSERT (slot == VNET_SIMULATED_ETHERNET_TX_NEXT_ETHERNET_INPUT);
  }

  return error;
}

static VLIB_CLI_COMMAND (create_simulated_ethernet_interface_command) = {
  .name = "create-ethernet",
  .short_help = "Create simulated ethernet interface",
  .parent = &vlib_cli_interface_command,
  .function = create_simulated_ethernet_interfaces,
};
#endif
