/*
 * mpls.h: types/functions for mpls.
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

#ifndef included_mpls_h
#define included_mpls_h

#include <vnet/vnet.h>
#include <vnet/mpls/packet.h>
#include <vnet/pg/pg.h>
#include <vnet/ip/ip.h>		/* MPLS is friends with IP. */

typedef enum {
#define mpls_error(n,s) MPLS_ERROR_##n,
#include <vnet/mpls/error.def>
#undef mpls_error
  MPLS_N_ERROR,
} mpls_error_t;

typedef struct {
  u32 * adj_index_by_label;
} mpls_fib_t;

typedef struct {
  vlib_main_t * vlib_main;

  mpls_fib_t * fibs;

  u16 * fib_index_by_sw_if_index;

  ip_lookup_main_t lookup_main;
} mpls_main_t;

mpls_main_t mpls_main;

format_function_t format_mpls_header;
format_function_t format_mpls_header_with_length;

/* Parse mpls header. */
unformat_function_t unformat_mpls_header;
unformat_function_t unformat_pg_mpls_header;

always_inline void
mpls_setup_node (vlib_main_t * vm, u32 node_index)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  pg_node_t * pn = pg_get_node (node_index);

  n->format_buffer = format_mpls_header_with_length;
  n->unformat_buffer = unformat_mpls_header;
  pn->unformat_edit = unformat_pg_mpls_header;
}

/* Returns 4/6 for ip4/ip6 or otherwise (for unknown). */
always_inline uword
mpls_header_get_ip_version (mpls_header_t * h)
{
  u8 t = *(u8 *) (h + 1);
  t >>= 4;
  return t;
}

#endif /* included_mpls_h */
