/* 
 *------------------------------------------------------------------
 * ixgbe_vnet.h - vlib / vnet specifics
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

#ifndef __included_ixgbe_vnet_h__
#define __included_ixgbe_vnet_h__

#include <vnet/devices/ixgbe/ixgbe.h>

/* Legacy RX descriptor, TX descriptor */
typedef struct ixgbe_descriptor_ {
    u64 bufaddr;
    u64 cs;
} ixgbe_descriptor_t;

#define IXGBE_TX_RINGSIZE           768
#define IXGBE_RX_RINGSIZE	    512

/* Legacy RX, TX descriptor */
#define IXGBE_B_DONE           ((u64) 1 << 32)
#define IXGBE_B_EOP            ((u64) 1 << 33)
#define IXGBE_RMD_ERR_SUMMARY  0x0000F700

#define IXGBE_TMD_EOP          0x01000000      /* end of packet */
#define IXGBE_TMD_IFCS         0x02000000      /* insert FCS */
#define IXGBE_TMD_IC           0x04000000      /* insert TCP checksum */
#define IXGBE_TMD_RS           0x08000000      /* report status */
#define IXGBE_TMD_RSV1         0x10000000      /* reserved */
#define IXGBE_TMD_DEXT         0x20000000      /* extended descriptor */
#define IXGBE_TMD_VLE          0x40000000      /* VLAN enable */
#define IXGBE_TMD_RSV2         0x80000000      /* reserved */

struct ixgbe_main;

typedef struct ixgbe_port {
    volatile u8 *regs;
    u32 hw_if_index;
    u32 sw_if_index;
    struct adapter adapter;     /* bsd driver adapter structure */
    struct ixgbe_main *im;
    u16 pci_device_id;
    u32 buffer_bytes;
    u32 buffer_free_list_index;
    ixgbe_descriptor_t *rx_ring;
    u32 *rx_buffers;
    ixgbe_descriptor_t *tx_ring;
    u32 *tx_buffers;
} ixgbe_port_t;

typedef struct ixgbe_main {
    vlib_main_t *vm;
    ixgbe_port_t *ports;
} ixgbe_main_t;

ixgbe_main_t ixgbe_main;

vlib_main_t *ixgbe_vlib_main;

/*
 * Prototypes from the vlib driver
 */
clib_error_t *ixgbe_vlib_init (vlib_main_t *vm);

/* 
 * Prototypes from the bsd driver
 */
int ixgbe_attach(ixgbe_port_t *ixp);
void ixgbe_init(void *arg);
void ixgbe_handle_link (ixgbe_port_t *ixp);
void ixgbe_print_hw_stats(struct adapter * adapter);
void ixgbe_print_debug_info(vlib_main_t *vm, struct adapter *adapter);

#endif /* __included_ixgbe_vnet_h__ */
