/*
 * docsis.h: types/functions for DOCSIS.
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

#ifndef included_docsis_h
#define included_docsis_h

#include <vnet/vnet.h>
#include <vnet/docsis/packet.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/llc/llc.h>
#include <vnet/pg/pg.h>

vnet_hw_interface_class_t docsis_hw_interface_class;

typedef enum {
#define _(n,s) DOCSIS_ERROR_##n,
#include <vnet/docsis/error.def>
#undef _
  DOCSIS_N_ERROR,
} docsis_node_error_t;

/* Access is provided by cable modem (CM);
   Termination is provided by cable modem termination system (CMTS). */
#define foreach_docsis_role			\
  _ (CM)					\
  _ (CMTS)

typedef enum {
#define _(f) DOCSIS_ROLE_##f,
  foreach_docsis_role
#undef _
  DOCSIS_N_ROLE,
} docsis_role_t;

struct docsis_main_t;

typedef docsis_node_error_t (docsis_input_handler_t) (struct docsis_main_t * dm, vlib_buffer_t * b0);

typedef struct {
  docsis_input_handler_t * control[32];
  docsis_input_handler_t * management[64];
} docsis_input_functions_t;

typedef struct docsis_main_t {
  /* Whether we are CM or CMTS. */
  docsis_role_t role;

  /* Input handling functions for each role. */
  docsis_input_functions_t input_functions_for_role[DOCSIS_N_ROLE];

  /* Max supported version for DOCSIS management messages.
     1 => >= DOCSIS 1.0
     2 => >= DOCSIS 1.1
     3 => >= DOCSIS 2.0
     4 => >= DOCSIS 3.0
     etc. */
  u32 max_supported_version;
} docsis_main_t;

docsis_main_t docsis_main;

format_function_t format_docsis_header;
format_function_t format_docsis_header_with_length;

unformat_function_t unformat_docsis_protocol_host_byte_order;
unformat_function_t unformat_docsis_protocol_net_byte_order;

/* Parse docsis header. */
unformat_function_t unformat_docsis_header;
unformat_function_t unformat_pg_docsis_header;

always_inline void
docsis_setup_node (vlib_main_t * vm, u32 node_index)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  pg_node_t * pn = pg_get_node (node_index);

  n->format_buffer = format_docsis_header_with_length;
  n->unformat_buffer = unformat_docsis_header;
  pn->unformat_edit = unformat_pg_docsis_header;
}

#endif /* included_docsis_h */
