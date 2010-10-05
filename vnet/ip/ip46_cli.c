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
  for (i = 0; i < ARRAY_LEN (a1->data_u32); i++)
    {
      int cmp = clib_net_to_host_u32 (a1->data_u32) - clib_net_to_host_u32 (a2->data_u32);
      if (cmp != 0)
	return cmp;
    }
  return 0;
}

static VLIB_CLI_COMMAND (set_interface_ip_command) = {
  .name = "ip",
  .short_help = "IP4/IP6 commands",
  .parent = &vlib_cli_set_interface_command,
};

static clib_error_t *
set_ip_address (vlib_main_t * vm,
		unformat_input_t * input,
		vlib_cli_command_t * cmd)
{
  ip4_address_t a4;
  ip6_address_t a6;
  clib_error_t * error = 0;
  u32 sw_if_index, length;

  sw_if_index = ~0;
  if (! unformat_user (input, unformat_vlib_sw_interface, vm, &sw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (unformat (input, "%U/%d", unformat_ip4_address, &a4, &length))
    ip4_set_interface_address (vm, sw_if_index, &a4, length);
  else if (unformat (input, "%U/%d", unformat_ip6_address, &a6, &length))
    ip6_set_interface_address (vm, sw_if_index, &a6, length);
  else
    {
      error = clib_error_return (0, "expected IP4/IP6 address/length `%U'",
				 format_unformat_error, input);
      goto done;
    }


 done:
  return error;
}

static VLIB_CLI_COMMAND (set_interface_ip4_address_command) = {
  .name = "address",
  .function = set_ip_address,
  .short_help = "Set IP4/IP6 address for interface",
  .parent = &set_interface_ip_command,
};

/* Dummy init function to get us linked in. */
static clib_error_t * ip4_cli_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (ip4_cli_init);
