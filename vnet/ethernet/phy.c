/*
 * ethernet_phy.c: generic ethernet PHY driver
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

#include <vnet/ethernet/phy.h>

clib_error_t *
ethernet_phy_read_write_multiple (ethernet_phy_t * phy,
				  ethernet_phy_reg_t * regs,
				  u32 n_regs,
				  vlib_read_or_write_t read_or_write)
{
  clib_error_t * error = 0;
  ethernet_phy_reg_t * r;

  for (r = regs; r < regs + n_regs; r++)
    {
      error = phy->read_write (phy, r->reg, &r->value, read_or_write);
      if (error)
	break;
    }

  return error;
}

clib_error_t * ethernet_phy_reset (ethernet_phy_t * phy)
{
  vlib_main_t * vm = phy->vlib_main;
  clib_error_t * error = 0;
  f64 t_start;
  u32 r;

  r = ETHERNET_PHY_BMCR_RESET;
  if (phy->flags & ETHERNET_PHY_NO_ISOLATE)
    r &= ~ETHERNET_PHY_BMCR_ISOLATE;
  error = ethernet_phy_write (phy, ETHERNET_PHY_BMCR, r);
  if (error)
    goto done;

  /* Some PHYs want some time before BMCR is polled for. */
  if (phy->reset_wait_time > 0)
    vlib_time_wait (phy->vlib_main, phy->reset_wait_time);

  /* Wait up to 100ms for it to complete. */
  t_start = vlib_time_now (vm);
  while (1)
    {
      error = ethernet_phy_read (phy, ETHERNET_PHY_BMCR, &r);
      if (error)
	goto done;

      if ((r & ETHERNET_PHY_BMCR_RESET) == 0)
	break;

      if (vlib_time_now (vm) > t_start + 100e-3)
	{
	  error = clib_error_create ("timeout");
	  goto done;
	}
    }

  /* Call PHY specific reset function. */
  if (phy->device && phy->device->reset)
    error = phy->device->reset (phy);

 done:
  return error;
}

/* Find PHY specific driver. */
static ethernet_phy_device_registration_t *
find_phy_device (ethernet_phy_t * phy)
{
  vlib_main_t * vm = phy->vlib_main;
  vlib_elf_section_bounds_t * b, * bounds;
  ethernet_phy_device_registration_t * r;
  ethernet_phy_device_id_t * i;

  bounds = vlib_get_elf_section_bounds (vm, "ethernet_phy");
  vec_foreach (b, bounds)
    {
      for (r = b->lo;
	   r < (ethernet_phy_device_registration_t *) b->hi;
	   r = ethernet_phy_device_next_registered (r))
	{
	  for (i = r->supported_devices; i->vendor_id != 0; i++)
	    if (i->vendor_id == phy->vendor_id && i->device_id == phy->device_id)
	      return r;
	}
    }

  return 0;
}

static clib_error_t *
ethernet_phy_get_link (ethernet_phy_t * phy, u32 * bmsr)
{
  clib_error_t * error;

  /* Need to read it twice to get link status. */
  if ((error = ethernet_phy_read (phy, ETHERNET_PHY_BMSR, bmsr)))
    goto done;
  if ((error = ethernet_phy_read (phy, ETHERNET_PHY_BMSR, bmsr)))
    goto done;

 done:
  return error;
}

