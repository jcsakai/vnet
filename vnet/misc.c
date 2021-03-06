/*
 * misc.c: vnet misc
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

vnet_main_t vnet_main;

static uword
vnet_local_interface_tx (vlib_main_t * vm,
			 vlib_node_runtime_t * node,
			 vlib_frame_t * f)
{
  ASSERT (0);
  return f->n_vectors;
}

static VNET_DEVICE_CLASS (vnet_local_interface_device_class) = {
  .name = "local",
  .tx_function = vnet_local_interface_tx,
};

static VNET_HW_INTERFACE_CLASS (vnet_local_interface_hw_class) = {
  .name = "local",
};

static clib_error_t *
vnet_main_init (vlib_main_t * vm)
{
  vnet_main_t * vnm = &vnet_main;
  clib_error_t * error;
  u32 hw_if_index;
  vnet_hw_interface_t * hw;

  if ((error = vlib_call_init_function (vm, vnet_interface_init)))
    return error;

  vnm->vlib_main = vm;

  hw_if_index = vnet_register_interface
    (vnm,
     vnet_local_interface_device_class.index, /* instance */ 0,
     vnet_local_interface_hw_class.index, /* instance */ 0);
  hw = vnet_get_hw_interface (vnm, hw_if_index);

  vnm->local_interface_hw_if_index = hw_if_index;
  vnm->local_interface_sw_if_index = hw->sw_if_index;

  if ((error = vlib_call_init_function (vm, vnet_buffer_init)))
    return error;

  return 0;
}

VLIB_INIT_FUNCTION (vnet_main_init);
