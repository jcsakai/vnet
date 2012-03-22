/*
 * interface.c: VNET interfaces/sub-interfaces
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

#define VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE (1 << 0)
#define VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE (1 << 1)

static clib_error_t *
vnet_hw_interface_set_flags_helper (vnet_main_t * vm, u32 hw_if_index, u32 flags,
				    u32 helper_flags);

static clib_error_t *
vnet_sw_interface_set_flags_helper (vnet_main_t * vm, u32 sw_if_index, u32 flags,
				    u32 helper_flags);

static clib_error_t *
vnet_hw_interface_set_class_helper (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index, u32 redistribute);

typedef struct {
  /* Either sw or hw interface index. */
  u32 sw_hw_if_index;

  /* Flags. */
  u32 flags;
} vnet_sw_hw_interface_state_t;

static void serialize_vec_vnet_sw_hw_interface_state (serialize_main_t * m, va_list * va)
{
    vnet_sw_hw_interface_state_t * s = va_arg (*va, vnet_sw_hw_interface_state_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        serialize_integer (m, s[i].sw_hw_if_index, sizeof (s[i].sw_hw_if_index));
        serialize_integer (m, s[i].flags, sizeof (s[i].flags));
    }
}

static void unserialize_vec_vnet_sw_hw_interface_state (serialize_main_t * m, va_list * va)
{
    vnet_sw_hw_interface_state_t * s = va_arg (*va, vnet_sw_hw_interface_state_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        unserialize_integer (m, &s[i].sw_hw_if_index, sizeof (s[i].sw_hw_if_index));
        unserialize_integer (m, &s[i].flags, sizeof (s[i].flags));
    }
}

static void serialize_vnet_sw_hw_interface_set_flags (serialize_main_t * m, va_list * va)
{
  vnet_sw_hw_interface_state_t * s = va_arg (*va, vnet_sw_hw_interface_state_t *);
  serialize (m, serialize_vec_vnet_sw_hw_interface_state, s, 1);
}

static void unserialize_vnet_sw_interface_set_flags (serialize_main_t * m, va_list * va)
{
  CLIB_UNUSED (mc_main_t * mc) = va_arg (*va, mc_main_t *);
  vnet_sw_hw_interface_state_t s;

  unserialize (m, unserialize_vec_vnet_sw_hw_interface_state, &s, 1);

  vnet_sw_interface_set_flags_helper
    (&vnet_main, s.sw_hw_if_index, s.flags,
     /* helper_flags no redistribution */ 0);
}

static void unserialize_vnet_hw_interface_set_flags (serialize_main_t * m, va_list * va)
{
  CLIB_UNUSED (mc_main_t * mc) = va_arg (*va, mc_main_t *);
  vnet_sw_hw_interface_state_t s;

  unserialize (m, unserialize_vec_vnet_sw_hw_interface_state, &s, 1);

  vnet_hw_interface_set_flags_helper
    (&vnet_main, s.sw_hw_if_index, s.flags,
     /* helper_flags no redistribution */ 0);
}

static MC_SERIALIZE_MSG (vnet_sw_interface_set_flags_msg) = {
  .name = "vnet_sw_interface_set_flags",
  .serialize = serialize_vnet_sw_hw_interface_set_flags,
  .unserialize = unserialize_vnet_sw_interface_set_flags,
};

static MC_SERIALIZE_MSG (vnet_hw_interface_set_flags_msg) = {
  .name = "vnet_hw_interface_set_flags",
  .serialize = serialize_vnet_sw_hw_interface_set_flags,
  .unserialize = unserialize_vnet_hw_interface_set_flags,
};

