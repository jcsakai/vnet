/*
 * interface_cli.c: interface CLI
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

#include <vnet/vnet.h>

static clib_error_t *
show_or_clear_hw_interfaces (vlib_main_t * vm,
			     unformat_input_t * input,
			     vlib_cli_command_t * cmd)
{
  clib_error_t * error = 0;
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;
  vnet_hw_interface_t * hi;
  u32 hw_if_index, * hw_if_indices = 0;
  int i, verbose = 1, is_show;

  is_show = strstr (cmd->path, "show") != 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      /* See if user wants to show a specific interface. */
      if (unformat (input, "%U", unformat_vnet_hw_interface, vnm, &hw_if_index))
	{
	  vec_add1 (hw_if_indices, hw_if_index);
	  /* Implies verbose. */
	  verbose = 1;
	}

      else if (unformat (input, "verbose"))
	verbose = 1;

      else if (unformat (input, "brief"))
	verbose = 0;

      else
	{
	  error = clib_error_return (0, "unknown input `%U'",
				     format_unformat_error, input);
	  goto done;
	}
    }
	
  /* Gather interfaces. */
  if (vec_len (hw_if_indices) == 0)
    pool_foreach (hi, im->hw_interfaces,
		  vec_add1 (hw_if_indices, hi - im->hw_interfaces));

  if (is_show)
    {
      /* Sort by name. */
      vec_sort (hw_if_indices, hi1, hi2,
		vnet_hw_interface_compare (&vnet_main, *hi1, *hi2));

      vlib_cli_output (vm, "%U\n", format_vnet_hw_interface, vnm, 0, verbose);
      for (i = 0; i < vec_len (hw_if_indices); i++)
	{
	  hi = vnet_get_hw_interface (vnm, hw_if_indices[i]);
	  vlib_cli_output (vm, "%U\n", format_vnet_hw_interface, vnm, hi, verbose);
	}
    }
  else
    {
      for (i = 0; i < vec_len (hw_if_indices); i++)
	{
	  vnet_device_class_t * dc;

	  hi = vnet_get_hw_interface (vnm, hw_if_indices[i]);
	  dc = vec_elt_at_index (im->device_classes, hi->dev_class_index);
	  
	  if (dc->clear_counters)
	    dc->clear_counters (hi->dev_instance);
	}
    }

 done:
  vec_free (hw_if_indices);
  return error;
}

static VLIB_CLI_COMMAND (show_hw_interfaces_command) = {
  .path = "show hardware-interfaces",
  .short_help = "Show interface hardware",
  .function = show_or_clear_hw_interfaces,
};

static VLIB_CLI_COMMAND (clear_hw_interface_counters_command) = {
  .path = "clear hardware-interfaces",
  .short_help = "Clear hardware interfaces statistics",
  .function = show_or_clear_hw_interfaces,
};

static clib_error_t *
show_sw_interfaces (vlib_main_t * vm,
		    unformat_input_t * input,
		    vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;
  vnet_sw_interface_t * si, * sorted_sis;

  vlib_cli_output (vm, "%U\n", format_vnet_sw_interface, vnm, 0);

  /* Gather interfaces. */
  sorted_sis = vec_new (vnet_sw_interface_t, pool_elts (im->sw_interfaces));
  _vec_len (sorted_sis) = 0;
  pool_foreach (si, im->sw_interfaces, ({ vec_add1 (sorted_sis, si[0]); }));

  /* Sort by name. */
  vec_sort (sorted_sis, si1, si2,
	    /* Crashes as shared library with vnm replaced for &vnet_main. */
	    vnet_sw_interface_compare (&vnet_main, si1->sw_if_index, si2->sw_if_index));

  vec_foreach (si, sorted_sis)
    {
      vlib_cli_output (vm, "%U\n", format_vnet_sw_interface, vnm, si);
    }

  vec_free (sorted_sis);
  return 0;
}

static VLIB_CLI_COMMAND (show_sw_interfaces_command) = {
  .path = "show interfaces",
  .short_help = "Show interfaces statistics",
  .function = show_sw_interfaces,
};

/* Root of all interface commands. */
static VLIB_CLI_COMMAND (vnet_cli_interface_command) = {
  .path = "interface",
  .short_help = "Interface commands",
};

static VLIB_CLI_COMMAND (vnet_cli_set_interface_command) = {
  .path = "set interface",
  .short_help = "Interface commands",
};

