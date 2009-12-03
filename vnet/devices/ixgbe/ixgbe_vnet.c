/* 
 *------------------------------------------------------------------
 * ixgbe_vnet.c - vector tx / rx / init
 *
 * Copyright (c) 2009 Cisco Systems, Inc. 
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
 *------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/pci.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <clib/types.h>
#include <clib/hash.h>
#include <clib/format.h>
#include <clib/error.h>
#include <clib/unix.h>
#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

#include <vnet/unix/pci_probe.h>
#include <vnet/devices/ixgbe/ixgbe_vnet.h>

#include <vnet/pg/pg.h>

static uword
ixgbe_tx (vlib_main_t * vm,
          vlib_node_runtime_t * node,
          vlib_frame_t * frame)
{
  uword n_buffers = frame->n_vectors;

  return n_buffers;
}

typedef enum {
    IXGBE_TX_ERROR_OVERRUN,
} ixgbe_tx_error_t;

static char * ixgbe_tx_output_error_strings[] = {
    [IXGBE_TX_ERROR_OVERRUN] = "TX ring overrun buffer drops",
};

static vlib_device_class_t ixgbe_device_class = {
  .name = "ixgbe",
  .tx_function = ixgbe_tx,
  .tx_function_error_strings = ixgbe_tx_output_error_strings,
  .tx_function_n_errors = ARRAY_LEN (ixgbe_tx_output_error_strings),
};

static vlib_hw_interface_class_t ixgbe_hw_interface_class = {
  .name = "ixgbe",
};

static uword
ixgbe_input (vlib_main_t * vm,
	     vlib_node_runtime_t * rt,
	     vlib_frame_t * f)
{
    u32 n_packets = 0;

    return n_packets;
}

#define foreach_ixgbe_input_error \
_(IXGBE_RX_GENERIC_ERROR, "generic ixgbe input error")

static char * ixgbe_input_error_strings[] = {
#define _(n,s) s,
    foreach_ixgbe_input_error
#undef _
};

VLIB_REGISTER_NODE (ixgbe_input_node) = {
    .function = ixgbe_input,
    .type = VLIB_NODE_TYPE_INPUT,
    .name = "ixgbe-input",

    /* Will be enabled if/when hardware is detected. */
    .flags = VLIB_NODE_FLAG_IS_DISABLED,

    .n_errors = ARRAY_LEN(ixgbe_input_error_strings),
    .error_strings = ixgbe_input_error_strings,

    .n_next_nodes = 1,
    .next_nodes = {
	"error-drop",
    },
};


#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_IXGBE 0x10c6 /* $$$ register all of 'em*/

static clib_error_t *
ixgbe_probe (u16 device, int fd, u8 *regbase, 
             u64 * resources, u16 bus, u16 devfn, u32 irq)
{
    ixgbe_main_t *im = &ixgbe_main;
    ixgbe_port_t *port;
    vlib_hw_interface_t *hw_if;

    clib_warning (
        "device 0x%x regbase 0x%llx, bus 0x%2x, devfn 0x%2x irq 0x%x\n",
        device, regbase, bus, devfn, irq);

    /* $$$ only the two-port, sr optics for now. That's what we have */
    if (device != PCI_DEVICE_ID_IXGBE) {
        clib_warning ("unknown ixgbe flavor, outta here...");
        return 0;
    }

    vec_add2(im->ports, port, 1);

    port->hw_if_index 
        = vlib_register_interface (im->vm, &ixgbe_device_class, 
                                   port - im->ports,
                                   &ixgbe_hw_interface_class, 
                                   port - im->ports);
    hw_if = vlib_get_hw_interface (im->vm, port->hw_if_index);
    port->sw_if_index = hw_if->sw_if_index;
    port->regs = regbase;

#if 0   /* fuckme code, shows we really have hold of the dev */
    {
#define IXGBE_STATUS(regs) ((regs) + 0x8)
#define IXGBE_READ(r) (*((u32 *)(r)))

        u32 status;
        volatile u8 *regs;

        regs = regbase;
        /* 
         * expect STATUS 0x00080004 from one port,
         *        STATUS 0x00080000 from the other port 
         */
        status = IXGBE_READ(IXGBE_STATUS(regs));
        clib_warning("STATUS 0x%08x\n", status);
    }
#endif

    return 0;
}

clib_error_t *
ixgbe_init (vlib_main_t * vm)
{
    static u8 once;

    if (once)
        return 0;

    once = 1;

    ixgbe_main.vm = vm;

    pci_probe_register (PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_IXGBE,
                        128<<10, ixgbe_probe);
    return 0;
}
VLIB_INIT_FUNCTION (ixgbe_init);
