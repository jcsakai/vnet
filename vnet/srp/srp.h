/*
 * srp.h: types/functions for srp.
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

#ifndef included_srp_h
#define included_srp_h

#include <vnet/srp/packet.h>
#include <vnet/pg/pg.h>

/* SRP sends packets down 2 counter rotating rings outer and inner.
   A side is rx outer plus tx inner.
   B side is rx inner plus tx outer. */
typedef enum {
  SRP_SIDE_A,
  SRP_SIDE_B,
  SRP_N_SIDE,
} srp_side_t;

/* SRP interface instance. */
typedef struct {
  /* MAC address for this interface. */
  u8 address[6];

  /* Side of this interface. */
  srp_side_t side;

  /* Index of ring mate (other side). */
  u32 mate_hw_if_index;
} srp_interface_t;

extern vlib_hw_interface_class_t srp_hw_interface_class;

#define foreach_srp_error			\
  _ (NONE, "no error")				\
  _ (UNKNOWN_MODE, "unknown mode in SRP header")

typedef enum {
#define _(n,s) SRP_ERROR_##n,
  foreach_srp_error
#undef _
  SRP_N_ERROR,
} srp_error_t;

typedef struct {
  vlib_main_t * vlib_main;

  /* Pool of srp interface instances. */
  srp_interface_t * interfaces;

  /* TTL to use for outgoing data packets. */
  u32 default_data_ttl;
} srp_main_t;

srp_main_t srp_main;

always_inline uword
is_srp_interface (u32 hw_if_index)
{
  srp_main_t * em = &srp_main;
  vlib_hw_interface_t * i = vlib_get_hw_interface (em->vlib_main, hw_if_index);
  vlib_hw_interface_class_t * c = vlib_get_hw_interface_class (em->vlib_main, i->hw_class_index);
  return ! strcmp (c->name, srp_hw_interface_class.name);
}

always_inline srp_interface_t *
srp_get_interface (srp_main_t * em, u32 hw_if_index)
{
  vlib_hw_interface_t * i = vlib_get_hw_interface (em->vlib_main, hw_if_index);
  return (is_srp_interface (hw_if_index)
	  ? pool_elt_at_index (em->interfaces, i->hw_instance)
	  : 0);
}

clib_error_t *
srp_register_interface (vlib_main_t * vm,
			u32 dev_class_index,
			u32 dev_instance,
			u8 * address,
			u32 * hw_if_index_return);

void srp_delete_interface (vlib_main_t * vm, u32 hw_if_index);

int srp_interface_get_address (vlib_main_t * vm, u32 hw_if_index, u8 * address);

u8 * format_srp_header (u8 * s, va_list * args);
u8 * format_srp_header_with_length (u8 * s, va_list * args);

/* Parse srp header. */
uword
unformat_srp_header (unformat_input_t * input, va_list * args);

/* Parse srp interface name; return hw_if_index. */
uword unformat_srp_interface (unformat_input_t * input, va_list * args);

uword unformat_pg_srp_header (unformat_input_t * input, va_list * args);

always_inline void
srp_setup_node (vlib_main_t * vm, u32 node_index)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  pg_node_t * pn = pg_get_node (node_index);

  n->format_buffer = format_srp_header_with_length;
  n->unformat_buffer = unformat_srp_header;
  pn->unformat_edit = unformat_pg_srp_header;
}

#endif /* included_srp_h */