static clib_error_t *
clear_interface_counters (vlib_main_t * vm,
			  unformat_input_t * input,
			  vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;
  vlib_simple_counter_main_t * sm;
  vlib_combined_counter_main_t * cm;

  vec_foreach (sm, im->sw_if_counters)
    vlib_clear_simple_counters (sm);
  vec_foreach (cm, im->combined_sw_if_counters)
    vlib_clear_combined_counters (cm);

  return 0;
}

static VLIB_CLI_COMMAND (clear_interface_counters_command) = {
  .path = "clear interfaces",
  .short_help = "Clear interfaces statistics",
  .function = clear_interface_counters,
};

static clib_error_t *
create_sub_interfaces (vlib_main_t * vm,
		       unformat_input_t * input,
		       vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  clib_error_t * error = 0;
  u32 hw_if_index, sw_if_index;
  vnet_hw_interface_t * hi;
  u32 id, id_min, id_max;

  hw_if_index = ~0;
  if (! unformat_user (input, unformat_vnet_hw_interface, vnm, &hw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (unformat (input, "%d-%d", &id_min, &id_max))
    {
      if (id_min > id_max)
	goto id_error;
    }
  else if (unformat (input, "%d", &id_min))
    id_max = id_min;
  else
    {
    id_error:
      error = clib_error_return (0, "expected ID or ID MIN-MAX, got `%U'",
				 format_unformat_error, input);
      goto done;
    }

  hi = vnet_get_hw_interface (vnm, hw_if_index);
  for (id = id_min; id <= id_max; id++)
    {
      vnet_sw_interface_t template;

      template.type = VNET_SW_INTERFACE_TYPE_SUB;
      template.sup_sw_if_index = hi->sw_if_index;
      template.sub.id = id;
      sw_if_index = vnet_create_sw_interface (vnm, &template);
      hash_set (hi->sub_interface_sw_if_index_by_id, id, sw_if_index);
    }

  if (error)
    goto done;

 done:
  return error;
}

static VLIB_CLI_COMMAND (create_sub_interfaces_command) = {
  .path = "create sub-interface",
  .short_help = "Create sub-interfaces",
  .function = create_sub_interfaces,
};

static clib_error_t *
set_state (vlib_main_t * vm,
	   unformat_input_t * input,
	   vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  clib_error_t * error;
  u32 sw_if_index, flags;

  sw_if_index = ~0;
  if (! unformat_user (input, unformat_vnet_sw_interface, vnm, &sw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (! unformat (input, "%U", unformat_vnet_sw_interface_flags, &flags))
    {
      error = clib_error_return (0, "unknown flags `%U'",
				 format_unformat_error, input);
      goto done;
    }

  error = vnet_sw_interface_set_flags (vnm, sw_if_index, flags);
  if (error)
    goto done;

 done:
  return error;
}

static VLIB_CLI_COMMAND (set_state_command) = {
  .path = "set interface state",
  .short_help = "Set interface state",
  .function = set_state,
};

static clib_error_t *
set_hw_class (vlib_main_t * vm,
	      unformat_input_t * input,
	      vlib_cli_command_t * cmd)
{
  vnet_main_t * vnm = &vnet_main;
  vnet_interface_main_t * im = &vnm->interface_main;
  clib_error_t * error;
  u32 hw_if_index, hw_class_index;

  hw_if_index = ~0;
  if (! unformat_user (input, unformat_vnet_hw_interface, vnm, &hw_if_index))
    {
      error = clib_error_return (0, "unknown hardware interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (! unformat_user (input, unformat_hash_string,
		       im->hw_interface_class_by_name, &hw_class_index))
    {
      error = clib_error_return (0, "unknown hardware class `%U'",
				 format_unformat_error, input);
      goto done;
    }

  error = vnet_hw_interface_set_class (vnm, hw_if_index, hw_class_index);
  if (error)
    goto done;

 done:
  return error;
}

static VLIB_CLI_COMMAND (set_hw_class_command) = {
  .path = "set interface hw-class",
  .short_help = "Set interface hardware class",
  .function = set_hw_class,
};

static clib_error_t * vnet_interface_cli_init (vlib_main_t * vm)
{ return 0; }

VLIB_INIT_FUNCTION (vnet_interface_cli_init);

