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

#include <vnet/devices/ixgbe/ixgbe.h>
#include <vnet/devices/ixgbe/ixgbe_vnet.h>

#include <vnet/pg/pg.h>

ixgbe_main_t ixgbe_main;

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

int ixgbe_ring_init (ixgbe_port_t * port,
                     uword n_descriptors,
                     vlib_rx_or_tx_t rx_or_tx)
{
    vlib_main_t * vm = port->im->vm;
    ixgbe_descriptor_t * d, * ring;
    clib_error_t * error;

    ring = vlib_physmem_alloc_aligned (vm,
				       &error,
				       n_descriptors * sizeof (ring[0]),
				       CLIB_CACHE_LINE_BYTES);
    if (error) {
	/* Allocation error is fatal. */
	clib_error_report (error);
	return 1;
    }

    memset (ring, 0, n_descriptors * sizeof (ring[0]));

    if (rx_or_tx == VLIB_RX) {
	port->buffer_bytes = 2048;
	port->buffer_free_list_index = 
            vlib_buffer_get_or_create_free_list (vm, port->buffer_bytes);

	vec_validate (port->rx_buffers, n_descriptors - 1);
	if (n_descriptors != 
            vlib_buffer_alloc_from_free_list (vm, port->rx_buffers,
                                              n_descriptors,
                                              port->buffer_free_list_index)) {
	    vlib_physmem_free (vm, ring);
            clib_error("buffer alloc failed");
	    return 1;
	}

	for (d = ring; d < ring + n_descriptors; d++) {
	    u32 bi = port->rx_buffers[d - ring];
            d->bufaddr = vlib_get_buffer_data_physical_address (vm, bi);
	}
        port->rx_ring = ring;
    } else {
        port->tx_ring = ring;
        vec_validate(port->tx_buffers, n_descriptors-1);
    }
    return 0;
}

static int ixgbe_poller(ixgbe_port_t *port)
{
    ixgbe_handle_link (port);
    return 0;
}

static uword
ixgbe_process (vlib_main_t * vm, vlib_node_runtime_t * rt, vlib_frame_t * f)
{
    int i;
    ixgbe_main_t *im = &ixgbe_main;
    ixgbe_port_t *port;

    /* Initialize */
    for (i = 0; i < vec_len(im->ports); i++) {
        port = im->ports + i;
        /* setup rings */
        if (ixgbe_ring_init (port, IXGBE_TX_RINGSIZE, VLIB_TX))
            goto broken;
        if (ixgbe_ring_init (port, IXGBE_RX_RINGSIZE, VLIB_RX))
            goto broken;

        /* setup bsd driver */
        if (ixgbe_attach (port))
            goto broken;
        /* init hardware */
        ixgbe_init (&port->adapter);
    }

    /* Turn on rx/tx processing */
    vlib_node_enable_disable (im->vm, ixgbe_input_node.index, 
                              /* enable */ 1);
    while (1) {
        for (i = 0; i < vec_len(im->ports); i++)
            if (ixgbe_poller (im->ports + i))
                goto broken;
        vlib_process_suspend (vm, 200e-3);
    }

broken:
    clib_warning ("stopping ixgbe process...");
    vlib_process_suspend (vm, 1e70);
    return 0; /* not this year... */
}


VLIB_REGISTER_NODE (ixgbe_process_node) = {
    .function = ixgbe_process,
    .type = VLIB_NODE_TYPE_PROCESS,
    .name = "ixgbe-process",
    
    /* Will be enabled if/when hardware is detected. */
    .flags = VLIB_NODE_FLAG_IS_DISABLED,
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
    struct adapter *adapter;

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
    port->im = im;
    port->pci_device_id = device;
    adapter = &port->adapter;
    adapter->port = port;
    adapter->num_queues = 1;
    adapter->if_mtu = ETHERMTU;
    adapter->hw.hw_addr = regbase;
    
    adapter->hw.vendor_id = PCI_VENDOR_ID_INTEL; /* duh */
    adapter->hw.device_id = device;
    /* pci_read_config(dev, PCIR_REVID, 1); */
    adapter->hw.revision_id = 0; 
    /* pci_read_config(dev, PCIR_SUBVEND_0, 2); */
    adapter->hw.subsystem_vendor_id = 0; 
    /* pci_read_config(dev, PCIR_SUBDEV_0, 2); */
    adapter->hw.subsystem_device_id = 0;
    
    /* Found at least one ixgbe port, so enable the nanny process */
    vlib_node_enable_disable (im->vm, ixgbe_process_node.index, 
                              /* enable */ 1);
    return 0;
}

clib_error_t *
ixgbe_vlib_init (vlib_main_t * vm)
{
    static u8 once;

    if (once)
        return 0;

    once = 1;

    ixgbe_main.vm = vm;
    ixgbe_vlib_main = vm;
    /* 
     * Aggravation: back-pointers in ported code means that we have
     * to preallocate the port vector.
     */
    vec_validate(ixgbe_main.ports, 1);
    _vec_len(ixgbe_main.ports) = 0;

    pci_probe_register (PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_IXGBE,
                        128<<10, ixgbe_probe);
    return 0;
}

VLIB_INIT_FUNCTION (ixgbe_vlib_init);

u8 *format_ixgbe_rx_descriptors (u8 * s, va_list * args)
{
    ixgbe_descriptor_t *ring = va_arg (*args, ixgbe_descriptor_t *);
    int n = va_arg (*args, int);
    int i;
    u64 cs;

    for (i = 0; i < n; i++) {
        cs = ring[i].cs;
        s = format (s, "[%d]: physaddr 0x%llx cs 0x%llx", i,
                    ring[i].bufaddr, cs);
        if (cs & IXGBE_B_DONE) {
            s = format (s, " DONE");
        }
        if (cs & IXGBE_B_EOP) {
            s = format (s, " EOP");
        }
        s = format (s, "\n");
    }

    return s;
}

static clib_error_t *
ixgbe_cli_command (vlib_main_t * vm,
                   unformat_input_t * input,
                   vlib_cli_command_t * cmd)
{
    ixgbe_main_t *im = &ixgbe_main;
    ixgbe_port_t *port;
    int i;

    if (unformat (input, "debug")) {
        for (i = 0; i < vec_len(im->ports); i++) {
            port = im->ports + i;
            vlib_cli_output (im->vm, "Port %d debug info:\n", i);
            ixgbe_print_debug_info (im->vm, &port->adapter);
            vlib_cli_output (im->vm, "-----------------\n");
        }
    } else if (unformat(input, "stats")){
        for (i = 0; i < vec_len(im->ports); i++) {
            port = im->ports + i;
            vlib_cli_output (im->vm, "Port %d stats:\n", i);
            ixgbe_print_hw_stats (im->vm, &port->adapter);
            vlib_cli_output (im->vm, "-----------------\n");
        }
    } else if (unformat(input, "rxring")) {
            port = im->ports + i;
            vlib_cli_output (im->vm, "Port %d first 10 rx ring entries:\n", i);
            vlib_cli_output (im->vm, "%U", format_ixgbe_rx_descriptors, 
                             port->rx_ring, 10);
            vlib_cli_output (im->vm, "-----------------\n");
    } else {
        clib_warning ("unknown ixgbe show command");
    }

    return 0;
}

static VLIB_CLI_COMMAND (debug_cli_command) = {
  .name = "ixgbe",
  .short_help = "Intel 10xGE NIC show commands",
  .function = ixgbe_cli_command,
  .parent = &vlib_cli_show_command,
};