clib_error_t *
ethernet_phy_init (ethernet_phy_t * phy)
{
  clib_error_t * e;
  u32 bmsr, extsr;

  /* Read ID registers to get driver for this PHY. */
  {
    u32 id1, id2;

    if ((e = ethernet_phy_read (phy, ETHERNET_PHY_ID1, &id1)))
      goto done;
    if ((e = ethernet_phy_read (phy, ETHERNET_PHY_ID2, &id2)))
      goto done;

    phy->vendor_id = ethernet_phy_id_oui (id1, id2);
    phy->device_id = ethernet_phy_id_model (id1, id2);
    phy->revision_id = ethernet_phy_id_revision (id1, id2);

    phy->device = find_phy_device (phy);

    if (phy->device
	&& phy->device->init
	&& ((e = phy->device->init (phy))))
      goto done;
  }

  if ((e = ethernet_phy_read (phy, ETHERNET_PHY_BMSR, &bmsr)))
    goto done;

  extsr = 0;
  if (bmsr & ETHERNET_PHY_BMSR_EXTENDED_STATUS)
    {
      e = ethernet_phy_read (phy, ETHERNET_PHY_EXTSR, &extsr);
      if (e)
	goto done;
    }

  phy->init_bmsr = bmsr;
  phy->init_extsr = extsr;

 done:
  return e;
}

static inline int
ethernet_phy_is_1000x (ethernet_phy_t * phy)
{
  return 0 != (phy->init_extsr & (ETHERNET_PHY_EXTSR_1000X_FULL_DUPLEX
				  | ETHERNET_PHY_EXTSR_1000X_HALF_DUPLEX));
}

static inline int
ethernet_phy_is_1000t (ethernet_phy_t * phy)
{
  return 0 != (phy->init_extsr & (ETHERNET_PHY_EXTSR_1000T_FULL_DUPLEX
				  | ETHERNET_PHY_EXTSR_1000T_HALF_DUPLEX));
}

clib_error_t *
ethernet_phy_negotiate_media (ethernet_phy_t * phy)
{
  clib_error_t * error;

  /* 1000BASE-X auto negotiation is a bit different. */
  if (ethernet_phy_is_1000x (phy))
    {
      u32 anar = 0;
      
      if (phy->init_extsr & ETHERNET_PHY_EXTSR_1000X_FULL_DUPLEX)
	anar |= ETHERNET_PHY_ANAR_1000X_FULL_DUPLEX;

      if (phy->init_extsr & ETHERNET_PHY_EXTSR_1000X_HALF_DUPLEX)
	anar |= ETHERNET_PHY_ANAR_1000X_HALF_DUPLEX;

      /* XXX Asymmetric vs. symmetric? */
      if (! (phy->flags & ETHERNET_PHY_NO_FLOW_CONTROL))
	anar |= ETHERNET_PHY_ANAR_1000X_PAUSE_TOWARDS;

      error = ethernet_phy_write (phy, ETHERNET_PHY_ANAR, anar);
      if (error)
	goto done;
    }
  else
    {
      u32 anar = (ETHERNET_PHY_BMSR_MEDIA_TO_ANAR (phy->init_bmsr) |
		  ETHERNET_PHY_ANAR_CSMA);

      if (! (phy->flags & ETHERNET_PHY_NO_FLOW_CONTROL))
	{
	  anar |= ETHERNET_PHY_ANAR_FLOW_CONTROL;

	  /* XXX Only 1000BASE-T has PAUSE_ASYM? */
	  if (ethernet_phy_is_1000t (phy))
	    anar |= ETHERNET_PHY_ANAR_1000X_PAUSE_ASYM;
	}

      error = ethernet_phy_write (phy, ETHERNET_PHY_ANAR, anar);
      if (error)
	goto done;

      if (ethernet_phy_is_1000t (phy))
	{
	  u32 gtcr = 0;

	  if (phy->init_extsr & ETHERNET_PHY_EXTSR_1000T_FULL_DUPLEX)
	    gtcr |= ETHERNET_PHY_GTCR_ADV_1000T_FULL_DUPLEX;

	  if (phy->init_extsr & ETHERNET_PHY_EXTSR_1000T_HALF_DUPLEX)
	    gtcr |= ETHERNET_PHY_GTCR_ADV_1000T_HALF_DUPLEX;

	  error = ethernet_phy_write (phy, ETHERNET_PHY_GTCR, gtcr);
	  if (error)
	    goto done;
	}
    }

  error = ethernet_phy_write (phy, ETHERNET_PHY_BMCR,
			      (ETHERNET_PHY_BMCR_AUTONEG_ENABLE
			       | ETHERNET_PHY_BMCR_AUTONEG_START));

 done:
  return error;
}

