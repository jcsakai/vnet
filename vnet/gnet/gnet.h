/*
 * gnet.h: types/functions for Gridnet.
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

#ifndef included_gnet_h
#define included_gnet_h

#include <vnet/vnet.h>
#include <vnet/gnet/packet.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/pg/pg.h>

extern vnet_hw_interface_class_t gnet_hw_interface_class;

/* Each node has 4 neighbors.  n/s is +/- x[13]; e/w is +/- x[02]. */
#define foreach_gnet_direction _ (n) _ (e) _ (s) _ (w) 

typedef enum {
#define _(f) GNET_DIRECTION_##f,
  foreach_gnet_direction
#undef _
  GNET_N_DIRECTION,
} gnet_direction_t;

/* Node in x0/x1 plane.  Interconnect between x0/x1 and x2/x3 planes.
   Gateway to/from external network. */
#define foreach_gnet_interface_role		\
  _ (node)					\
  _ (interconnect)				\
  _ (gateway)

typedef enum {
#define _(f) GNET_INTERFACE_ROLE_##f,
  foreach_gnet_interface_role
#undef _
  GNET_N_INTERFACE_ROLE,
} gnet_interface_role_t;

typedef struct {
  u8 link_is_up;

  gnet_direction_t direction : 8;

  u16 pad;

  /* Next node index for gnet-input to output to this interface direction. */
  u32 gnet_input_next_index;

  /* Hardware interface for this ring/side. */
  u32 hw_if_index;

  /* Software interface corresponding to hardware interface. */
  u32 sw_if_index;
} gnet_interface_direction_t;

struct gnet_interface_t;

typedef struct gnet_interface_t {
  /* Address for this interface. */
  gnet_address_t address;

  /* Address coordinates 0 thru 4 for quick access when forwarding packets. */
  i8 address_01_as_i8[2];

  /* 12 bit coordinates 2 & 3 concatenated. */
  u16 address_23;

  /* Distance vector (x0,x1) from our address to one of up to 64 routers
     which connect to x2 +- 1 and x3 +- 1 planes. */
  u8 router_x0[64], router_x1[64];

  u32 next_index_by_dst_x0[GNET_ADDRESS_N_PER_DIMENSION];
  u32 next_index_by_dst_x1[GNET_ADDRESS_N_PER_DIMENSION];

  gnet_interface_role_t role;

  gnet_interface_direction_t directions[GNET_N_DIRECTION];

  /* For interconnect (router) role. */
  gnet_interface_direction_t directions_23[GNET_N_DIRECTION];
} gnet_interface_t;

typedef struct {
  vlib_main_t * vlib_main;

  /* Pool of GNET interfaces. */
  gnet_interface_t * interface_pool;

  uword * interface_index_by_hw_if_index;

  vlib_one_time_waiting_process_t * gnet_register_interface_waiting_process_pool;

  uword * gnet_register_interface_waiting_process_pool_index_by_hw_if_index;

  /* Grid is 4D torus with size n[0] x n[1] x n[2] x n[3]. */
  u8 grid_size[4];

  /* n0*n1, n1*n2*n3 */
  u16 grid_size_01;
  u32 grid_size_012;

  /* Table of all possible addresses by index. */
  gnet_address_t * address_by_index;
} gnet_main_t;

always_inline u32
gnet_address_to_index (gnet_main_t * m, gnet_address_t * a)
{
  u32 x = (a->as_u8[0] << 16) | (a->as_u8[1] << 8) | a->as_u8[2];
  u32 x0 = (x >> (0*6)) & 0x3f;
  u32 x1 = (x >> (1*6)) & 0x3f;
  u32 x2 = (x >> (2*6)) & 0x3f;
  u32 x3 = (x >> (3*6)) & 0x3f;
  return x0 + x1*m->grid_size[0] + x2*m->grid_size_01 + x3*m->grid_size_012;
}

/* Yes this is slow but we tabulate the result. */
always_inline void
gnet_index_to_address (gnet_main_t * m, gnet_address_t * a, u32 index)
{
  u32 y = index;
  u32 x = 0;

  x |= (y % m->grid_size[0]) << (6*0); y /= m->grid_size[0];
  x |= (y % m->grid_size[1]) << (6*1); y /= m->grid_size[1];
  x |= (y % m->grid_size[2]) << (6*2); y /= m->grid_size[2];

  ASSERT (y < m->grid_size[3]);
  x |= y << (6*3);

  a->as_u8[0] = x >> (8*2);
  a->as_u8[1] = x >> (8*1);
  a->as_u8[2] = x >> (8*0);
}

always_inline void
gnet_unpack_address (gnet_address_t * a, u8 * u)
{
  u[0] = gnet_address_get (a, 0);
  u[1] = gnet_address_get (a, 1);
  u[2] = gnet_address_get (a, 2);
  u[3] = gnet_address_get (a, 3);
}

always_inline void
gnet_pack_address (gnet_address_t * a, u8 * u)
{
  gnet_address_set (a, u[0], u[1], u[2], u[3]);
}

/* Registers 4 hardware interfaces as being GNET capable. */
void gnet_register_interface (gnet_address_t * if_address, u32 * hw_if_indices);

gnet_main_t gnet_main;

always_inline gnet_interface_t *
gnet_get_interface_from_vnet_hw_interface (u32 hw_if_index)
{
  gnet_main_t * gm = &gnet_main;
  uword * p = hash_get (gm->interface_index_by_hw_if_index, hw_if_index);
  return p ? pool_elt_at_index (gm->interface_pool, p[0]) : 0;
}

format_function_t format_gnet_address;
format_function_t format_gnet_header;
format_function_t format_gnet_header_with_length;
format_function_t format_gnet_device;

/* Parse gnet header. */
unformat_function_t unformat_gnet_address;
unformat_function_t unformat_gnet_header;
unformat_function_t unformat_pg_gnet_header;

always_inline void
gnet_setup_node (vlib_main_t * vm, u32 node_index)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  pg_node_t * pn = pg_get_node (node_index);
  n->format_buffer = format_gnet_header_with_length;
  n->unformat_buffer = unformat_gnet_header;
  pn->unformat_edit = unformat_pg_gnet_header;
}

#define foreach_gnet_error					\
  _ (NONE, "no error")						\
  _ (INVALID_ADDRESS, "address out of range")			\
  _ (CONTROL_PACKETS_PROCESSED, "control packets processed")	\
  _ (UNKNOWN_CONTROL, "unknown control packet")

typedef enum {
#define _(n,s) GNET_ERROR_##n,
  foreach_gnet_error
#undef _
  GNET_N_ERROR,
} gnet_error_t;

serialize_function_t serialize_gnet_main, unserialize_gnet_main;

#endif /* included_gnet_h */
