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

int ip6_address_compare (ip6_address_t * a1, ip6_address_t * a2)
{
  int i;
  for (i = 0; i < ARRAY_LEN (a1->as_u16); i++)
    {
      int cmp = clib_net_to_host_u16 (a1->as_u16[i]) - clib_net_to_host_u16 (a2->as_u16[i]);
      if (cmp != 0)
	return cmp;
    }
  return 0;
}

static VLIB_CLI_COMMAND (set_interface_ip_command) = {
  .path = "set interface ip",
  .short_help = "IP4/IP6 commands",
};

static clib_error_t *
add_del_ip_address (vlib_main_t * vm,
		    unformat_input_t * input,
		    vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  ip4_address_t a4;
  ip6_address_t a6;
  clib_error_t * error = 0;
  u32 sw_if_index, length, is_del;

  sw_if_index = ~0;
  is_del = 0;

  if (unformat (input, "del"))
    is_del = 1;

  if (! unformat_user (input, unformat_vnet_sw_interface, vnm, &sw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (unformat (input, "%U/%d", unformat_ip4_address, &a4, &length))
    error = ip4_add_del_interface_address (vm, sw_if_index, &a4, length, is_del);
  else if (unformat (input, "%U/%d", unformat_ip6_address, &a6, &length))
    error = ip6_add_del_interface_address (vm, sw_if_index, &a6, length, is_del);
  else
    {
      error = clib_error_return (0, "expected IP4/IP6 address/length `%U'",
				 format_unformat_error, input);
      goto done;
    }


 done:
  return error;
}

static VLIB_CLI_COMMAND (set_interface_ip_address_command) = {
  .path = "set interface ip address",
  .function = add_del_ip_address,
  .short_help = "Add/delete IP4/IP6 address for interface",
};

/* Dummy init function to get us linked in. */
static clib_error_t * ip4_cli_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (ip4_cli_init);
