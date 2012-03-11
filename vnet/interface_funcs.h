/*
 * interface_funcs.h: VNET interfaces/sub-interfaces exported functions
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

#ifndef included_vnet_interface_funcs_h
#define included_vnet_interface_funcs_h

always_inline vnet_hw_interface_t *
vnet_get_hw_interface (vnet_main_t * vm, u32 hw_if_index)
{ return pool_elt_at_index (vm->interface_main.hw_interfaces, hw_if_index); }

always_inline vnet_sw_interface_t *
vnet_get_sw_interface (vnet_main_t * vm, u32 sw_if_index)
{ return pool_elt_at_index (vm->interface_main.sw_interfaces, sw_if_index); }

always_inline vnet_sw_interface_t *
vnet_get_hw_sw_interface (vnet_main_t * vm, u32 hw_if_index)
{
  vnet_hw_interface_t * hw = vnet_get_hw_interface (vm, hw_if_index);
  vnet_sw_interface_t * sw = vnet_get_sw_interface (vm, hw->sw_if_index);
  ASSERT (sw->type == VNET_SW_INTERFACE_TYPE_HARDWARE);
  return sw;
}

always_inline vnet_sw_interface_t *
vnet_get_sup_sw_interface (vnet_main_t * vm, u32 sw_if_index)
{
  vnet_sw_interface_t * sw = vnet_get_sw_interface (vm, sw_if_index);
  if (sw->type == VNET_SW_INTERFACE_TYPE_SUB)
    sw = vnet_get_sw_interface (vm, sw->sup_sw_if_index);
  return sw;
}

always_inline vnet_hw_interface_t *
vnet_get_sup_hw_interface (vnet_main_t * vm, u32 sw_if_index)
{
  vnet_sw_interface_t * sw = vnet_get_sup_sw_interface (vm, sw_if_index);
  ASSERT (sw->type == VNET_SW_INTERFACE_TYPE_HARDWARE);
  return vnet_get_hw_interface (vm, sw->hw_if_index);
}

always_inline vnet_hw_interface_class_t *
vnet_get_hw_interface_class (vnet_main_t * vm, u32 hw_class_index)
{ return vec_elt_at_index (vm->interface_main.hw_interface_classes, hw_class_index); }

always_inline vnet_device_class_t *
vnet_get_device_class (vnet_main_t * vm, u32 dev_class_index)
{ return vec_elt_at_index (vm->interface_main.device_classes, dev_class_index); }

/* Register a hardware interface instance. */
u32 vnet_register_interface (vnet_main_t * vm,
			     u32 dev_class_index,
			     u32 dev_instance,
			     u32 hw_class_index,
			     u32 hw_instance);

/* Creates a software interface given template. */
u32 vnet_create_sw_interface (vnet_main_t * vm, vnet_sw_interface_t * template);

void vnet_delete_hw_interface (vnet_main_t * vm, u32 hw_if_index);
void vnet_delete_sw_interface (vnet_main_t * vm, u32 sw_if_index);

always_inline uword
vnet_sw_interface_get_flags (vnet_main_t * vm, u32 sw_if_index)
{
  vnet_sw_interface_t * sw = vnet_get_sw_interface (vm, sw_if_index);
  return sw->flags;
}

always_inline uword
vnet_sw_interface_is_admin_up (vnet_main_t * vm, u32 sw_if_index)
{ return (vnet_sw_interface_get_flags (vm, sw_if_index) & VNET_SW_INTERFACE_FLAG_ADMIN_UP) != 0; }

always_inline uword
vnet_hw_interface_get_flags (vnet_main_t * vm, u32 hw_if_index)
{
  vnet_hw_interface_t * hw = vnet_get_hw_interface (vm, hw_if_index);
  return hw->flags;
}

always_inline uword
vnet_hw_interface_is_link_up (vnet_main_t * vm, u32 hw_if_index)
{ return (vnet_hw_interface_get_flags (vm, hw_if_index) & VNET_HW_INTERFACE_FLAG_LINK_UP) != 0; }

always_inline vlib_frame_t *
vnet_get_frame_to_sw_interface (vnet_main_t * vm, u32 sw_if_index)
{
  vnet_hw_interface_t * hw = vnet_get_sup_hw_interface (vm, sw_if_index);
  return vlib_get_frame_to_node (vm->vlib_main, hw->output_node_index);
}

always_inline void
vnet_put_frame_to_sw_interface (vnet_main_t * vm, u32 sw_if_index, vlib_frame_t * f)
{
  vnet_hw_interface_t * hw = vnet_get_sup_hw_interface (vm, sw_if_index);
  return vlib_put_frame_to_node (vm->vlib_main, hw->output_node_index, f);
}

/* Change interface flags (e.g. up, down, enable, disable). */
clib_error_t *
vnet_hw_interface_set_flags (vnet_main_t * vm, u32 hw_if_index, u32 flags);

/* Change interface flags (e.g. up, down, enable, disable). */
clib_error_t *
vnet_sw_interface_set_flags (vnet_main_t * vm, u32 sw_if_index, u32 flags);

/* Change interface class. */
clib_error_t *
vnet_hw_interface_set_class (vnet_main_t * vm, u32 hw_if_index, u32 new_hw_class_index);

void vnet_hw_interface_init_for_class (vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index, u32 hw_instance);

/* Formats sw/hw interface. */
format_function_t format_vnet_hw_interface;
format_function_t format_vnet_sw_interface;
format_function_t format_vnet_sw_interface_name;
format_function_t format_vnet_sw_if_index_name;
format_function_t format_vnet_sw_interface_flags;

/* Parses sw/hw interface name -> index. */
unformat_function_t unformat_vnet_sw_interface;
unformat_function_t unformat_vnet_hw_interface;

/* Parses interface flags (up, down, enable, disable, etc.) */
unformat_function_t unformat_vnet_hw_interface_flags;
unformat_function_t unformat_vnet_sw_interface_flags;

/* Node runtime for interface output function. */
typedef struct {
  u32 hw_if_index;
  u32 sw_if_index;
  u32 dev_instance;
  u32 is_deleted;
} vnet_interface_output_runtime_t;

/* Interface output function. */
uword
vnet_interface_output_node (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    vlib_frame_t * frame);

word vnet_sw_interface_compare (vnet_main_t * vm, uword sw_if_index0, uword sw_if_index1);
word vnet_hw_interface_compare (vnet_main_t * vm, uword hw_if_index0, uword hw_if_index1);

typedef enum {
  VNET_INTERFACE_OUTPUT_NEXT_DROP,
  VNET_INTERFACE_OUTPUT_NEXT_TX,
} vnet_interface_output_next_t;

typedef enum {
  VNET_INTERFACE_TX_NEXT_DROP,
  VNET_INTERFACE_TX_N_NEXT,
} vnet_interface_tx_next_t;

typedef enum {
  VNET_INTERFACE_OUTPUT_ERROR_INTERFACE_DOWN,
  VNET_INTERFACE_OUTPUT_ERROR_INTERFACE_DELETED,
} vnet_interface_output_error_t;

/* Format for interface output traces. */
u8 * format_vnet_interface_output_trace (u8 * s, va_list * va);

serialize_function_t serialize_vnet_interface_state, unserialize_vnet_interface_state;

#endif /* included_vnet_interface_funcs_h */
