/*
 * docsis/interface.c: DOCSIS interfaces
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

static void
docsis_rewrite_for_hw_interface (vnet_main_t * vm, u32 hw_if_index, void * rewrite)
{
  docsis_rewrite_header_t * h = rewrite;
  vnet_hw_interface_t * hif;
  u32 ni;

  hif = vnet_get_hw_interface (vm, hw_if_index);
  ni = vlib_node_add_next (vm->vlib_main, docsis_fixup_rewrite_node.index, hif->output_node_index);
  docsis_rewrite_header_set_next_index (h, ni);
}

static uword
docsis_rewrite_for_sw_interface (vnet_main_t * vm,
				 u32 sw_if_index,
				 vnet_l3_packet_type_t l3_type,
				 void * dst_address,
				 void * rewrite,
				 uword max_rewrite_bytes)
{
  docsis_rewrite_header_t * h = rewrite;
  docsis_packet_t * dh = &h->docsis;
  ethernet_header_t * eh = &h->ethernet;
  ethernet_type_t ethernet_type;
  docsis_main_t * dm = &docsis_main;
  vnet_hw_interface_t * hif = vnet_get_sup_hw_interface (vm, sw_if_index);
  docsis_interface_t * dif = pool_elt_at_index (dm->interface_pool, hif->hw_instance);

  if (max_rewrite_bytes < sizeof (h[0]))
    return 0;

  switch (l3_type) {
#define _(a,b) case VNET_L3_PACKET_TYPE_##a: ethernet_type = ETHERNET_TYPE_##b; break
    _ (IP4, IP4);
    _ (IP6, IP6);
    _ (MPLS_UNICAST, MPLS_UNICAST);
    _ (MPLS_MULTICAST, MPLS_MULTICAST);
#undef _
  default:
    return 0;
  }

  memset (h, 0, sizeof (h[0]));

  dh->generic.header.packet_type = DOCSIS_PACKET_TYPE_ethernet;

  /* Compute CRC without including packet length.
     Length and CRC will be updated by rewrite fixup node. */
  dh->generic.expected_header_crc =
    docsis_header_crc_itu_t (0,
			     dh->as_u8,
			     (sizeof (docsis_packet_t)
			      - STRUCT_SIZE_OF (docsis_packet_t, generic.expected_header_crc)
			      - STRUCT_SIZE_OF (docsis_packet_t, generic.n_bytes_in_payload_plus_extended_header)));

  eh->type = clib_host_to_net_u16 (ethernet_type);

  memcpy (eh->src_address, dif->address, sizeof (eh->src_address));

  if (dst_address)
    memcpy (eh->dst_address, dst_address, sizeof (eh->dst_address));
  else
    memset (eh->dst_address, ~0, sizeof (eh->dst_address)); /* broadcast */
		     
  return sizeof (h[0]);
}

VNET_HW_INTERFACE_CLASS (docsis_hw_interface_class) = {
  .name = "DOCSIS",
  .rewrite_fixup_node = "docsis-fixup-rewrite",
  .format_header = format_docsis_header_with_length,
  .unformat_header = unformat_docsis_header,
  .rewrite_for_sw_interface = docsis_rewrite_for_sw_interface,
  .rewrite_for_hw_interface = docsis_rewrite_for_hw_interface,
};

clib_error_t *
docsis_register_interface (vnet_main_t * vm,
			   u32 dev_class_index,
			   u32 dev_instance,
			   u8 * address,
			   u32 * hw_if_index_return)
{
  docsis_main_t * dm = &docsis_main;
  docsis_interface_t * di;
  vnet_hw_interface_t * hi;
  u32 hw_if_index;

  pool_get (dm->interface_pool, di);

  hw_if_index = vnet_register_interface
    (vm,
     dev_class_index, dev_instance,
     docsis_hw_interface_class.index,
     di - dm->interface_pool);
  *hw_if_index_return = hw_if_index;

  hi = vnet_get_hw_interface (vm, hw_if_index);

  docsis_setup_node (vm->vlib_main, hi->output_node_index);

  /* Standard default docsis MTU. */
  hi->max_l3_packet_bytes[VLIB_RX] = hi->max_l3_packet_bytes[VLIB_TX] = 1500;

  memcpy (di->address, address, sizeof (di->address));
  vec_free (hi->hw_address);
  vec_add (hi->hw_address, address, sizeof (di->address));

  return /* no error */ 0;
}

#if CLIB_DEBUG > 0

#define VNET_SIMULATED_DOCSIS_TX_NEXT_DOCSIS_INPUT VNET_INTERFACE_TX_N_NEXT

/* Echo packets back to docsis input. */
static uword
simulated_docsis_interface_tx (vlib_main_t * vm,
			       vlib_node_runtime_t * node,
			       vlib_frame_t * frame)
{
  u32 n_left_from, n_left_to_next, n_copy, * from, * to_next;
  u32 next_index = VNET_SIMULATED_DOCSIS_TX_NEXT_DOCSIS_INPUT;
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
	  /* TX interface will be fake eth; copy to RX for benefit of docsis-input. */
	  vnet_buffer (b)->sw_if_index[VLIB_RX] = vnet_buffer (b)->sw_if_index[VLIB_TX];
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_left_from;
}

static u8 * format_simulated_docsis_name (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  return format (s, "fake-doc%d", dev_instance);
}

static VNET_DEVICE_CLASS (docsis_simulated_device_class) = {
  .name = "Simulated DOCSIS",
  .format_device_name = format_simulated_docsis_name,
  .tx_function = simulated_docsis_interface_tx,
};

static clib_error_t *
create_simulated_docsis_interfaces (vlib_main_t * vm,
				    unformat_input_t * input,
				    vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
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

  error = docsis_register_interface
    (vnm,
     docsis_simulated_device_class.index,
     instance++,
     address,
     &hw_if_index);

  {
    vnet_hw_interface_t * hw_if;
    u32 slot;

    hw_if = vnet_get_hw_interface (vnm, hw_if_index);
    slot = vlib_node_add_named_next_with_slot
      (vm, hw_if->tx_node_index,
       "docsis-input",
       VNET_SIMULATED_DOCSIS_TX_NEXT_DOCSIS_INPUT);
    ASSERT (slot == VNET_SIMULATED_DOCSIS_TX_NEXT_DOCSIS_INPUT);
  }

  return error;
}

static VLIB_CLI_COMMAND (create_simulated_docsis_interface_command) = {
  .path = "docsis create-interfaces",
  .short_help = "Create simulated docsis interface",
  .function = create_simulated_docsis_interfaces,
};
#endif