void serialize_vnet_interface_state (serialize_main_t * m, va_list * va)
{
  vnet_main_t * vm = va_arg (*va, vnet_main_t *);
  vnet_sw_hw_interface_state_t * sts = 0, * st;
  vnet_sw_interface_t * sif;
  vnet_hw_interface_t * hif;
  vnet_interface_main_t * im = &vm->interface_main;

  /* Serialize hardware interface classes since they may have changed.
     Must do this before sending up/down flags. */
  pool_foreach (hif, im->hw_interfaces, ({
    vnet_hw_interface_class_t * hw_class = vnet_get_hw_interface_class (vm, hif->hw_class_index);
    serialize_cstring (m, hw_class->name);
  }));

  /* Send sw/hw interface state when non-zero. */
  pool_foreach (sif, im->sw_interfaces, ({
    if (sif->flags != 0)
      {
	vec_add2 (sts, st, 1);
	st->sw_hw_if_index = sif->sw_if_index;
	st->flags = sif->flags;
      }
  }));

  vec_serialize (m, sts, serialize_vec_vnet_sw_hw_interface_state);

  if (sts)
    _vec_len (sts) = 0;

  pool_foreach (hif, im->hw_interfaces, ({
    if (hif->flags != 0)
      {
	vec_add2 (sts, st, 1);
	st->sw_hw_if_index = hif->hw_if_index;
	st->flags = hif->flags;
      }
  }));

  vec_serialize (m, sts, serialize_vec_vnet_sw_hw_interface_state);

  vec_free (sts);
}

void unserialize_vnet_interface_state (serialize_main_t * m, va_list * va)
{
  vnet_main_t * vm = va_arg (*va, vnet_main_t *);
  vnet_sw_hw_interface_state_t * sts = 0, * st;

  /* First set interface hardware class. */
  {
    vnet_interface_main_t * im = &vm->interface_main;
    vnet_hw_interface_t * hif;
    char * class_name;
    uword * p;
    clib_error_t * error;

    pool_foreach (hif, im->hw_interfaces, ({
      unserialize_cstring (m, &class_name);
      p = hash_get_mem (im->hw_interface_class_by_name, class_name);
      ASSERT (p != 0);
      error = vnet_hw_interface_set_class_helper (vm, hif->hw_if_index, p[0], /* redistribute */ 0);
      if (error)
	clib_error_report (error);
      vec_free (class_name);
    }));
  }

  vec_unserialize (m, &sts, unserialize_vec_vnet_sw_hw_interface_state);
  vec_foreach (st, sts)
    vnet_sw_interface_set_flags_helper (vm, st->sw_hw_if_index, st->flags,
					/* no distribute */ 0);
  vec_free (sts);

  vec_unserialize (m, &sts, unserialize_vec_vnet_sw_hw_interface_state);
  vec_foreach (st, sts)
    vnet_hw_interface_set_flags_helper (vm, st->sw_hw_if_index, st->flags,
					/* no distribute */ 0);
  vec_free (sts);
}

static clib_error_t *
call_elf_section_interface_callbacks (vnet_main_t * vm, u32 if_index, u32 flags, char * name)
{
  clib_elf_section_bounds_t * b, * bounds;
  clib_error_t * error = 0;
  vnet_interface_function_t ** f;

  bounds = clib_elf_get_section_bounds (name);
  vec_foreach (b, bounds)
    {
      for (f = b->lo;
	   f < (vnet_interface_function_t **) b->hi;
	   f = clib_elf_section_data_next (f, 0))
	if ((error = f[0] (vm, if_index, flags)))
	  return error;
    }
  return error;
}

static clib_error_t *
call_hw_interface_add_del_callbacks (vnet_main_t * vm, u32 hw_if_index, u32 is_create)
{
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, hw_if_index);
  vnet_hw_interface_class_t * hw_class = vnet_get_hw_interface_class (vm, hi->hw_class_index);
  vnet_device_class_t * dev_class = vnet_get_device_class (vm, hi->dev_class_index);
  clib_error_t * error = 0;

  if (hw_class->interface_add_del_function
      && (error = hw_class->interface_add_del_function (vm, hw_if_index, is_create)))
    return error;

  if (dev_class->interface_add_del_function
      && (error = dev_class->interface_add_del_function (vm, hw_if_index, is_create)))
    return error;

  error = call_elf_section_interface_callbacks (vm, hw_if_index, is_create,
						"vnet_hw_interface_add_del_functions");

  return error;
}

static clib_error_t *
call_sw_interface_add_del_callbacks (vnet_main_t * vm, u32 sw_if_index, u32 is_create)
{
  return call_elf_section_interface_callbacks (vm, sw_if_index, is_create,
					       "vnet_sw_interface_add_del_functions");
}