clib_error_t *
ethernet_phy_set_media (ethernet_phy_t * phy,
			ethernet_media_t * set)
{
  clib_error_t * e;
  u32 bmcr, anar, gtcr;

  if (! (set->flags & (ETHERNET_MEDIA_FULL_DUPLEX | ETHERNET_MEDIA_HALF_DUPLEX)))
    set->flags |= ETHERNET_MEDIA_FULL_DUPLEX;

  anar = ETHERNET_PHY_ANAR_CSMA | ETHERNET_PHY_ANAR_FLOW_CONTROL;
  bmcr = gtcr = 0;

  switch (set->type)
    {
    case ETHERNET_MEDIA_unknown:
      bmcr |= ETHERNET_PHY_BMCR_ISOLATE;
      break;

    case ETHERNET_MEDIA_10T:
      bmcr |= ETHERNET_PHY_BMCR_SPEED_10;
      if (set->flags & ETHERNET_MEDIA_FULL_DUPLEX)
	{
	  anar |= ETHERNET_PHY_ANAR_10T_FULL_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_FULL_DUPLEX;
	}
      else
	{
	  anar |= ETHERNET_PHY_ANAR_10T_HALF_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_HALF_DUPLEX;
	}
      break;

    case ETHERNET_MEDIA_100TX:
      bmcr |= ETHERNET_PHY_BMCR_SPEED_100;
      if (set->flags & ETHERNET_MEDIA_FULL_DUPLEX)
	{
	  anar |= ETHERNET_PHY_ANAR_100TX_FULL_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_FULL_DUPLEX;
	}
      else
	{
	  anar |= ETHERNET_PHY_ANAR_100TX_HALF_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_HALF_DUPLEX;
	}
      break;

    case ETHERNET_MEDIA_100T4:
      bmcr |= ETHERNET_PHY_BMCR_SPEED_100;
      anar |= ETHERNET_PHY_ANAR_100T4;
      break;

    case ETHERNET_MEDIA_1000T:
      bmcr |= ETHERNET_PHY_BMCR_SPEED_1000;
      if (set->flags & ETHERNET_MEDIA_FULL_DUPLEX)
	{
	  gtcr |= ETHERNET_PHY_GTCR_ADV_1000T_FULL_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_FULL_DUPLEX;
	}
      else
	{
	  gtcr |= ETHERNET_PHY_GTCR_ADV_1000T_HALF_DUPLEX;
	  bmcr |= ETHERNET_PHY_BMCR_HALF_DUPLEX;
	}
      break;

    case ETHERNET_MEDIA_1000X:
      bmcr |= ETHERNET_PHY_BMCR_SPEED_1000;
      if (set->flags & ETHERNET_MEDIA_FULL_DUPLEX)
	bmcr |= ETHERNET_PHY_BMCR_FULL_DUPLEX;
      else
	bmcr |= ETHERNET_PHY_BMCR_HALF_DUPLEX;
      break;
    }

  if ((e = ethernet_phy_write (phy, ETHERNET_PHY_ANAR, anar)))
    goto done;
  if (ethernet_phy_is_1000t (phy)
      && (e = ethernet_phy_write (phy, ETHERNET_PHY_GTCR, gtcr)))
    goto done;
  if ((e = ethernet_phy_write (phy, ETHERNET_PHY_BMCR, bmcr)))
    goto done;

  phy->set_media = set[0];

 done:
  return e;
}

