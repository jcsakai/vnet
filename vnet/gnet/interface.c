/*
 * gnet_interface.c: gridnet interfaces
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
#include <vnet/pg/pg.h>
#include <vnet/gnet/gnet.h>

static void gnet_register_interface_helper (gnet_address_t * my_address, u32 * hw_if_indices_by_direction, u32 redistribute);

void serialize_gnet_address (serialize_main_t * m, va_list * va)
{
  gnet_address_t * a = va_arg (*va, gnet_address_t *);
  u8 * p = serialize_get (m, sizeof (a->as_u8));
  int i;
  for (i = 0; i < sizeof (a->as_u8); i++)
    p[i] = a->as_u8[i];
}

void unserialize_gnet_address (serialize_main_t * m, va_list * va)
{
  gnet_address_t * a = va_arg (*va, gnet_address_t *);
  u8 * p = unserialize_get (m, sizeof (a->as_u8));
  int i;
  for (i = 0; i < sizeof (a->as_u8); i++)
    a->as_u8[i] = p[i];
}

void serialize_gnet_main (serialize_main_t * m, va_list * va)
{
  gnet_main_t * gm = &gnet_main;
  gnet_interface_t * gi;
  u32 d;

  serialize_integer (m, pool_elts (gm->interface_pool), sizeof (u32));
  pool_foreach (gi, gm->interface_pool, ({
    serialize (m, serialize_gnet_address, &gi->address);
    for (d = 0; d < ARRAY_LEN (gi->directions); d++)
      serialize_integer (m, gi->directions[d].hw_if_index, sizeof (u32));
  }));
}

void unserialize_gnet_main (serialize_main_t * m, va_list * va)
{
  u32 i, d, n_ifs, hw_if_indices[GNET_N_DIRECTION];
  gnet_address_t my_address;

  unserialize_integer (m, &n_ifs, sizeof (u32));
  for (i = 0; i < n_ifs; i++)
    {
      unserialize (m, unserialize_gnet_address, &my_address);
      for (d = 0; d < GNET_N_DIRECTION; d++)
	unserialize_integer (m, &hw_if_indices[d], sizeof (u32));
      gnet_register_interface_helper (&my_address, hw_if_indices, /* redistribute */ 0);
    }
}

static void serialize_gnet_register_interface_msg (serialize_main_t * m, va_list * va)
{
  u32 * hw_if_indices = va_arg (*va, u32 *);
  u32 d;
  for (d = 0; d < GNET_N_DIRECTION; d++)
    serialize_integer (m, hw_if_indices[d], sizeof (hw_if_indices[d]));
}

static void unserialize_gnet_register_interface_msg (serialize_main_t * m, va_list * va)
{
  CLIB_UNUSED (mc_main_t * mcm) = va_arg (*va, mc_main_t *);
  u32 d, hw_if_indices[GNET_N_DIRECTION];
  gnet_main_t * gm = &gnet_main;
  gnet_address_t my_address;
  uword * p;

  unserialize (m, unserialize_gnet_address, &my_address);
  for (d = 0; d < GNET_N_DIRECTION; d++)
    unserialize_integer (m, &hw_if_indices[d], sizeof (hw_if_indices[d]));

  p = hash_get (gm->gnet_register_interface_waiting_process_pool_index_by_hw_if_index,
		hw_if_indices[0]);
  if (p)
    {
      vlib_one_time_waiting_process_t * wp = pool_elt_at_index (gm->gnet_register_interface_waiting_process_pool, p[0]);
      vlib_signal_one_time_waiting_process (mcm->vlib_main, wp);
      pool_put (gm->gnet_register_interface_waiting_process_pool, wp);
      hash_unset (gm->gnet_register_interface_waiting_process_pool_index_by_hw_if_index,
		  hw_if_indices[0]);
    }
  else
    gnet_register_interface_helper (&my_address, hw_if_indices, /* redistribute */ 0);
}

static MC_SERIALIZE_MSG (gnet_register_interface_msg) = {
  .name = "vnet_gnet_register_interface",
  .serialize = serialize_gnet_register_interface_msg,
  .unserialize = unserialize_gnet_register_interface_msg,
};

static void
gnet_register_interface_helper (gnet_address_t * my_address,
				u32 * hw_if_indices_by_direction,
				u32 redistribute)
{
  vnet_main_t * vnm = &vnet_main;
  gnet_main_t * gm = &gnet_main;
  vlib_main_t * vm = gm->vlib_main;
  gnet_interface_t * gi;
  vnet_hw_interface_t * hws[GNET_N_DIRECTION];
  uword d, * p;

  if (vm->mc_main && redistribute)
    {
      vlib_one_time_waiting_process_t * wp;
      mc_serialize (vm->mc_main, &gnet_register_interface_msg, hw_if_indices_by_direction);
      pool_get (gm->gnet_register_interface_waiting_process_pool, wp);
      hash_set (gm->gnet_register_interface_waiting_process_pool_index_by_hw_if_index,
		hw_if_indices_by_direction[0],
		wp - gm->gnet_register_interface_waiting_process_pool);
      vlib_current_process_wait_for_one_time_event (vm, wp);
    }

  /* Check if interface has already been registered. */
  p = hash_get (gm->interface_index_by_hw_if_index, hw_if_indices_by_direction[0]);
  if (p)
    {
      gi = pool_elt_at_index (gm->interface_pool, p[0]);
    }
  else
    {
      pool_get (gm->interface_pool, gi);
      memset (gi, 0, sizeof (gi[0]));
    }

  gi->address = my_address[0];

  for (d = 0; d < GNET_N_DIRECTION; d++)
    {
      gnet_interface_direction_t * id = gi->directions + d;
      hws[d] = vnet_get_hw_interface (vnm, hw_if_indices_by_direction[d]);
      id->direction = d;
      id->hw_if_index = hw_if_indices_by_direction[d];
      id->sw_if_index = hws[d]->sw_if_index;
      hash_set (gm->interface_index_by_hw_if_index, hw_if_indices_by_direction[d], gi - gm->interface_pool);
    }
}