#define VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE (1 << 0)
#define VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE (1 << 1)

static clib_error_t *
vnet_hw_interface_set_flags_helper (vnet_main_t * vm, u32 hw_if_index, u32 flags,
				    u32 helper_flags)
{
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, hw_if_index);
  vnet_hw_interface_class_t * hw_class = vnet_get_hw_interface_class (vm, hi->hw_class_index);
  vnet_device_class_t * dev_class = vnet_get_device_class (vm, hi->dev_class_index);
  vlib_main_t * lm = vm->vlib_main;
  u32 mask;
  clib_error_t * error = 0;
  u32 is_create = (helper_flags & VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE) != 0;

  mask = VNET_HW_INTERFACE_FLAG_LINK_UP;
  flags &= mask;

  /* Call hardware interface add/del callbacks. */
  if (is_create)
    call_hw_interface_add_del_callbacks (vm, hw_if_index, is_create);

  /* Already in the desired state? */
  if (! is_create && (hi->flags & mask) == flags)
    goto done;

  /* Some interface classes do not redistribute (e.g. are local). */
  if (! dev_class->redistribute)
    helper_flags &= ~ VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE;

  if (lm->mc_main
      && (helper_flags & VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE))
    {
      vnet_sw_hw_interface_state_t s;
      s.sw_hw_if_index = hw_if_index;
      s.flags = flags;
      mc_serialize (lm->mc_main, &vnet_hw_interface_set_flags_msg, &s);
    }

  /* Do hardware class (e.g. ethernet). */
  if (hw_class->link_up_down_function
      && (error = hw_class->link_up_down_function (vm, hw_if_index, flags)))
    goto done;

  error = call_elf_section_interface_callbacks (vm, hw_if_index, is_create,
						"vnet_hw_interface_link_up_down_functions");
  if (error)
    goto done;

  hi->flags &= ~mask;
  hi->flags |= flags;

 done:
  return error;
}

static clib_error_t *
vnet_sw_interface_set_flags_helper (vnet_main_t * vm, u32 sw_if_index, u32 flags,
				    u32 helper_flags)
{
  vnet_sw_interface_t * si = vnet_get_sw_interface (vm, sw_if_index);
  vlib_main_t * lm = vm->vlib_main;
  u32 mask;
  clib_error_t * error = 0;
  u32 is_create = (helper_flags & VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE) != 0;

  mask = VNET_SW_INTERFACE_FLAG_ADMIN_UP | VNET_SW_INTERFACE_FLAG_PUNT;
  flags &= mask;

  if (is_create)
    {
      error = call_sw_interface_add_del_callbacks (vm, sw_if_index, is_create);
      if (error)
	goto done;
    }
  else
    {
      vnet_sw_interface_t * si_sup = si;

      /* Check that super interface is in correct state. */
      if (si->type == VNET_SW_INTERFACE_TYPE_SUB)
	{
	  si_sup = vnet_get_sw_interface (vm, si->sup_sw_if_index);

	  if (flags != (si_sup->flags & mask))
	    {
	      error = clib_error_return (0, "super-interface %v must be %U",
					 format_vnet_sw_interface_name, vm, si_sup,
					 format_vnet_sw_interface_flags, flags);
	      goto done;
	    }
	}

      /* Already in the desired state? */
      if ((si->flags & mask) == flags)
	goto done;

      /* Sub-interfaces of hardware interfaces that do no redistribute,
	 do not redistribute themselves. */
      if (si_sup->type == VNET_SW_INTERFACE_TYPE_HARDWARE)
	{
	  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, si_sup->hw_if_index);
	  vnet_device_class_t * dev_class = vnet_get_device_class (vm, hi->dev_class_index);
	  if (! dev_class->redistribute)
	    helper_flags &= ~ VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE;
	}

      if (lm->mc_main
	  && (helper_flags & VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE))
	{
	  vnet_sw_hw_interface_state_t s;
	  s.sw_hw_if_index = sw_if_index;
	  s.flags = flags;
	  mc_serialize (lm->mc_main, &vnet_sw_interface_set_flags_msg, &s);
	}

      if (si->type == VNET_SW_INTERFACE_TYPE_HARDWARE)
	{
	  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, si->hw_if_index);
	  vnet_hw_interface_class_t * hw_class = vnet_get_hw_interface_class (vm, hi->hw_class_index);
	  vnet_device_class_t * dev_class = vnet_get_device_class (vm, hi->dev_class_index);

	  if (dev_class->admin_up_down_function
	      && (error = dev_class->admin_up_down_function (vm, si->hw_if_index, flags)))
	    goto done;

	  if (hw_class->admin_up_down_function
	      && (error = hw_class->admin_up_down_function (vm, si->hw_if_index, flags)))
	    goto done;

	  /* Admin down implies link down. */
	  if (! (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP)
	      && (hi->flags & VNET_HW_INTERFACE_FLAG_LINK_UP))
	    vnet_hw_interface_set_flags_helper (vm, si->hw_if_index,
						hi->flags &~ VNET_HW_INTERFACE_FLAG_LINK_UP,
						helper_flags);
	}
    }

  error = call_elf_section_interface_callbacks (vm, sw_if_index, flags,
						"vnet_sw_interface_admin_up_down_functions");
  if (error)
    goto done;

  si->flags &= ~mask;
  si->flags |= flags;

 done:
  return error;
}