clib_error_t *
ethernet_phy_status (ethernet_phy_t * phy)
{
  u32 flags = 0;
  ethernet_media_type_t type = ETHERNET_MEDIA_unknown;
  u32 bmsr, bmcr, gtsr, gtcr, anar, anlpr;
  clib_error_t * error;

  if ((error = ethernet_phy_get_link (phy, &bmsr)))
    goto done;

  if (bmsr & ETHERNET_PHY_BMSR_LINK_UP)
    flags |= ETHERNET_MEDIA_LINK_UP;

  if ((error = ethernet_phy_read (phy, ETHERNET_PHY_BMCR, &bmcr)))
    goto done;

  if (bmcr & ETHERNET_PHY_BMCR_ISOLATE)
    goto done;

  if (bmcr & ETHERNET_PHY_BMCR_LOOPBACK)
    flags |= ETHERNET_MEDIA_LOOPBACK;

  if ((bmcr & ETHERNET_PHY_BMCR_AUTONEG_ENABLE)
      && (bmsr & ETHERNET_PHY_BMSR_AUTONEG_DONE))
    {
      if ((error = ethernet_phy_read (phy, ETHERNET_PHY_ANAR, &anar)))
	goto done;
      if ((error = ethernet_phy_read (phy, ETHERNET_PHY_ANLPAR, &anlpr)))
	goto done;

      /* Take best media advertised between us and link partner. */
      anar &= anlpr;

      gtcr = gtsr = 0;
      if (ethernet_phy_is_1000t (phy))
	{
	  if ((error = ethernet_phy_read (phy, ETHERNET_PHY_GTCR, &gtcr)))
	    goto done;
	  if ((error = ethernet_phy_read (phy, ETHERNET_PHY_GTSR, &gtsr)))
	    goto done;
	}

      if ((gtcr & ETHERNET_PHY_GTCR_ADV_1000T_FULL_DUPLEX)
	  && (gtsr & ETHERNET_PHY_GTSR_REMOTE_1000T_FULL_DUPLEX))
	{
	  flags |= ETHERNET_MEDIA_FULL_DUPLEX;
	  type = ETHERNET_MEDIA_1000T;
	}

      else if ((gtcr & ETHERNET_PHY_GTCR_ADV_1000T_HALF_DUPLEX)
	       && (gtsr & ETHERNET_PHY_GTSR_REMOTE_1000T_HALF_DUPLEX))
	{
	  flags |= ETHERNET_MEDIA_HALF_DUPLEX;
	  type = ETHERNET_MEDIA_1000T;
	}
      
      else if (anar & ETHERNET_PHY_ANAR_100T4)
	{
	  type = ETHERNET_MEDIA_100T4;
	}

      else if (anar & ETHERNET_PHY_ANAR_100TX_FULL_DUPLEX)
	{
	  flags |= ETHERNET_MEDIA_FULL_DUPLEX;
	  type = ETHERNET_MEDIA_100TX;
	}	  

      else if (anar & ETHERNET_PHY_ANAR_100TX_HALF_DUPLEX)
	{
	  flags |= ETHERNET_MEDIA_HALF_DUPLEX;
	  type = ETHERNET_MEDIA_100TX;
	}	  

      else if (anar & ETHERNET_PHY_ANAR_10T_FULL_DUPLEX)
	{
	  flags |= ETHERNET_MEDIA_FULL_DUPLEX;
	  type = ETHERNET_MEDIA_10T;
	}	  

      else if (anar & ETHERNET_PHY_ANAR_10T_HALF_DUPLEX)
	{
	  flags |= ETHERNET_MEDIA_HALF_DUPLEX;
	  type = ETHERNET_MEDIA_10T;
	}	  

      if (type == ETHERNET_MEDIA_1000T
	  && (gtsr & ETHERNET_PHY_GTSR_IS_MASTER))
	flags |= ETHERNET_MEDIA_MASTER;
    }

  else if (phy->device && phy->device->status)
    error = phy->device->status (phy);

  else
    {
      /* No autonegotiation: get status from programmed values. */
      flags |= phy->set_media.flags;
      type = phy->set_media.type;
    }

 done:
  phy->media.flags = flags;
  phy->media.type = type;
  return error;
}

