/*
 * ip/ip4_cli.c: ip4 commands
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

#include <vnet/ip/ip.h>

int ip4_address_compare (ip4_address_t * a1, ip4_address_t * a2)
{ return clib_net_to_host_u32 (a1->data_u32) - clib_net_to_host_u32 (a2->data_u32); }

static VLIB_CLI_COMMAND (set_interface_ip4_command) = {
  .name = "ip4",
  .short_help = "IP4 commands",
  .parent = &vlib_cli_set_interface_command,
};

void
ip4_set_interface_address (vlib_main_t * vm, u32 sw_if_index,
			   ip4_address_t * new_address, uword new_length)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_adjacency_t * adj;
  ip4_address_t old_address;
  uword old_length, fib_index;
  u32 new_adj_indices[2], old_adj_indicies[2];

  /* Can't assign a /32 to an interface. */
  ASSERT (new_length < 32);

  ASSERT (sw_if_index < vec_len (im->ip4_address_by_sw_if_index));
  ASSERT (sw_if_index < vec_len (im->ip4_address_length_by_sw_if_index));

  old_address = im->ip4_address_by_sw_if_index[sw_if_index];
  old_length = im->ip4_address_length_by_sw_if_index[sw_if_index];

  im->ip4_address_by_sw_if_index[sw_if_index] = new_address[0];
  im->ip4_address_length_by_sw_if_index[sw_if_index] = new_length;

  fib_index = im->fib_index_by_sw_if_index[sw_if_index];

  if (old_address.data_u32 != ~0)
    {
      old_adj_indicies[0]
	= ip4_add_del_route (im, fib_index,
			     IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_FIB_INDEX,
			     old_address.data,
			     old_length,
			     /* adj_index */ ~0);
      old_adj_indicies[1]
	= ip4_add_del_route (im, fib_index,
			     IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_FIB_INDEX,
			     old_address.data,
			     32,
			     /* adj_index */ ~0);

      ip_del_adjacency (lm, old_adj_indicies[0]);
      ip_del_adjacency (lm, old_adj_indicies[1]);

      ip4_delete_matching_routes (im, fib_index, IP4_ROUTE_FLAG_FIB_INDEX,
				  old_address.data,
				  old_length);
    }

  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &new_adj_indices[0]);
  ip_adjacency_set_arp (vm, adj, sw_if_index);

  ip4_add_del_route (im, fib_index,
		     IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_FIB_INDEX,
		     new_address->data,
		     new_length,
		     new_adj_indices[0]);

  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &new_adj_indices[1]);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;

  ip4_add_del_route (im, fib_index,
		     IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_FIB_INDEX,
		     new_address->data,
		     32,
		     new_adj_indices[1]);
}

static clib_error_t *
set_ip4_address (vlib_main_t * vm,
		 unformat_input_t * input,
		 vlib_cli_command_t * cmd)
{
  ip4_address_t a;
  clib_error_t * error = 0;
  u32 sw_if_index, length;

  sw_if_index = ~0;
  if (! unformat_user (input, unformat_vlib_sw_interface, vm, &sw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (! unformat (input, "%U/%d", unformat_ip4_address, &a, &length))
    {
      error = clib_error_return (0, "expected IP4 address A.B.C.D/L `%U'",
				 format_unformat_error, input);
      goto done;
    }

  ip4_set_interface_address (vm, sw_if_index, &a, length);

 done:
  return error;
}

static VLIB_CLI_COMMAND (set_interface_ip4_address_command) = {
  .name = "address",
  .function = set_ip4_address,
  .short_help = "Set IP4 address for interface",
  .parent = &set_interface_ip4_command,
};

/* Dummy init function to get us linked in. */
static clib_error_t * ip4_cli_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (ip4_cli_init);