clib_error_t *
vnet_hw_interface_set_flags (vnet_main_t * vm, u32 hw_if_index, u32 flags)
{
  return vnet_hw_interface_set_flags_helper
    (vm, hw_if_index, flags,
     VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE);
}

clib_error_t *
vnet_sw_interface_set_flags (vnet_main_t * vm, u32 sw_if_index, u32 flags)
{
  return vnet_sw_interface_set_flags_helper
    (vm, sw_if_index, flags,
     VNET_INTERFACE_SET_FLAGS_HELPER_WANT_REDISTRIBUTE);
}

static u32
vnet_create_sw_interface_no_callbacks (vnet_main_t * vm, vnet_sw_interface_t * template)
{
  vnet_interface_main_t * im = &vm->interface_main;
  vnet_sw_interface_t * sw;
  u32 sw_if_index;

  pool_get (im->sw_interfaces, sw);
  sw_if_index = sw - im->sw_interfaces;

  sw[0] = template[0];

  sw->flags = 0;
  sw->sw_if_index = sw_if_index;
  if (sw->type == VNET_SW_INTERFACE_TYPE_HARDWARE)
    sw->sup_sw_if_index = sw->sw_if_index;

  /* Allocate counters for this interface. */
  {
    u32 i;

    for (i = 0; i < vec_len (im->sw_if_counters); i++)
      {
	vlib_validate_counter (&im->sw_if_counters[i], sw_if_index);
	vlib_zero_simple_counter (&im->sw_if_counters[i], sw_if_index);
      }

    for (i = 0; i < vec_len (im->combined_sw_if_counters); i++)
      {
	vlib_validate_counter (&im->combined_sw_if_counters[i], sw_if_index);
	vlib_zero_combined_counter (&im->combined_sw_if_counters[i], sw_if_index);
      }
  }

  return sw_if_index;
}

u32
vnet_create_sw_interface (vnet_main_t * vm, vnet_sw_interface_t * template)
{
  u32 sw_if_index;

  sw_if_index = vnet_create_sw_interface_no_callbacks (vm, template);
  vnet_sw_interface_set_flags_helper
    (vm, sw_if_index, /* flags */ 0,
     VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE);
  return sw_if_index;
}

void vnet_delete_sw_interface (vnet_main_t * vm, u32 sw_if_index)
{
  vnet_interface_main_t * im = &vm->interface_main;
  vnet_sw_interface_t * sw = pool_elt_at_index (im->sw_interfaces, sw_if_index);

  /* Bring down interface in case it is up. */
  if (sw->flags != 0)
    vnet_sw_interface_set_flags (vm, sw_if_index, /* flags */ 0);

  call_sw_interface_add_del_callbacks (vm, sw_if_index, /* is_create */ 0);

  pool_put (im->sw_interfaces, sw);
}

