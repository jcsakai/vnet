/*
 * vnet.h: general networking definitions
 *
 * Copyright (c) 2011 Eliot Dresselhaus
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

#ifndef included_vnet_vnet_h
#define included_vnet_vnet_h

#include <clib/types.h>

typedef enum {
  VNET_UNICAST,
  VNET_MULTICAST,
  VNET_N_CAST,
} vnet_cast_t;

#include <vnet/buffer.h>
#include <vnet/config.h>
#include <vnet/interface.h>
#include <vnet/rewrite.h>

typedef struct vnet_main_t {
  u32 local_interface_hw_if_index;
  u32 local_interface_sw_if_index;

  vnet_interface_main_t interface_main;

  vlib_main_t * vlib_main;
} vnet_main_t;

vnet_main_t vnet_main;

#include <vnet/interface_funcs.h>

#endif /* included_vnet_vnet_h */