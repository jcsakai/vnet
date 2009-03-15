/*
 * cli.c: ethernet CLI
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

#include <vlib/vlib.h>
#include <vnet/ethernet/ethernet.h>

VLIB_CLI_COMMAND (vlib_cli_ethernet_command) = {
  .name = "ethernet",
  .short_help = "Ethernet commands",
};

static clib_error_t *
set_media (vlib_main_t * vm, unformat_input_t * input, vlib_cli_command_t * cmd)
{
  ethernet_main_t * em = &ethernet_main;
  ethernet_interface_t * ei;
  clib_error_t * error;
  ethernet_media_t m;
  u32 hw_if_index;

  if (! unformat (input, "%U%U",
		  unformat_ethernet_interface, vm, &hw_if_index,
		  unformat_ethernet_media, &m))
    {
      error = clib_error_return (0, "unknown input `%U'",
				 format_unformat_error, input);
      goto done;
    }

  ei = ethernet_get_interface (em, hw_if_index);
  error = ethernet_phy_set_media (&ei->phy, &m);
  if (error)
    goto done;

 done:
  return error;
}

static VLIB_CLI_COMMAND (ethernet_set_media_command) = {
  .name = "set-media",
  .short_help = "Set PHY media",
  .function = set_media,
  .parent = &vlib_cli_ethernet_command,
};

static clib_error_t *
phy_status (vlib_main_t * vm, unformat_input_t * input, vlib_cli_command_t * cmd)
{
  ethernet_main_t * em = &ethernet_main;
  vlib_hw_interface_t * hi;
  ethernet_interface_t * ei;
  clib_error_t * error;
  u32 hw_if_index;

  if (! unformat (input, "%U",
		  unformat_ethernet_interface, vm, &hw_if_index))
    {
      error = clib_error_return (0, "unknown input `%U'",
				 format_unformat_error, input);
      goto done;
    }

  ei = ethernet_get_interface (em, hw_if_index);
  error = ethernet_phy_status (&ei->phy);
  if (error)
    goto done;

  hi = vlib_get_hw_interface (vm, hw_if_index);
  vlib_cli_output (vm, "%v: phy status %U",
		 hi->name,
		 format_ethernet_media, &ei->phy.media);

 done:
  return error;
}

static VLIB_CLI_COMMAND (ethernet_status_command) = {
  .name = "status",
  .short_help = "PHY status\n",
  .function = phy_status,
  .parent = &vlib_cli_ethernet_command,
};

static clib_error_t *
helper (vlib_main_t * vm,
	unformat_input_t * input,
	vlib_cli_command_t * cmd,
	clib_error_t * (* f) (ethernet_phy_t * p))
{
  ethernet_main_t * em = &ethernet_main;
  ethernet_interface_t * ei;
  clib_error_t * error;
  u32 hw_if_index;

  if (! unformat (input, "%U",
		  unformat_ethernet_interface, vm, &hw_if_index))
    {
      error = clib_error_return (0, "unknown input `%U'",
				 format_unformat_error, input);
      goto done;
    }

  ei = ethernet_get_interface (em, hw_if_index);
  error = f (&ei->phy);
  if (error)
    goto done;

 done:
  return error;
}

static clib_error_t *
negotiate (vlib_main_t * vm,
	   unformat_input_t * input,
	   vlib_cli_command_t * cmd)
{ return helper (vm, input, cmd, ethernet_phy_negotiate_media); }

static VLIB_CLI_COMMAND (ethernet_negotiate_media_command) = {
  .name = "negotiate-media",
  .short_help = "Negotiate PHY media",
  .function = negotiate,
  .parent = &vlib_cli_ethernet_command,
};

static clib_error_t *
reset (vlib_main_t * vm,
		unformat_input_t * input,
		vlib_cli_command_t * cmd)
{ return helper (vm, input, cmd, ethernet_phy_reset); }

static VLIB_CLI_COMMAND (ethernet_reset_media_command) = {
  .name = "reset-media",
  .short_help = "Reset PHY media",
  .function = reset,
  .parent = &vlib_cli_ethernet_command,
};