static void setup_tx_node (vlib_main_t * vm,
			   u32 node_index,
			   vnet_device_class_t * dev_class)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);

  n->function = dev_class->tx_function;
  n->format_trace = dev_class->format_tx_trace;
  vlib_register_errors (vm, node_index, 
                        dev_class->tx_function_n_errors,
                        dev_class->tx_function_error_strings);
}

static void setup_output_node (vlib_main_t * vm,
			       u32 node_index,
			       vnet_hw_interface_class_t * hw_class)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  n->format_buffer = hw_class->format_header;
  n->unformat_buffer = hw_class->unformat_header;
}

/* Register an interface instance. */
u32
vnet_register_interface (vnet_main_t * vm,
			 u32 dev_class_index,
			 u32 dev_instance,
			 u32 hw_class_index,
			 u32 hw_instance)
{
  vnet_interface_main_t * im = &vm->interface_main;
  vnet_hw_interface_t * hw;
  vnet_device_class_t * dev_class = vnet_get_device_class (vm, dev_class_index);
  vnet_hw_interface_class_t * hw_class = vnet_get_hw_interface_class (vm, hw_class_index);
  vlib_main_t * lm = vm->vlib_main;
  u32 hw_index;
  char * tx_node_name, * output_node_name;

  pool_get (im->hw_interfaces, hw);

  hw_index = hw - im->hw_interfaces;
  hw->hw_if_index = hw_index;

  if (dev_class->format_device_name)
    hw->name = format (0, "%U",
		       dev_class->format_device_name, dev_instance);
  else if (hw_class->format_interface_name)
    hw->name = format (0, "%U", hw_class->format_interface_name,
		       dev_instance);
  else
    hw->name = format (0, "%s%x", hw_class->name, dev_instance);

  if (! im->hw_interface_by_name)
    im->hw_interface_by_name = hash_create_vec (/* size */ 0,
						sizeof (hw->name[0]),
						sizeof (uword));

  hash_set_mem (im->hw_interface_by_name, hw->name, hw_index);

  /* Make hardware interface point to software interface. */
  {
    vnet_sw_interface_t sw;

    memset (&sw, 0, sizeof (sw));
    sw.type = VNET_SW_INTERFACE_TYPE_HARDWARE;
    sw.hw_if_index = hw_index;
    hw->sw_if_index = vnet_create_sw_interface_no_callbacks (vm, &sw);
  }

  hw->dev_class_index = dev_class_index;
  hw->dev_instance = dev_instance;
  hw->hw_class_index = hw_class_index;
  hw->hw_instance = hw_instance;

  hw->max_rate_bits_per_sec = 0;
  hw->min_packet_bytes = 0;
  hw->per_packet_overhead_bytes = 0;
  hw->max_l3_packet_bytes[VLIB_RX] = ~0;
  hw->max_l3_packet_bytes[VLIB_TX] = ~0;

  tx_node_name = (char *) format (0, "%v-tx", hw->name);
  output_node_name = (char *) format (0, "%v-output", hw->name);

  /* If we have previously deleted interface nodes, re-use them. */
  if (vec_len (im->deleted_hw_interface_nodes) > 0)
    {
      vnet_hw_interface_nodes_t * hn;
      vnet_interface_output_runtime_t * rt;

      hn = vec_end (im->deleted_hw_interface_nodes) - 1;

      hw->tx_node_index = hn->tx_node_index;
      hw->output_node_index = hn->output_node_index;

      vlib_node_rename (lm, hw->tx_node_index, "%v", tx_node_name);
      vlib_node_rename (lm, hw->output_node_index, "%v", output_node_name);

      rt = vlib_node_get_runtime_data (lm, hw->output_node_index);
      ASSERT (rt->is_deleted == 1);
      rt->is_deleted = 0;

      _vec_len (im->deleted_hw_interface_nodes) -= 1;
    }
  else
    {
      vlib_node_registration_t r;
      vnet_interface_output_runtime_t rt = {
	.hw_if_index = hw_index,
	.sw_if_index = hw->sw_if_index,
	.dev_instance = hw->dev_instance,
	.is_deleted = 0,
      };

      memset (&r, 0, sizeof (r));
      r.type = VLIB_NODE_TYPE_INTERNAL;
      r.runtime_data = &rt;
      r.runtime_data_bytes = sizeof (rt);
      r.scalar_size = 0;
      r.vector_size = sizeof (u32);

      r.flags = VLIB_NODE_FLAG_IS_OUTPUT;
      r.name = tx_node_name;
      r.function = dev_class->tx_function;

      hw->tx_node_index = vlib_register_node (lm, &r);

      vlib_node_add_named_next_with_slot (lm, hw->tx_node_index,
					  "error-drop",
					  VNET_INTERFACE_TX_NEXT_DROP);

      r.flags = 0;
      r.name = output_node_name;
      r.function = vnet_interface_output_node;
      r.format_trace = format_vnet_interface_output_trace;

      {
	static char * e[] = {
	  "interface is down",
	  "interface is deleted",
	};

	r.n_errors = ARRAY_LEN (e);
	r.error_strings = e;
      }

      hw->output_node_index = vlib_register_node (lm, &r);

      vlib_node_add_named_next_with_slot (lm, hw->output_node_index,
					  "error-drop",
					  VNET_INTERFACE_OUTPUT_NEXT_DROP);
      vlib_node_add_next_with_slot (lm, hw->output_node_index,
				    hw->tx_node_index,
				    VNET_INTERFACE_OUTPUT_NEXT_TX);
    }

  setup_output_node (lm, hw->output_node_index, hw_class);
  setup_tx_node (lm, hw->tx_node_index, dev_class);

  /* Call all up/down callbacks with zero flags when interface is created. */
  vnet_sw_interface_set_flags_helper
    (vm, hw->sw_if_index, /* flags */ 0,
     VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE);
  vnet_hw_interface_set_flags_helper
    (vm, hw_index, /* flags */ 0,
     VNET_INTERFACE_SET_FLAGS_HELPER_IS_CREATE);

  return hw_index;
}

