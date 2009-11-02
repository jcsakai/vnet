/*
 * ip/ip4.h: ip4 main include file
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

#ifndef included_ip_ip4_h
#define included_ip_ip4_h

typedef struct {
  /* Hash table for each prefix length mapping. */
  uword * adj_index_by_dst_address[33];

  u32 masks[33];

  u32 table_id;
} ip4_really_slow_fib_t;

typedef struct {
  ip_lookup_main_t lookup_main;

  /* FIXME stupid fib. */
  ip4_really_slow_fib_t * fibs;

  /* Table id for default FIB (equal to zero). */
  u32 default_fib_table_id;

  uword * fib_index_by_table_id;
} ip4_main_t;

/* Global ip4 main structure. */
extern ip4_main_t ip4_main;

/* Global ip4 input node.  Errors get attached to ip4 input node. */
extern vlib_node_registration_t ip4_input_node;
extern vlib_node_registration_t ip4_rewrite_node;

/* Add a route to the FIB. */
void
ip4_route_add_del (ip4_main_t * im,
		   u32 table_id,
		   u8 * address,
		   u32 address_length,
		   u32 adj_index,
		   u32 is_del);

#endif /* included_ip_ip4_h */