u8 * format_ethernet_media_type (u8 * s, va_list * args)
{
  ethernet_media_type_t t = va_arg (*args, ethernet_media_type_t);
  char * n;
  switch (t)
    {
    default: n = "INVALID"; break;

#define _(s) case ETHERNET_MEDIA_##s: n = #s; break;
      ethernet_foreach_media;
#undef _
    }

  vec_add (s, n, strlen (n));
  return s;
}

uword unformat_ethernet_media (unformat_input_t * input, va_list * args)
{
  ethernet_media_t * s = va_arg (*args, ethernet_media_t *);

  if (unformat (input, "1000 half-duplex"))
    {
      s->type = ETHERNET_MEDIA_1000T;
      s->flags = ETHERNET_MEDIA_HALF_DUPLEX;
    }

  else if (unformat (input, "1000 full-duplex")
      || unformat (input, "1000"))
    {
      s->type = ETHERNET_MEDIA_1000T;
      s->flags = ETHERNET_MEDIA_FULL_DUPLEX;
    }

  else if (unformat (input, "1000x half-duplex"))
    {
      s->type = ETHERNET_MEDIA_1000X;
      s->flags = ETHERNET_MEDIA_HALF_DUPLEX;
    }

  else if (unformat (input, "1000x full-duplex")
      || unformat (input, "1000x"))
    {
      s->type = ETHERNET_MEDIA_1000X;
      s->flags = ETHERNET_MEDIA_FULL_DUPLEX;
    }

  else if (unformat (input, "100t4"))
    {
      s->type = ETHERNET_MEDIA_100T4;
      s->flags = 0;
    }

  else if (unformat (input, "100 half-duplex"))
    {
      s->type = ETHERNET_MEDIA_100TX;
      s->flags = ETHERNET_MEDIA_HALF_DUPLEX;
    }

  else if (unformat (input, "100 full-duplex")
	   || unformat (input, "100"))
    {
      s->type = ETHERNET_MEDIA_100TX;
      s->flags = ETHERNET_MEDIA_FULL_DUPLEX;
    }

  else if (unformat (input, "10 half-duplex"))
    {
      s->type = ETHERNET_MEDIA_10T;
      s->flags = ETHERNET_MEDIA_HALF_DUPLEX;
    }

  else if (unformat (input, "10 full-duplex")
	   || unformat (input, "10"))
    {
      s->type = ETHERNET_MEDIA_10T;
      s->flags = ETHERNET_MEDIA_FULL_DUPLEX;
    }

  else
    return 0;

  return 1;
}

u8 * format_ethernet_media (u8 * s, va_list * args)
{
  ethernet_media_t * m = va_arg (*args, ethernet_media_t *);

  s = format (s, "link %s, ", 
	      (m->flags & ETHERNET_MEDIA_LINK_UP) ? "up" : "down");

  s = format (s, "media %U%s, ",
	      format_ethernet_media_type, m->type,
	      ((m->flags & ETHERNET_MEDIA_FULL_DUPLEX)
	       ? " full-duplex"
	       : ((m->flags & ETHERNET_MEDIA_HALF_DUPLEX)
		  ? " half-duplex"
		  : "")));
  
  if (m->flags & ETHERNET_MEDIA_SERDES)
    s = format (s, "serdes, ");

  if (m->flags & ETHERNET_MEDIA_MASTER)
    s = format (s, "master, ");

  if (m->flags & ETHERNET_MEDIA_LOOPBACK)
    s = format (s, "loopback, ");

  if (m->flags & ETHERNET_MEDIA_AUTONEG)
    s = format (s, "auto-negotiation, ");

  if (m->flags & ETHERNET_MEDIA_GMII_CLOCK)
    s = format (s, "gmii-clock, ");

  return s;
}

/* Kludge to link in phy drivers along with phy.c */
void ethernet_phy_reference (void)
{
  extern void ethernet_phy_bcm_reference (void);
  ethernet_phy_bcm_reference ();
}