void vnet_delete_hw_interface (vnet_main_t * vm, u32 hw_if_index)
{
  vnet_interface_main_t * im = &vm->interface_main;
  vnet_hw_interface_t * hw = vnet_get_hw_interface (vm, hw_if_index);
  vlib_main_t * lm = vm->vlib_main;

  /* If it is up, mark it down. */
  if (hw->flags != 0)
    vnet_hw_interface_set_flags (vm, hw_if_index, /* flags */ 0);

  /* Call delete callbacks. */
  call_hw_interface_add_del_callbacks (vm, hw_if_index, /* is_create */ 0);

  /* Delete software interface corresponding to hardware interface. */
  vnet_delete_sw_interface (vm, hw->sw_if_index);

  /* Delete any sub-interfaces. */
  {
    u32 id, sw_if_index;
    hash_foreach (id, sw_if_index, hw->sub_interface_sw_if_index_by_id, ({
      vnet_delete_sw_interface (vm, sw_if_index);
    }));
  }

  {
    vnet_hw_interface_nodes_t * dn;
    vnet_interface_output_runtime_t * rt = vlib_node_get_runtime_data (lm, hw->output_node_index);

    /* Mark node runtime as deleted so output node (if called) will drop packets. */
    rt->is_deleted = 1;

    vlib_node_rename (lm, hw->output_node_index, "interface-%d-output-deleted", hw_if_index);
    vlib_node_rename (lm, hw->tx_node_index, "interface-%d-tx-deleted", hw_if_index);
    vec_add2 (im->deleted_hw_interface_nodes, dn, 1);
    dn->tx_node_index = hw->tx_node_index;
    dn->output_node_index = hw->output_node_index;
  }

  hash_unset_mem (im->hw_interface_by_name, hw->name);
  vec_free (hw->name);

  pool_put (im->hw_interfaces, hw);
}

static void serialize_vnet_hw_interface_set_class (serialize_main_t * m, va_list * va)
{
  u32 hw_if_index = va_arg (*va, u32);
  char * hw_class_name = va_arg (*va, char *);
  serialize_integer (m, hw_if_index, sizeof (hw_if_index));
  serialize_cstring (m, hw_class_name);
}

