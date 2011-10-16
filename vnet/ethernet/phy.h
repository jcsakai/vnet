/*
 * phy.h: generic ethernet phy definitions
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

#ifndef included_ethernet_phy_h
#define included_ethernet_phy_h

#include <vlib/vlib.h>
#include <vnet/ethernet/phy_reg.h>

#define ethernet_foreach_media			\
  _ (unknown)					\
  _ (none)					\
  _ (1000T)					\
  _ (1000X)					\
  _ (100TX)					\
  _ (100T4)					\
  _ (10T)

typedef enum {
#define _(s) ETHERNET_MEDIA_##s,
  ethernet_foreach_media
#undef _
} ethernet_media_type_t;

typedef struct {
  u16 flags;
  /* Link can be either up/down or neither meaning unknown. */
#define ETHERNET_MEDIA_LINK_UP		(1 << 0)
#define ETHERNET_MEDIA_FULL_DUPLEX	(1 << 1)
#define ETHERNET_MEDIA_HALF_DUPLEX	(1 << 2)
#define ETHERNET_MEDIA_LOOPBACK		(1 << 3)
#define ETHERNET_MEDIA_MASTER		(1 << 4)
  /* No MII; raw 10 bit SERDES interface. */
#define ETHERNET_MEDIA_SERDES		(1 << 5)
#define ETHERNET_MEDIA_AUTONEG          (1 << 6)
#define ETHERNET_MEDIA_GMII_CLOCK       (1 << 7)
  ethernet_media_type_t type : 16;
} ethernet_media_t;

struct ethernet_phy_device_registration;
struct ethernet_phy;

typedef clib_error_t *
  (ethernet_phy_read_write_function_t) (struct ethernet_phy * phy,
					u32 phy_reg,
					u32 * data,
					vlib_read_or_write_t read_or_write);

typedef struct ethernet_phy {
  /* Back pointer. */
  vlib_main_t * vlib_main;

  /* Handle used by callback functions. */
  uword opaque;

  /* Function to read/write PHY registers. */
  ethernet_phy_read_write_function_t * read_write;

  /* MII bus address of PHY. */
  u16 phy_address;

  /* Values of BMSR/EXTSR after reset.  Used to figure capabilities
     of PHY. */
  u16 init_bmsr, init_extsr;

  /* PHY id. */
  u32 vendor_id;
  u16 device_id;
  u16 revision_id;

  /* Driver for this vendor/device ID. */
  struct ethernet_phy_device_registration * device;

  u32 flags;
  /* PHY cannot be isolated. */
#define ETHERNET_PHY_NO_ISOLATE (1 << 0)

  /* PHY does not support flow control. */
#define ETHERNET_PHY_NO_FLOW_CONTROL (1 << 1)

  /* Autonegotiation is pending. */
#define ETHERNET_PHY_AUTONEG_IN_PROGRESS (1 << 2)

  /* Time to wait before polling BMCR after issueing reset. */
  f64 reset_wait_time;

  /* Current PHY media. */
  ethernet_media_t media;

  /* Status as programed when not negotiating media. */
  ethernet_media_t set_media;
} ethernet_phy_t;

typedef struct {
  u16 vendor_id, device_id;
} ethernet_phy_device_id_t;

typedef struct ethernet_phy_device_registration {
  /* PHY specific init function. */
  clib_error_t * (* init) (ethernet_phy_t * phy);

  /* PHY specific reset function. */
  clib_error_t * (* reset) (ethernet_phy_t * phy);

  /* PHY specific status function. */
  clib_error_t * (* status) (ethernet_phy_t * phy);

  /* Vendor/device ids supported by this driver. */
  ethernet_phy_device_id_t supported_devices[];
} ethernet_phy_device_registration_t;

#define REGISTER_ETHERNET_PHY_DEVICE(x) \
  ethernet_phy_device_registration_t x CLIB_ELF_SECTION("vnet_ethernet_phy")

static inline ethernet_phy_device_registration_t *
ethernet_phy_device_next_registered (ethernet_phy_device_registration_t * r)
{
  uword i;

  /* Null vendor id marks end of initialized list. */
  for (i = 0; r->supported_devices[i].vendor_id != 0; i++)
    ;

  return clib_elf_section_data_next (r, i * sizeof (r->supported_devices[0]));
}

static inline clib_error_t *
ethernet_phy_read (ethernet_phy_t * phy, u32 reg, u32 * data)
{ return phy->read_write (phy, reg, data, VLIB_READ); }

static inline clib_error_t *
ethernet_phy_write (ethernet_phy_t * phy, u32 reg, u32 data)
{ return phy->read_write (phy, reg, &data, VLIB_WRITE); }

typedef struct {
  u32 reg, value;
} ethernet_phy_reg_t;

clib_error_t *
ethernet_phy_read_write_multiple (ethernet_phy_t * phy,
				  ethernet_phy_reg_t * regs,
				  u32 n_regs,
				  vlib_read_or_write_t read_or_write);

static inline clib_error_t *
ethernet_phy_read_multiple (ethernet_phy_t * phy,
			    ethernet_phy_reg_t * regs,
			    u32 n_regs)
{ return ethernet_phy_read_write_multiple (phy, regs, n_regs, VLIB_READ); }

static inline clib_error_t *
ethernet_phy_write_multiple (ethernet_phy_t * phy,
			    ethernet_phy_reg_t * regs,
			    u32 n_regs)
{ return ethernet_phy_read_write_multiple (phy, regs, n_regs, VLIB_WRITE); }

always_inline uword
ethernet_phy_is_link_up (ethernet_phy_t * phy)
{ return (phy->media.flags & ETHERNET_MEDIA_LINK_UP) != 0; }

clib_error_t * ethernet_phy_init (ethernet_phy_t * phy);
clib_error_t * ethernet_phy_reset (ethernet_phy_t * phy);
clib_error_t * ethernet_phy_negotiate_media (ethernet_phy_t * phy);
clib_error_t * ethernet_phy_set_media (ethernet_phy_t * phy,
				       ethernet_media_t * set);
clib_error_t * ethernet_phy_status (ethernet_phy_t * phy);

u8 * format_ethernet_media (u8 * s, va_list * args);
u8 * format_ethernet_media (u8 * s, va_list * args);

uword unformat_ethernet_media (unformat_input_t * input, va_list * args);

#endif /* included_ethernet_phy_h */
