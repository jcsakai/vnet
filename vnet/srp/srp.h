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
#include <vnet/ethernet/ethernet.h>
#include <vnet/pg/pg.h>

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

  /* TTL to use for outgoing data packets. */
  u32 default_data_ttl;
} srp_main_t;

srp_main_t srp_main;

u8 * format_srp_header (u8 * s, va_list * args);
u8 * format_srp_header_with_length (u8 * s, va_list * args);

/* Parse srp header. */
uword
unformat_srp_header (unformat_input_t * input, va_list * args);

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