static void unserialize_vnet_hw_interface_set_class (serialize_main_t * m, va_list * va)
{
  CLIB_UNUSED (mc_main_t * mc) = va_arg (*va, mc_main_t *);
  vnet_main_t * vm = &vnet_main;
  u32 hw_if_index;
  char * hw_class_name;
  uword * p;
  clib_error_t * error;

  unserialize_integer (m, &hw_if_index, sizeof (hw_if_index));
  unserialize_cstring (m, &hw_class_name);
  p = hash_get (vm->interface_main.hw_interface_class_by_name, hw_class_name);
  ASSERT (p != 0);
  error = vnet_hw_interface_set_class_helper (vm, hw_if_index, p[0], /* redistribute */ 0);
  if (error)
    clib_error_report (error);
}

static MC_SERIALIZE_MSG (vnet_hw_interface_set_class_msg) = {
  .name = "vnet_hw_interface_set_class",
  .serialize = serialize_vnet_hw_interface_set_class,
  .unserialize = unserialize_vnet_hw_interface_set_class,
};

void vnet_hw_interface_init_for_class (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index, u32 hw_instance)
{
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, hw_if_index);
  vnet_hw_interface_class_t * hc = vnet_get_hw_interface_class (vm, hw_class_index);

  hi->hw_class_index = hw_class_index;
  hi->hw_instance = hw_instance;
  setup_output_node (vm->vlib_main, hi->output_node_index, hc);
}

static clib_error_t *
vnet_hw_interface_set_class_helper (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index, u32 redistribute)
{
  vnet_hw_interface_t * hi = vnet_get_hw_interface (vm, hw_if_index);
  vnet_sw_interface_t * si = vnet_get_sw_interface (vm, hi->sw_if_index);
  vnet_hw_interface_class_t * old_class = vnet_get_hw_interface_class (vm, hi->hw_class_index);
  vnet_hw_interface_class_t * new_class = vnet_get_hw_interface_class (vm, hw_class_index);
  vnet_device_class_t * dev_class = vnet_get_device_class (vm, hi->dev_class_index);
  clib_error_t * error = 0;

  /* New class equals old class?  Nothing to do. */
  if (hi->hw_class_index == hw_class_index)
    return 0;

  /* No need (and incorrect since admin up flag may be set) to do error checking when
     receiving unserialize message. */
  if (redistribute)
    {
      if (si->flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP)
	return clib_error_return (0, "%v must be admin down to change class from %s to %s",
				  hi->name, old_class->name, new_class->name);

      /* Make sure interface supports given class. */
      if ((new_class->is_valid_class_for_interface
	   && ! new_class->is_valid_class_for_interface (vm, hw_if_index, hw_class_index))
	  || (dev_class ->is_valid_class_for_interface
	      && ! dev_class->is_valid_class_for_interface (vm, hw_if_index, hw_class_index)))
	return clib_error_return (0, "%v class cannot be changed from %s to %s",
				  hi->name, old_class->name, new_class->name);

      if (vm->vlib_main->mc_main)
	{
	  mc_serialize (vm->vlib_main->mc_main, &vnet_hw_interface_set_class_msg, hw_if_index, new_class->name);
	  return 0;
	}
    }

  if (old_class->hw_class_change)
    old_class->hw_class_change (vm, hw_if_index, old_class->index, new_class->index);

  vnet_hw_interface_init_for_class (vm, hw_if_index, new_class->index, /* instance */ ~0);

  if (new_class->hw_class_change)
    new_class->hw_class_change (vm, hw_if_index, old_class->index, new_class->index);

  if (dev_class->hw_class_change)
    dev_class->hw_class_change (vm, hw_if_index, new_class->index);

  return error;
}

clib_error_t *
vnet_hw_interface_set_class (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index)
{ return vnet_hw_interface_set_class_helper (vm, hw_if_index, hw_class_index, /* redistribute */ 1); }

