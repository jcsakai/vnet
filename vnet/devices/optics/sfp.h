#ifndef included_vnet_optics_sfp_h
#define included_vnet_optics_sfp_h

#define foreach_sfp_id				\
  _ (unknown)					\
  _ (gbic)					\
  _ (on_motherboard)				\
  _ (sfp)

#include <clib/clib.h>

typedef struct {
  u8 id;
  u8 extended_id;
  u8 connector_type;
  u8 compatibility[8];
  u8 encoding;
  u8 nominal_bit_rate_100mbits_per_sec;
  u8 reserved13;
  u8 link_length[5];
  u8 reserved19;
  u8 vendor_name[16];
  u8 reserved36;
  u8 vendor_oui[3];
  u8 vendor_part_number[16];
  u8 vendor_revision[4];
  /* 16 bit value network byte order. */
  u8 laser_wavelength_in_nm[2];
  u8 reserved62;
  u8 checksum_0_to_62;

  u8 options[2];
  u8 max_bit_rate_margin_percent;
  u8 min_bit_rate_margin_percent;
  u8 vendor_serial_number[16];
  u8 vendor_date_code[8];
  u8 reserved92[3];
  u8 checksum_63_to_94;
  u8 vendor_specific[32];
  u8 reserved128[384];

  /* Vendor specific data follows. */
  u8 vendor_specific1[0];
} sfp_eeprom_t;

#endif /* included_vnet_optics_sfp_h */
