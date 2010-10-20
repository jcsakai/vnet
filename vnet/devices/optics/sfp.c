#include <vnet/devices/optics/sfp.h>

static u8 * format_space_terminated (u8 * s, va_list * args)
{
  u32 l = va_arg (*args, u32);
  u8 * v = va_arg (*args, u8 *);
  u8 * p;

  for (p = v + l - 1; p >= v && p[0] == ' '; p--)
    ;
  vec_add (s, v, clib_min (p - v + 1, l));
  return s;
}

u8 * format_sfp_eeprom (u8 * s, va_list * args)
{
  sfp_eeprom_t * e = va_arg (*args, sfp_eeprom_t *);

  s = format (s, "Vendor: `%U', part `%U', revision `%U'",
	      format_space_terminated, sizeof (e->vendor_name), e->vendor_name,
	      format_space_terminated, sizeof (e->vendor_part_number), e->vendor_part_number,
	      format_space_terminated, sizeof (e->vendor_revision), e->vendor_revision);

  return s;
}
