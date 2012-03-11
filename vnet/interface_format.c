/*
 * interface_format.c: interface formatting
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

u8 * format_vnet_sw_interface_flags (u8 * s, va_list * args)
{
  u32 flags = va_arg (*args, u32);

  s = format (s, "%s", (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP) ? "up" : "down");
  if (flags & VNET_SW_INTERFACE_FLAG_PUNT)
    s = format (s, "/punt");

  return s;
}

u8 * format_vnet_hw_interface (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  vnet_hw_interface_t * hi = va_arg (*args, vnet_hw_interface_t *);
  vnet_hw_interface_class_t * hw_class;
  vnet_device_class_t * dev_class;
  int verbose = va_arg (*args, int);
  uword indent;

  if (! hi)
    return format (s, "%=32s%=8s%s",
		   "Name", "Link", "Hardware");

  indent = format_get_indent (s);

  s = format (s, "%-32v%=8s",
	      hi->name,
	      hi->flags & VNET_HW_INTERFACE_FLAG_LINK_UP ? "up" : "down");

  hw_class = vnet_get_hw_interface_class (vm, hi->hw_class_index);
  dev_class = vnet_get_device_class (vm, hi->dev_class_index);

  if (dev_class->format_device_name)  
    s = format (s, "%U", dev_class->format_device_name, hi->dev_instance);
  else
    s = format (s, "%s%d", dev_class->name, hi->dev_instance);

  if (verbose)
    {
      if (hw_class->format_device)
	s = format (s, "\n%U%U",
		    format_white_space, indent + 2,
		    hw_class->format_device, hi->hw_if_index);
      else
	{
	  s = format (s, "\n%U%s",
		      format_white_space, indent + 2,
		      hw_class->name);
	  if (hw_class->format_address && vec_len (hi->hw_address) > 0)
	    s = format (s, " address %U", hw_class->format_address, hi->hw_address);
	}

      if (dev_class->format_device)
	s = format (s, "\n%U%U",
		    format_white_space, indent + 2,
		    dev_class->format_device, hi->dev_instance);
    }

  return s;
}

u8 * format_vnet_sw_interface_name (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  vnet_sw_interface_t * si = va_arg (*args, vnet_sw_interface_t *);
  vnet_sw_interface_t * si_sup = vnet_get_sup_sw_interface (vm, si->sw_if_index);
  vnet_hw_interface_t * hi_sup;

  ASSERT (si_sup->type == VNET_SW_INTERFACE_TYPE_HARDWARE);
  hi_sup = vnet_get_hw_interface (vm, si_sup->hw_if_index);

  s = format (s, "%v", hi_sup->name);

  if (si->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    s = format (s, ".%d", si->sub.id);

  return s;
}

u8 * format_vnet_sw_if_index_name (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  u32 sw_if_index = va_arg (*args, u32);
  return format (s, "%U",
		 format_vnet_sw_interface_name, vm,
		 vnet_get_sw_interface (vm, sw_if_index));
}

u8 * format_vnet_sw_interface (u8 * s, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  vnet_sw_interface_t * si = va_arg (*args, vnet_sw_interface_t *);
  vnet_interface_main_t * im = &vm->interface_main;
  uword indent, n_printed;

  if (! si)
    return format (s, "%=32s%=16s%=16s%=16s",
		   "Name", "State", "Counter", "Count");

  s = format (s, "%-32U%=16U",
	      format_vnet_sw_interface_name, vm, si,
	      format_vnet_sw_interface_flags, si->flags);

  indent = format_get_indent (s);
  n_printed = 0;

  {
    vlib_combined_counter_main_t * cm;
    vlib_counter_t v;
    u8 * n = 0;

    vec_foreach (cm, im->combined_sw_if_counters)
      {
	vlib_get_combined_counter (cm, si->sw_if_index, &v);

	/* Only display non-zero counters. */
	if (v.packets == 0)
	  continue;

	if (n_printed > 0)
	  s = format (s, "\n%U", format_white_space, indent);
	n_printed += 2;

	if (n)
	  _vec_len (n) = 0;
	n = format (n, "%s packets", cm->name);
	s = format (s, "%-16v%16Ld", n, v.packets);

	_vec_len (n) = 0;
	n = format (n, "%s bytes", cm->name);
	s = format (s, "\n%U%-16v%16Ld",
		    format_white_space, indent,
		    n, v.bytes);
      }

    vec_free (n);
  }

  {
    vlib_simple_counter_main_t * cm;
    u64 v;

    vec_foreach (cm, im->sw_if_counters)
      {
	v = vlib_get_simple_counter (cm, si->sw_if_index);

	/* Only display non-zero counters. */
	if (v == 0)
	  continue;

	if (n_printed > 0)
	  s = format (s, "\n%U", format_white_space, indent);
	n_printed += 1;

	s = format (s, "%-16s%16Ld", cm->name, v);
      }
  }

  return s;
}

