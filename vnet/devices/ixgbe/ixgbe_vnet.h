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

typedef struct {
    volatile u8 *regs;
    u32 hw_if_index;
    u32 sw_if_index;
} ixgbe_port_t;

typedef struct {
    vlib_main_t *vm;
    ixgbe_port_t *ports;
} ixgbe_main_t;

ixgbe_main_t ixgbe_main;

#endif /* __included_ixgbe_vnet_h__ */