word
vnet_sw_interface_compare (vnet_main_t * vm,
			   uword sw_if_index0, uword sw_if_index1)
{
  vnet_sw_interface_t * sup0 = vnet_get_sup_sw_interface (vm, sw_if_index0);
  vnet_sw_interface_t * sup1 = vnet_get_sup_sw_interface (vm, sw_if_index1);
  vnet_hw_interface_t * h0 = vnet_get_hw_interface (vm, sup0->hw_if_index);
  vnet_hw_interface_t * h1 = vnet_get_hw_interface (vm, sup1->hw_if_index);

  if (h0 != h1)
    return vec_cmp (h0->name, h1->name);
  return (word) h0->hw_instance - (word) h1->hw_instance;
}

word
vnet_hw_interface_compare (vnet_main_t * vm,
			   uword hw_if_index0, uword hw_if_index1)
{
  vnet_hw_interface_t * h0 = vnet_get_hw_interface (vm, hw_if_index0);
  vnet_hw_interface_t * h1 = vnet_get_hw_interface (vm, hw_if_index1);

  if (h0 != h1)
    return vec_cmp (h0->name, h1->name);
  return (word) h0->hw_instance - (word) h1->hw_instance;
}

clib_error_t *
vnet_interface_init (vlib_main_t * vm)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;

  vec_validate (im->sw_if_counters,
		VNET_N_SIMPLE_INTERFACE_COUNTER - 1);
  im->sw_if_counters[VNET_INTERFACE_COUNTER_DROP].name = "drops";
  im->sw_if_counters[VNET_INTERFACE_COUNTER_PUNT].name = "punts";

  vec_validate (im->combined_sw_if_counters,
		VNET_N_COMBINED_INTERFACE_COUNTER - 1);
  im->combined_sw_if_counters[VNET_INTERFACE_COUNTER_RX].name = "rx";
  im->combined_sw_if_counters[VNET_INTERFACE_COUNTER_TX].name = "tx";

  im->device_class_by_name = hash_create_string (/* size */ 0,
						 sizeof (uword));
  {
    clib_elf_section_bounds_t * b, * bounds;
    vnet_device_class_t * c, * lo, * hi;
    bounds = clib_elf_get_section_bounds ("vnet_device_class");
    vec_foreach (b, bounds)
      {
	lo = b->lo, hi = b->hi;
	for (c = lo; c < hi; c = clib_elf_section_data_next (c, 0))
	  {
	    c->index = vec_len (im->device_classes);
	    hash_set_mem (im->device_class_by_name, c->name, c->index);
	    vec_add1 (im->device_classes, c[0]);
	  }
      }
  }

  im->hw_interface_class_by_name = hash_create_string (/* size */ 0,
						       sizeof (uword));

  {
    clib_elf_section_bounds_t * b, * bounds;
    vnet_hw_interface_class_t * c, * lo, * hi;
    bounds = clib_elf_get_section_bounds ("vnet_hw_interface_class");
    vec_foreach (b, bounds)
      {
	lo = b->lo, hi = b->hi;
	for (c = lo; c < hi; c = clib_elf_section_data_next (c, 0))
	  {
	    c->index = vec_len (im->hw_interface_classes);
	    c->rewrite_fixup_node_index = ~0;
	    hash_set_mem (im->hw_interface_class_by_name, c->name, c->index);
	    vec_add1 (im->hw_interface_classes, c[0]);
	  }
      }
  }

  {
    clib_error_t * error;

    if ((error = vlib_call_init_function (vm, vnet_interface_cli_init)))
      return error;

    return error;
  }
}

VLIB_INIT_FUNCTION (vnet_interface_init);

static clib_error_t *
vnet_interface_main_loop_enter (vlib_main_t * vm)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;
  vnet_hw_interface_class_t * hc;

  vec_foreach (hc, im->hw_interface_classes)
    {
      if (hc->rewrite_fixup_node)
	{
	  vlib_node_t * n = vlib_get_node_by_name (vm, (u8 *) hc->rewrite_fixup_node);
	  if (! n)
	    return clib_error_return_fatal
	      (0, "interface class `%s' rewrite fixup node `%s' not found",
	       hc->name, hc->rewrite_fixup_node);
	  else
	    hc->rewrite_fixup_node_index = n->index;
	}
    }

  return 0;
}

VLIB_MAIN_LOOP_ENTER_FUNCTION (vnet_interface_main_loop_enter);
