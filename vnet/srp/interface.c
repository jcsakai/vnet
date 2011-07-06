/*
 * srp_interface.c: srp interfaces
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
#include <vnet/srp/srp.h>

static uword srp_set_rewrite (vlib_main_t * vm,
			      u32 sw_if_index,
			      u32 l3_type,
			      void * dst_address,
			      void * rewrite,
			      uword max_rewrite_bytes)
{
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  srp_main_t * sm = &srp_main;
  srp_and_ethernet_header_t * h = rewrite;
  u16 type;
  uword n_bytes = sizeof (h[0]);

  if (n_bytes > max_rewrite_bytes)
    return 0;

  switch (l3_type) {
#define _(a,b) case VNET_L3_PACKET_TYPE_##a: type = ETHERNET_TYPE_##b; break
    _ (IP4, IP4);
    _ (IP6, IP6);
    _ (MPLS_UNICAST, MPLS_UNICAST);
    _ (MPLS_MULTICAST, MPLS_MULTICAST);
    _ (ARP, ARP);
#undef _
  default:
    return 0;
  }

  memcpy (h->ethernet.src_address, hw->hw_address, sizeof (h->ethernet.src_address));
  if (dst_address)
    memcpy (h->ethernet.dst_address, dst_address, sizeof (h->ethernet.dst_address));
  else
    memset (h->ethernet.dst_address, ~0, sizeof (h->ethernet.dst_address)); /* broadcast */

  h->ethernet.type = clib_host_to_net_u16 (type);

  h->srp.as_u16 = 0;
  h->srp.mode = SRP_MODE_data;
  h->srp.ttl = sm->default_data_ttl;
  h->srp.parity = count_set_bits (h->srp.as_u16) ^ 1; /* odd parity */

  return n_bytes;
}

VLIB_HW_INTERFACE_CLASS (srp_hw_interface_class) = {
  .name = "srp",
  .format_address = format_ethernet_address,
  .format_header = format_srp_header_with_length,
  .unformat_hw_address = unformat_ethernet_address,
  .unformat_header = unformat_srp_header,
  .set_rewrite = srp_set_rewrite,
};

#if DEBUG > 0

#define VNET_SIMULATED_SRP_TX_NEXT_SRP_INPUT VLIB_INTERFACE_TX_N_NEXT

/* Echo packets back to srp input. */
static uword
simulated_srp_interface_tx (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  u32 n_left_from, n_left_to_next, n_copy, * from, * to_next;
  u32 next_index = VNET_SIMULATED_SRP_TX_NEXT_SRP_INPUT;
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
	  /* TX interface will be fake eth; copy to RX for benefit of srp-input. */
	  b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX];
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_left_from;
}

static u8 * format_simulated_srp_name (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  return format (s, "fake-srp%d", dev_instance);
}

static VLIB_DEVICE_CLASS (srp_simulated_device_class) = {
  .name = "Simulated srp",
  .format_device_name = format_simulated_srp_name,
  .tx_function = simulated_srp_interface_tx,
};

static clib_error_t *
create_simulated_srp_interfaces (vlib_main_t * vm,
				 unformat_input_t * input,
				 vlib_cli_command_t * cmd)
{
  u8 address[6];
  u32 hw_if_index;
  vlib_hw_interface_t * hi;
  static u32 instance;

  if (! unformat_user (input, unformat_ethernet_address, &address))
    {
      memset (address, 0, sizeof (address));
      address[0] = 0xde;
      address[1] = 0xad;
      address[5] = instance;
    }

  hw_if_index = vlib_register_interface (vm,
					 srp_simulated_device_class.index,
					 instance++,
					 srp_hw_interface_class.index, 0);

  hi = vlib_get_hw_interface (vm, hw_if_index);

  srp_setup_node (vm, hi->output_node_index);

  hi->min_packet_bytes = 40 + 16;

  /* Standard default ethernet MTU. */
  hi->max_l3_packet_bytes[VLIB_RX] = hi->max_l3_packet_bytes[VLIB_TX] = 1500;

  vec_free (hi->hw_address);
  vec_add (hi->hw_address, address, sizeof (address));

  {
    uword slot;

    slot = vlib_node_add_named_next_with_slot
      (vm, hi->tx_node_index,
       "srp-input",
       VNET_SIMULATED_SRP_TX_NEXT_SRP_INPUT);
    ASSERT (slot == VNET_SIMULATED_SRP_TX_NEXT_SRP_INPUT);
  }

  return /* no error */ 0;
}

static VLIB_CLI_COMMAND (create_simulated_srp_interface_command) = {
  .path = "srp create-interfaces",
  .short_help = "Create simulated srp interface",
  .function = create_simulated_srp_interfaces,
};
#endif