uword unformat_vnet_hw_interface (unformat_input_t * input, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  u32 * hw_if_index = va_arg (*args, u32 *);
  vnet_interface_main_t * im = &vm->interface_main;
  vnet_device_class_t * c;

  /* Try per device class functions first. */
  vec_foreach (c, im->device_classes)
    {
      if (c->unformat_device_name
	  && unformat_user (input, c->unformat_device_name, hw_if_index))
      return 1;
    }

  return unformat_user (input, unformat_hash_vec_string,
			im->hw_interface_by_name, hw_if_index);
}

uword unformat_vnet_sw_interface (unformat_input_t * input, va_list * args)
{
  vnet_main_t * vm = va_arg (*args, vnet_main_t *);
  u32 * result = va_arg (*args, u32 *);
  vnet_hw_interface_t * hi;
  u32 hw_if_index, id, id_specified;
  u8 * if_name = 0;
  uword * p, error = 0;

  id = ~0;
  if (unformat (input, "%_%v.%d%_", &if_name, &id)
      && ((p = hash_get (vm->interface_main.hw_interface_by_name, if_name))))
    {
      hw_if_index = p[0];
      id_specified = 1;
    }
  else if (unformat (input, "%U", unformat_vnet_hw_interface, vm, &hw_if_index))
    id_specified = 0;
  else
    goto done;

  hi = vnet_get_hw_interface (vm, hw_if_index);
  if (! id_specified)
    {
      *result = hi->sw_if_index;
    }
  else
    {
      if (! (p = hash_get (hi->sub_interface_sw_if_index_by_id, id)))
	return 0;
      *result = p[0];
    }
  error = 1;
 done:
  vec_free (if_name);
  return error;
}

uword unformat_vnet_sw_interface_flags (unformat_input_t * input, va_list * args)
{
  u32 * result = va_arg (*args, u32 *);
  u32 flags = 0;

  if (unformat (input, "up"))
    flags |= VNET_SW_INTERFACE_FLAG_ADMIN_UP;
  else if (unformat (input, "down"))
    flags &= ~VNET_SW_INTERFACE_FLAG_ADMIN_UP;
  else if (unformat (input, "punt"))
    flags |= VNET_SW_INTERFACE_FLAG_PUNT;
  else if (unformat (input, "enable"))
    flags &= ~VNET_SW_INTERFACE_FLAG_PUNT;
  else
    return 0;

  *result = flags;
  return 1;
}

uword unformat_vnet_hw_interface_flags (unformat_input_t * input, va_list * args)
{
  u32 * result = va_arg (*args, u32 *);
  u32 flags = 0;

  if (unformat (input, "up"))
    flags |= VNET_HW_INTERFACE_FLAG_LINK_UP;
  else if (unformat (input, "down"))
    flags &= ~VNET_HW_INTERFACE_FLAG_LINK_UP;
  else
    return 0;

  *result = flags;
  return 1;
}