void gnet_register_interface (gnet_address_t * my_address, u32 * hw_if_indices_by_direction)
{
  gnet_register_interface_helper (my_address, hw_if_indices_by_direction, /* redistribute */ 1);
}

static uword
gnet_is_valid_class_for_interface (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index)
{
  gnet_interface_t * gi = gnet_get_interface_from_vnet_hw_interface (hw_if_index);
  uword d;

  if (! gi)
    return 0;

  /* All directions must be admin down. */
  for (d = 0; d < GNET_N_DIRECTION; d++)
    if (vnet_sw_interface_is_admin_up (vm, gi->directions[d].sw_if_index))
      return 0;

  return 1;
}

static void
gnet_interface_hw_class_change (vnet_main_t * vm, u32 hw_if_index,
			       u32 old_hw_class_index, u32 new_hw_class_index)
{
  gnet_main_t * gm = &gnet_main;
  gnet_interface_t * gi = gnet_get_interface_from_vnet_hw_interface (hw_if_index);
  vnet_hw_interface_t * hi;
  vnet_device_class_t * dc;
  u32 d, to_gnet;

  ASSERT (gi != 0);

  to_gnet = new_hw_class_index == gnet_hw_interface_class.index;

  /* Changing class on either outer or inner rings implies changing the class
     of the other. */
  for (d = 0; d < GNET_N_DIRECTION; d++)
    {
      gnet_interface_direction_t * id = &gi->directions[d];

      hi = vnet_get_hw_interface (vm, id->hw_if_index);
      dc = vnet_get_device_class (vm, hi->dev_class_index);

      /* hw_if_index itself will be handled by caller. */
      if (id->hw_if_index != hw_if_index)
	{
	  vnet_hw_interface_init_for_class (vm, id->hw_if_index,
					    new_hw_class_index,
					    to_gnet ? gi - gm->interface_pool : ~0);

	  if (dc->hw_class_change)
	    dc->hw_class_change (vm, id->hw_if_index, new_hw_class_index);
	}
      else
	hi->hw_instance = to_gnet ? gi - gm->interface_pool : ~0;
    }
}

VNET_HW_INTERFACE_CLASS (gnet_hw_interface_class) = {
  .name = "Gridnet",
  .format_address = format_gnet_address,
  .format_header = format_gnet_header_with_length,
  .format_device = format_gnet_device,
  .unformat_hw_address = unformat_gnet_address,
  .unformat_header = unformat_gnet_header,
  .is_valid_class_for_interface = gnet_is_valid_class_for_interface,
  .hw_class_change = gnet_interface_hw_class_change,
};

#if DEBUG > 0

#define VNET_SIMULATED_GNET_TX_NEXT_GNET_INPUT VNET_INTERFACE_TX_N_NEXT

/* Echo packets back to gnet input. */
static uword
simulated_gnet_interface_tx (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  u32 n_left_from, n_left_to_next, n_copy, * from, * to_next;
  u32 next_index = VNET_SIMULATED_GNET_TX_NEXT_GNET_INPUT;
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
	  /* TX interface will be fake eth; copy to RX for benefit of gnet-input. */
	  b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX];
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_left_from;
}

static u8 * format_simulated_gnet_name (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  return format (s, "fake-gnet%d", dev_instance);
}

static VNET_DEVICE_CLASS (gnet_simulated_device_class) = {
  .name = "Simulated gnet",
  .format_device_name = format_simulated_gnet_name,
  .tx_function = simulated_gnet_interface_tx,
};

static clib_error_t *
create_simulated_gnet_interfaces (vlib_main_t * vm,
				 unformat_input_t * input,
				 vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  u8 address[6];
  u32 hw_if_index;
  vnet_hw_interface_t * hi;
  static u32 instance;

  if (! unformat_user (input, unformat_ethernet_address, &address))
    {
      memset (address, 0, sizeof (address));
      address[0] = 0xde;
      address[1] = 0xad;
      address[5] = instance;
    }

  hw_if_index = vnet_register_interface (vnm,
					 gnet_simulated_device_class.index,
					 instance++,
					 gnet_hw_interface_class.index, 0);

  hi = vnet_get_hw_interface (vnm, hw_if_index);

  gnet_setup_node (vm, hi->output_node_index);

  hi->min_packet_bytes = 40 + 16;

  /* Standard default ethernet MTU. */
  hi->max_l3_packet_bytes[VLIB_RX] = hi->max_l3_packet_bytes[VLIB_TX] = 1500;

  vec_free (hi->hw_address);
  vec_add (hi->hw_address, address, sizeof (address));

  {
    uword slot;

    slot = vlib_node_add_named_next_with_slot
      (vm, hi->tx_node_index,
       "gnet-input",
       VNET_SIMULATED_GNET_TX_NEXT_GNET_INPUT);
    ASSERT (slot == VNET_SIMULATED_GNET_TX_NEXT_GNET_INPUT);
  }

  return /* no error */ 0;
}

static VLIB_CLI_COMMAND (create_simulated_gnet_interface_command) = {
  .path = "gnet create-interfaces",
  .short_help = "Create simulated gnet interface",
  .function = create_simulated_gnet_interfaces,
};
#endif
