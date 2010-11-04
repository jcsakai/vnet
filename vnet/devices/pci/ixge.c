#include <vnet/devices/pci/ixge.h>
#include <vnet/devices/xge/xge.h>
#include <vnet/ethernet/ethernet.h>
#include <vlib/unix/unix.h>
#include <vlib/unix/pci.h>

ixge_main_t ixge_main;

static void ixge_semaphore_get (ixge_device_t * xd)
{
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
  ixge_regs_t * r = xd->regs;
  u32 i;

  i = 0;
  while (! (r->software_semaphore & (1 << 0)))
    {
      if (i > 0)
	vlib_process_suspend (vm, 100e-6);
      i++;
    }
  do {
    r->software_semaphore |= 1 << 1;
  } while (! (r->software_semaphore & (1 << 1)));
}

static void ixge_semaphore_release (ixge_device_t * xd)
{
  ixge_regs_t * r = xd->regs;
  r->software_semaphore &= ~3;
}

static void ixge_software_firmware_sync (ixge_device_t * xd, u32 sw_mask)
{
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
  ixge_regs_t * r = xd->regs;
  u32 fw_mask = sw_mask << 5;
  u32 m, done = 0;

  while (! done)
    {
      ixge_semaphore_get (xd);
      m = r->software_firmware_sync;
      done = (m & fw_mask) == 0;
      if (done)
	r->software_firmware_sync = m | sw_mask;
      ixge_semaphore_release (xd);
      if (! done)
	vlib_process_suspend (vm, 10e-3);
    }
}

static void ixge_software_firmware_sync_release (ixge_device_t * xd, u32 sw_mask)
{
  ixge_regs_t * r = xd->regs;
  ixge_semaphore_get (xd);
  r->software_firmware_sync &= ~sw_mask;
  ixge_semaphore_release (xd);
}

u32 ixge_read_write_phy_reg (ixge_device_t * xd, u32 dev_type, u32 reg_index, u32 v, u32 is_read)
{
  ixge_regs_t * r = xd->regs;
  const u32 busy_bit = 1 << 30;
  u32 x;
  
  ASSERT (xd->phy_index < 2);
  ixge_software_firmware_sync (xd, 1 << (1 + xd->phy_index));

  ASSERT (reg_index < (1 << 16));
  ASSERT (dev_type < (1 << 5));
  if (! is_read)
    r->xge_mac.phy_data = v;

  /* Address cycle. */
  x = reg_index | (dev_type << 16) | (xd->phys[xd->phy_index].mdio_address << 21);
  r->xge_mac.phy_command = x | busy_bit;
  /* Busy wait timed to take 28e-6 secs.  No suspend. */
  while (r->xge_mac.phy_command & busy_bit)
    ;

  r->xge_mac.phy_command = x | ((is_read ? 2 : 1) << 26) | busy_bit;
  while (r->xge_mac.phy_command & busy_bit)
    ;

  if (is_read)
    v = r->xge_mac.phy_data >> 16;

  ixge_software_firmware_sync_release (xd, 1 << (1 + xd->phy_index));

  return v;
}

static u32 ixge_read_phy_reg (ixge_device_t * xd, u32 dev_type, u32 reg_index)
{ return ixge_read_write_phy_reg (xd, dev_type, reg_index, 0, /* is_read */ 1); }

static void ixge_write_phy_reg (ixge_device_t * xd, u32 dev_type, u32 reg_index, u32 v)
{ (void) ixge_read_write_phy_reg (xd, dev_type, reg_index, v, /* is_read */ 0); }

static void ixge_i2c_put_bits (i2c_bus_t * b, int scl, int sda)
{
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, b->private);
  u32 v;

  v = 0;
  v |= (sda != 0) << 3;
  v |= (scl != 0) << 1;
  xd->regs->i2c_control = v;
}

static void ixge_i2c_get_bits (i2c_bus_t * b, int * scl, int * sda)
{
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, b->private);
  u32 v;

  v = xd->regs->i2c_control;
  *sda = (v & (1 << 2)) != 0;
  *scl = (v & (1 << 0)) != 0;
}

static u16 ixge_read_eeprom (ixge_device_t * xd, u32 address)
{
  ixge_regs_t * r = xd->regs;
  u32 v;
  r->eeprom_read = ((/* start bit */ (1 << 0)) | (address << 2));
  /* Wait for done bit. */
  while (! ((v = r->eeprom_read) & (1 << 1)))
    ;
  return v >> 16;
}

static clib_error_t *
ixge_sfp_phy_init_from_eeprom (ixge_device_t * xd, u16 sfp_type)
{
  u16 a, id, reg_values_addr;

  a = ixge_read_eeprom (xd, 0x2b);
  if (a == 0 || a == 0xffff)
    return clib_error_create ("no init sequence in eeprom");

  while (1)
    {
      id = ixge_read_eeprom (xd, ++a);
      if (id == 0xffff)
	break;
      reg_values_addr = ixge_read_eeprom (xd, ++a);
      if (id == sfp_type)
	break;
    }
  if (id != sfp_type)
    return clib_error_create ("failed to find id 0x%x", sfp_type);

  while (1)
    {
      u16 v = ixge_read_eeprom (xd, ++reg_values_addr);
      if (v == 0xffff)
	break;
      xd->regs->core_analog_config = v;
    }

  return 0;
}

static void ixge_sfp_phy_setup (ixge_device_t * xd, int wait)
{
  u32 v, last, i, n_resets;

  n_resets = 0;
 again:
  /* pma/pmd 10g serial SFI. */
  xd->regs->xge_mac.auto_negotiation_control2 &= ~(3 << 16);
  xd->regs->xge_mac.auto_negotiation_control2 |= 2 << 16;

  v = xd->regs->xge_mac.auto_negotiation_control;
  /* 10g pma/pmd type => kx4 */
  v &= ~(3 << 7);
  v |= (1 << 7);
  /* link mode 10g sfi serdes */
  v &= ~(7 << 13);
  v |= (3 << 13);
  /* restart autoneg. */
  v |= (1 << 12);
  xd->regs->xge_mac.auto_negotiation_control = v;

  /* spd 5 => is_10g speed.
     spd 3 => disable laser.  both outputs. */
  /* Configure pins 3 & 5 as output. */
  v = ((1 << 3) | (1 << 5)) << 8;
  /* Select 10g and enable laser. */
  v |= ((1 << 5) | (0 << 3));
  xd->regs->sdp_control = v;

  if (! wait)
    return;

  i = 0;
  last = 0;
  while (1)
    {
      v = xd->regs->xge_mac.link_status;
      if (v != last)
	{
	  ELOG_TYPE_DECLARE (e) = {
	    .function = (char *) __FUNCTION__,
	    .format = "ixge %d, link 0x%x mode %s speed %s",
	    .format_args = "i4i4t1t1",
	    .n_enum_strings = 8,
	    .enum_strings = {
	      "1g", "10g parallel", "10g serial", "autoneg",
	      "unknown", "100m", "1g", "10g",
	    },
	  };
	  struct { u32 instance, link; u8 mode, speed; } * ed;
	  ed = ELOG_DATA (&vlib_global_main.elog_main, e);
	  ed->instance = xd->device_index;
	  ed->link = v;
	  ed->mode = 0 + ((v >> 26) & 3);
	  ed->speed = 4 + ((v >> 28) & 3);
	  last = v;
	}
      if (v & (1 << 30))
	break;
      i++;
      if (i > (1 << 20))
	{
	  ELOG_TYPE_DECLARE (e) = {
	    .function = (char *) __FUNCTION__,
	    .format = "ixge %d, reset mac and try again",
	    .format_args = "i4",
	  };
	  struct { u32 instance; } * ed;

	  if (++n_resets >= 3)
	    break;

	  ed = ELOG_DATA (&vlib_global_main.elog_main, e);
	  ed->instance = xd->device_index;

	  xd->regs->control |= 1 << 3;
	  while (xd->regs->control & (1 << 3))
	    ;
	  goto again;
	}
    }
}

static void ixge_sfp_phy_init (ixge_device_t * xd)
{
  ixge_phy_t * phy = xd->phys + xd->phy_index;
  i2c_bus_t * ib = &xd->i2c_bus;
  u8 start_address[1];
  u32 timed_out;

  ib->private = xd->device_index;
  ib->put_bits = ixge_i2c_put_bits;
  ib->get_bits = ixge_i2c_get_bits;
  i2c_init (ib);

  start_address[0] = 0;
  timed_out = i2c_write_read (ib, 0xa0,
			      &start_address, 1,
			      &xd->sfp_eeprom, 128);
  if (timed_out || ! sfp_eeprom_is_valid (&xd->sfp_eeprom))
    xd->sfp_eeprom.id = SFP_ID_unknown;
  else
    {
      /* FIXME 5 => SR/LR eeprom ID. */
      clib_error_t * e = ixge_sfp_phy_init_from_eeprom (xd, 5 + xd->pci_function);
      if (e)
	clib_error_report (e);
      ixge_sfp_phy_setup (xd, /* wait */ 0);
    }

  phy->mdio_address = ~0;
}

static void ixge_phy_init (ixge_device_t * xd)
{
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
  ixge_phy_t * phy = xd->phys + xd->phy_index;

  switch (xd->device_id)
    {
    case IXGE_82599_sfp:
    case IXGE_82599_sfp_em:
    case IXGE_82599_sfp_fcoe:
      /* others? */
      return ixge_sfp_phy_init (xd);

    default:
      break;
    }

  /* Probe address of phy. */
  {
    u32 i, v;

    phy->mdio_address = ~0;
    for (i = 0; i < 32; i++)
      {
	phy->mdio_address = i;
	v = ixge_read_phy_reg (xd, XGE_PHY_DEV_TYPE_PMA_PMD, XGE_PHY_ID1);
	if (v != 0xffff && v != 0)
	  break;
      }

    /* No PHY found? */
    if (i >= 32)
      return;
  }

  phy->id = ((ixge_read_phy_reg (xd, XGE_PHY_DEV_TYPE_PMA_PMD, XGE_PHY_ID1) << 16)
	     | ixge_read_phy_reg (xd, XGE_PHY_DEV_TYPE_PMA_PMD, XGE_PHY_ID2));

  {
    ELOG_TYPE_DECLARE (e) = {
      .function = (char *) __FUNCTION__,
      .format = "ixge %d, phy id 0x%d mdio address %d",
      .format_args = "i4i4i4",
    };
    struct { u32 instance, id, address; } * ed;
    ed = ELOG_DATA (&vm->elog_main, e);
    ed->instance = xd->device_index;
    ed->id = phy->id;
    ed->address = phy->mdio_address;
  }

  /* Reset phy. */
  ixge_write_phy_reg (xd, XGE_PHY_DEV_TYPE_PHY_XS, XGE_PHY_CONTROL, XGE_PHY_CONTROL_RESET);

  /* Wait for self-clearning reset bit to clear. */
  do {
    vlib_process_suspend (vm, 1e-3);
  } while (ixge_read_phy_reg (xd, XGE_PHY_DEV_TYPE_PHY_XS, XGE_PHY_CONTROL) & XGE_PHY_CONTROL_RESET);
}

typedef struct {
  ixge_descriptor_t before, after;

  u32 buffer_index;

  u16 device_index;

  u8 queue_index;

  u8 is_start_of_packet;

  /* Copy of VLIB buffer; packet data stored in pre_data. */
  vlib_buffer_t buffer;
} ixge_dma_trace_t;

static u8 * format_ixge_rx_from_hw_descriptor (u8 * s, va_list * va)
{
  ixge_rx_from_hw_descriptor_t * d = va_arg (*va, ixge_rx_from_hw_descriptor_t *);
  u32 s0 = d->status[0], s2 = d->status[2];
  u32 is_ip4, is_ip6, is_ip, is_tcp, is_udp;
  uword indent = format_get_indent (s);

  s = format (s, "%s-owned",
	      (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE) ? "sw" : "hw");
  s = format (s, ", length this descriptor %d, l3 offset %d",
	      d->n_packet_bytes_this_descriptor,
	      IXGE_RX_DESCRIPTOR_STATUS0_L3_OFFSET (s0));
  if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET)
    s = format (s, ", end-of-packet");

  s = format (s, "\n%U", format_white_space, indent);

  if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_ETHERNET_ERROR)
    s = format (s, "layer2 error");

  if (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_LAYER2)
    {
      s = format (s, "layer 2 type %d", (s0 & 0x1f));
      return s;
    }

  if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_VLAN)
    s = format (s, "vlan header 0x%x\n%U", d->vlan_tag,
		format_white_space, indent);

  if ((is_ip4 = (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_IP4)))
    {
      s = format (s, "ip4%s",
		  (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_IP4_EXT) ? " options" : "");
      if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_IP4_CHECKSUMMED)
	s = format (s, " checksum %s",
		    (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IP4_CHECKSUM_ERROR) ? "bad" : "ok");
    }
  if ((is_ip6 = (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_IP6)))
    s = format (s, "ip6%s",
		(s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_IP6_EXT) ? " extended" : "");
  is_tcp = is_udp = 0;
  if ((is_ip = (is_ip4 | is_ip6)))
    {
      is_tcp = (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_TCP) != 0;
      is_udp = (s0 & IXGE_RX_DESCRIPTOR_STATUS0_IS_UDP) != 0;
      if (is_tcp)
	s = format (s, ", tcp");
      if (is_udp)
	s = format (s, ", udp");
    }

  if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_L4_CHECKSUMMED)
    s = format (s, ", l4 checksum %s",
		(s2 & IXGE_RX_DESCRIPTOR_STATUS2_L4_CHECKSUM_ERROR) ? "bad" : "ok");
  if (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_UDP_CHECKSUMMED)
    s = format (s, ", udp checksum %s",
		(s2 & IXGE_RX_DESCRIPTOR_STATUS2_UDP_CHECKSUM_ERROR) ? "bad" : "ok");

  return s;
}

static u8 * format_ixge_dma_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_node_t * node = va_arg (*va, vlib_node_t *);
  ixge_dma_trace_t * t = va_arg (*va, ixge_dma_trace_t *);
  vlib_rx_or_tx_t rx_or_tx = va_arg (*va, int);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, t->device_index);
  ixge_dma_queue_t * dq;
  format_function_t * f;
  uword indent = format_get_indent (s);

  dq = vec_elt_at_index (xd->dma_queues[rx_or_tx], t->queue_index);

  {
    vlib_sw_interface_t * sw = vlib_get_sw_interface (vm, xd->vlib_sw_if_index);
    s = format (s, "%U %U queue %d",
		format_vlib_sw_interface_name, vm, sw,
		format_vlib_rx_tx, rx_or_tx,
		t->queue_index);
  }

  s = format (s, "\n%Ubefore: %U",
	      format_white_space, indent,
	      format_ixge_rx_from_hw_descriptor, &t->before);
  s = format (s, "\n%Uafter : head/tail address 0x%Lx/0x%Lx",
	      format_white_space, indent,
	      t->after.rx_to_hw.head_address,
	      t->after.rx_to_hw.tail_address);

  s = format (s, "\n%Ubuffer 0x%x: %U",
	      format_white_space, indent,
	      t->buffer_index,
	      format_vlib_buffer, &t->buffer);

  s = format (s, "\n%U",
	      format_white_space, indent);

  f = node->format_buffer;
  if (! f || ! t->is_start_of_packet)
    f = format_hex_bytes;
  s = format (s, "%U", f, t->buffer.pre_data, sizeof (t->buffer.pre_data));

  return s;
}

static u8 * format_ixge_dma_rx_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_node_t * node = va_arg (*va, vlib_node_t *);
  ixge_dma_trace_t * t = va_arg (*va, ixge_dma_trace_t *);
  return format (s, "%U", format_ixge_dma_trace, vm, node, t, VLIB_RX);
}

typedef struct {
  vlib_node_runtime_t * node;

  u32 next_index;

  u32 saved_start_of_packet_buffer_index;

  u32 saved_start_of_packet_next_index;
  u32 saved_last_buffer_index;

  u32 is_start_of_packet;

  u32 n_descriptors_done_total;

  u32 n_descriptors_done_this_call;

  u32 n_bytes;
} ixge_rx_state_t;

#define foreach_ixge_rx_error				\
  _ (none, "no error")					\
  _ (ip4_checksum_error, "ip4 checksum errors")

typedef enum {
#define _(f,s) IXGE_RX_ERROR_##f,
  foreach_ixge_rx_error
#undef _
  IXGE_RX_N_ERROR,
} ixge_rx_error_t;

typedef enum {
  IXGE_RX_NEXT_IP4_INPUT,
  IXGE_RX_NEXT_IP6_INPUT,
  IXGE_RX_NEXT_ETHERNET_INPUT,
  IXGE_RX_NEXT_DROP,
  IXGE_RX_N_NEXT,
} ixge_rx_next_t;

always_inline void
ixge_rx_next_and_error_from_status_x1 (u32 s00, u32 s02,
				       u8 * next0, u8 * error0)
{
  u8 is0_ip4, is0_ip6, n0, e0;

  e0 = IXGE_RX_ERROR_none;
  n0 = IXGE_RX_NEXT_ETHERNET_INPUT;

  is0_ip4 = s02 & IXGE_RX_DESCRIPTOR_STATUS2_IS_IP4_CHECKSUMMED;
  n0 = is0_ip4 ? IXGE_RX_NEXT_IP4_INPUT : n0;
  e0 = (is0_ip4 && (s02 & IXGE_RX_DESCRIPTOR_STATUS2_IP4_CHECKSUM_ERROR)
	? IXGE_RX_ERROR_ip4_checksum_error
	: e0);

  is0_ip6 = s00 & IXGE_RX_DESCRIPTOR_STATUS0_IS_IP6;
  n0 = is0_ip6 ? IXGE_RX_NEXT_IP6_INPUT : n0;

  /* Check for error. */
  n0 = e0 != IXGE_RX_ERROR_none ? IXGE_RX_NEXT_DROP : n0;

  *error0 = e0;
  *next0 = n0;
}

always_inline void
ixge_rx_next_and_error_from_status_x2 (u32 s00, u32 s02,
				       u32 s10, u32 s12,
				       u8 * next0, u8 * error0,
				       u8 * next1, u8 * error1)
{
  ixge_rx_next_and_error_from_status_x1 (s00, s02, next0, error0);
  ixge_rx_next_and_error_from_status_x1 (s10, s12, next1, error1);
}

static void
ixge_rx_trace (ixge_main_t * xm,
	       ixge_device_t * xd,
	       ixge_dma_queue_t * dq,
	       ixge_rx_state_t * rx_state,
	       ixge_descriptor_t * before_descriptors,
	       u32 * before_buffers,
	       ixge_descriptor_t * after_descriptors,
	       uword n_descriptors)
{
  vlib_main_t * vm = xm->vlib_main;
  vlib_node_runtime_t * node = rx_state->node;
  ixge_rx_from_hw_descriptor_t * bd;
  ixge_rx_to_hw_descriptor_t * ad;
  u32 * b, n_left, is_sop, next_index_sop;

  n_left = n_descriptors;
  b = before_buffers;
  bd = &before_descriptors->rx_from_hw;
  ad = &after_descriptors->rx_to_hw;
  is_sop = rx_state->is_start_of_packet;
  next_index_sop = rx_state->saved_start_of_packet_next_index;

  while (n_left >= 2) {
    u32 bi0, bi1;
    vlib_buffer_t * b0, * b1;
    ixge_dma_trace_t * t0, * t1;
    u8 next0, error0, next1, error1;

    bi0 = b[0];
    bi1 = b[1];
    n_left -= 2;

    b0 = vlib_get_buffer (vm, bi0);
    b1 = vlib_get_buffer (vm, bi1);

    ixge_rx_next_and_error_from_status_x2 (bd[0].status[0], bd[0].status[2],
					   bd[1].status[0], bd[1].status[2],
					   &next0, &error0,
					   &next1, &error1);

    next_index_sop = is_sop ? next0 : next_index_sop;
    vlib_trace_buffer (vm, node, next_index_sop, b0, /* follow_chain */ 0);
    t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
    t0->is_start_of_packet = is_sop;
    is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

    next_index_sop = is_sop ? next1 : next_index_sop;
    vlib_trace_buffer (vm, node, next_index_sop, b1, /* follow_chain */ 0);
    t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
    t1->is_start_of_packet = is_sop;
    is_sop = (b1->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

    t0->queue_index = dq->queue_index;
    t1->queue_index = dq->queue_index;
    t0->device_index = xd->device_index;
    t1->device_index = xd->device_index;
    t0->before.rx_from_hw = bd[0];
    t1->before.rx_from_hw = bd[1];
    t0->after.rx_to_hw = ad[0];
    t1->after.rx_to_hw = ad[1];
    t0->buffer_index = bi0;
    t1->buffer_index = bi1;
    memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
    memcpy (&t1->buffer, b1, sizeof (b1[0]) - sizeof (b0->pre_data));
    memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));
    memcpy (t1->buffer.pre_data, b1->data, sizeof (t1->buffer.pre_data));

    b += 2;
    bd += 2;
    ad += 2;
  }

  while (n_left >= 1) {
    u32 bi0;
    vlib_buffer_t * b0;
    ixge_dma_trace_t * t0;
    u8 next0, error0;

    bi0 = b[0];
    n_left -= 1;

    b0 = vlib_get_buffer (vm, bi0);

    ixge_rx_next_and_error_from_status_x1 (bd[0].status[0], bd[0].status[2],
					   &next0, &error0);

    next_index_sop = is_sop ? next0 : next_index_sop;
    vlib_trace_buffer (vm, node, next_index_sop, b0, /* follow_chain */ 0);
    t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
    t0->is_start_of_packet = is_sop;
    is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

    t0->queue_index = dq->queue_index;
    t0->device_index = xd->device_index;
    t0->before.rx_from_hw = bd[0];
    t0->after.rx_to_hw = ad[0];
    t0->buffer_index = bi0;
    memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
    memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));

    b += 1;
    bd += 1;
    ad += 1;
  }
}

#if 0
static void
ixge_dma_tx_trace (ixge_dma_channel_t * c,
		   ixge_dma_descriptor_t * before_descriptors,
		   ixge_dma_descriptor_t * after_descriptors,
		   uword n_descriptors)
{
  q_board_main_t * bm = &q_board_main;
  vlib_main_t * vm = bm->vlib_main;
  vlib_node_runtime_t * node = c->node;
  ixge_main_t * sm = &bm->ixge_main;
  ixge_dma_main_t * pm = &sm->dma_main;
  ixge_dma_descriptor_t * b, * a;
  u32 n_left, is_sop, next0_sop;

  n_left = n_descriptors;
  b = before_descriptors;
  a = after_descriptors;
  next0_sop = ~0;
  is_sop = 1;

  while (n_left >= 2) {
    u32 bi0, bi1;
    vlib_buffer_t * b0, * b1;
    ixge_dma_trace_t * t0, * t1;

    bi0 = a[0].software_defined;
    bi1 = a[1].software_defined;
    n_left -= 2;

    b0 = vlib_get_buffer (vm, bi0);
    b1 = vlib_get_buffer (vm, bi1);

    if (b0->flags & VLIB_BUFFER_IS_TRACED) {
      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->channel_index = c - pm->channels[VLIB_TX];
      t0->before_descriptor = b[0];
      t0->after_descriptor = a[0];
      t0->buffer_index = bi0;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data + b0->current_data, sizeof (t0->buffer.pre_data));
    }
    if (b1->flags & VLIB_BUFFER_IS_TRACED) {
      t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
      t1->channel_index = c - pm->channels[VLIB_TX];
      t1->before_descriptor = b[1];
      t1->after_descriptor = a[1];
      t1->buffer_index = bi1;
      memcpy (&t1->buffer, b1, sizeof (b1[0]) - sizeof (b0->pre_data));
      memcpy (t1->buffer.pre_data, b1->data + b1->current_data, sizeof (t1->buffer.pre_data));
    }
    b += 2;
    a += 2;
  }

  while (n_left >= 1) {
    u32 bi0;
    vlib_buffer_t * b0;
    ixge_dma_trace_t * t0;

    bi0 = a[0].software_defined;
    n_left -= 1;

    b0 = vlib_get_buffer (vm, bi0);

    if (b0->flags & VLIB_BUFFER_IS_TRACED) {
      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->channel_index = c - pm->channels[VLIB_TX];
      t0->before_descriptor = b[0];
      t0->after_descriptor = a[0];
      t0->buffer_index = bi0;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data + b0->current_data, sizeof (t0->buffer.pre_data));
    }

    b += 1;
    a += 1;
  }
}
#endif

always_inline uword
ixge_ring_sub (ixge_dma_queue_t * q, u32 i0, u32 i1)
{
  i32 d = i1 - i0;
  ASSERT (i0 < q->n_descriptors);
  ASSERT (i1 < q->n_descriptors);
  return d < 0 ? -d : d;
}

always_inline uword
ixge_ring_add (ixge_dma_queue_t * q, u32 i0, u32 i1)
{
  u32 d = i0 + i1;
  ASSERT (i0 < q->n_descriptors);
  ASSERT (i1 < q->n_descriptors);
  d -= d >= q->n_descriptors ? q->n_descriptors : 0;
  return d;
}

always_inline ixge_dma_regs_t *
get_dma_regs (ixge_device_t * xd, vlib_rx_or_tx_t rt, u32 qi)
{
  ixge_regs_t * r = xd->regs;
  ASSERT (qi < 128);
  if (rt == VLIB_RX)
    return qi < 64 ? &r->rx_dma0[qi] : &r->rx_dma1[qi - 64];
  else
    return &r->tx_dma[qi];
}

always_inline uword
ixge_tx_descriptor_matches_template (ixge_main_t * xm, ixge_tx_descriptor_t * d)
{
  int i;
  for (i = 0; i < ARRAY_LEN (d->status); i++)
    if ((d->status[i] & xm->tx_descriptor_template_mask.status[i])
	!= xm->tx_descriptor_template.status[i])
      return 0;
  return 1;
}

typedef struct {
  u32 is_start_of_packet;

  u32 n_bytes_in_packet;

  ixge_tx_descriptor_t * start_of_packet_descriptor;
} ixge_tx_state_t;

static uword
ixge_tx_no_wrap (ixge_main_t * xm,
		 ixge_device_t * xd,
		 ixge_dma_queue_t * dq,
		 u32 * buffers,
		 u32 start_descriptor_index,
		 u32 n_descriptors,
		 ixge_tx_state_t * tx_state)
{
  vlib_main_t * vm = xm->vlib_main;
  ixge_tx_descriptor_t * d, * d_sop;
  u32 n_left = n_descriptors;
  u32 * to_free = vec_end (xm->tx_buffers_pending_free);
  u32 * to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 is_sop = tx_state->is_start_of_packet;
  u32 len_sop = tx_state->n_bytes_in_packet;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index].tx;
  d_sop = is_sop ? d : tx_state->start_of_packet_descriptor;

  while (n_left >= 4)
    {
      vlib_buffer_t * b0, * b1;
      u32 bi0, fi0, len0;
      u32 bi1, fi1, len1;
      u8 is_eop0, is_eop1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, buffers[2], LOAD);
      vlib_prefetch_buffer_with_index (vm, buffers[3], LOAD);
      CLIB_PREFETCH (d + 2, 32, STORE);

      bi0 = buffers[0];
      bi1 = buffers[1];

      to_free[0] = fi0 = to_tx[0];
      to_tx[0] = bi0;
      to_free += fi0 != 0;

      to_free[0] = fi1 = to_tx[1];
      to_tx[1] = bi1;
      to_free += fi1 != 0;

      buffers += 2;
      n_left -= 2;
      to_tx += 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      is_eop0 = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;
      is_eop1 = (b1->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      len0 = b0->current_length;
      len1 = b1->current_length;

      ASSERT (ixge_tx_descriptor_matches_template (xm, d + 0));
      ASSERT (ixge_tx_descriptor_matches_template (xm, d + 1));

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;
      d[1].buffer_address = vlib_get_buffer_data_physical_address (vm, bi1) + b1->current_data;

      d[0].status[0] |=
	((is_eop0 << IXGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET)
	 | IXGE_TX_DESCRIPTOR_STATUS0_N_BYTES_THIS_BUFFER (len0));
      d[1].status[0] |=
	((is_eop1 << IXGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET)
	 | IXGE_TX_DESCRIPTOR_STATUS0_N_BYTES_THIS_BUFFER (len1));

      len_sop = (is_sop ? 0 : len_sop) + len0;
      d_sop[0].status[1] = IXGE_TX_DESCRIPTOR_STATUS1_N_BYTES_IN_PACKET (len_sop);
      d += 1;
      d_sop = is_sop ? d : d_sop;

      is_sop = is_eop0;

      len_sop = (is_sop ? 0 : len_sop) + len1;
      d_sop[0].status[1] = IXGE_TX_DESCRIPTOR_STATUS1_N_BYTES_IN_PACKET (len_sop);
      d += 1;
      d_sop = is_sop ? d : d_sop;

      is_sop = is_eop1;
    }

  while (n_left > 0)
    {
      vlib_buffer_t * b0;
      u32 bi0, fi0, len0;
      u8 is_eop0;

      bi0 = buffers[0];

      to_free[0] = fi0 = to_tx[0];
      to_tx[0] = bi0;
      to_free += fi0 != 0;

      buffers += 1;
      n_left -= 1;
      to_tx += 1;

      b0 = vlib_get_buffer (vm, bi0);

      is_eop0 = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      len0 = b0->current_length;

      ASSERT (ixge_tx_descriptor_matches_template (xm, d + 0));

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;

      d[0].status[0] |=
	((is_eop0 << IXGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET)
	 | IXGE_TX_DESCRIPTOR_STATUS0_N_BYTES_THIS_BUFFER (len0));

      len_sop = (is_sop ? 0 : len_sop) + len0;
      d_sop[0].status[1] = IXGE_TX_DESCRIPTOR_STATUS1_N_BYTES_IN_PACKET (len_sop);
      d += 1;
      d_sop = is_sop ? d : d_sop;

      is_sop = is_eop0;
    }

  _vec_len (xm->tx_buffers_pending_free) = to_free - xm->tx_buffers_pending_free;

  tx_state->is_start_of_packet = is_sop;
  tx_state->start_of_packet_descriptor = d_sop;
  tx_state->n_bytes_in_packet = len_sop;

  return n_descriptors;
}

static uword
ixge_interface_tx (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
  vlib_interface_output_runtime_t * rd = (void *) node->runtime_data;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, rd->dev_instance);
  ixge_dma_queue_t * dq;
  u32 * from, n_left_from, n_left_tx, n_descriptors_to_tx;
  u32 queue_index = 0;		/* fixme parameter */
  ixge_tx_state_t tx_state;

  tx_state.is_start_of_packet = 1;
  tx_state.start_of_packet_descriptor = 0;
  tx_state.n_bytes_in_packet = 0;
  
  from = vlib_frame_vector_args (f);
  n_left_from = f->n_vectors;

  dq = vec_elt_at_index (xd->dma_queues[VLIB_TX], queue_index);

  n_left_tx = dq->n_descriptors - dq->tx.n_descriptors_active;

  /* There might be room on the ring due to packets already transmitted. */
  {
    u32 hw_head_index = dq->tx.head_index_write_back[0];
    n_left_tx += ixge_ring_sub (dq, hw_head_index, dq->head_index);
    dq->head_index = hw_head_index;
  }

  n_descriptors_to_tx = clib_min (n_left_tx, n_left_from);

  _vec_len (xm->tx_buffers_pending_free) = 0;

  /* Process from tail to end of descriptor ring. */
  if (n_descriptors_to_tx > 0 && dq->tail_index < dq->n_descriptors)
    {
      u32 n = clib_min (dq->n_descriptors - dq->tail_index, n_descriptors_to_tx);
      n = ixge_tx_no_wrap (xm, xd, dq, from, dq->tail_index, n, &tx_state);
      from += n;
      n_left_from -= n;
      n_descriptors_to_tx -= n;
      dq->tail_index += n;
      ASSERT (dq->tail_index <= dq->n_descriptors);
      if (dq->tail_index == dq->n_descriptors)
	dq->tail_index = 0;
    }

  if (n_descriptors_to_tx > 0)
    {
      u32 n = ixge_tx_no_wrap (xm, xd, dq, from, 0, n_descriptors_to_tx, &tx_state);
      from += n;
      n_left_from -= n;
      ASSERT (n == n_descriptors_to_tx);
      dq->tail_index += n;
      ASSERT (dq->tail_index <= dq->n_descriptors);
      if (dq->tail_index == dq->n_descriptors)
	dq->tail_index = 0;
    }

  /* We should only get full packets. */
  ASSERT (tx_state.is_start_of_packet);

  /* Give new descriptors to hardware. */
  {
    ixge_dma_regs_t * dr = get_dma_regs (xd, VLIB_TX, queue_index);

    CLIB_MEMORY_BARRIER ();

    dr->tail_index = dq->tail_index;
  }

  /* Free any buffers that are done. */
  {
    u32 n = _vec_len (xm->tx_buffers_pending_free);
    if (n > 0)
      {
	vlib_buffer_free (vm, xm->tx_buffers_pending_free,
			  /* stride */ 1,
			  n,
			  /* follow_buffer_next */ 0);
	_vec_len (xm->tx_buffers_pending_free) = 0;
      }
  }

  /* Not enough room on ring: drop the buffers. */
  if (n_left_from > 0)
    {
      /* Back up to last start of packet and free from there. */
      ASSERT (0);
    }

  return f->n_vectors;
}

static uword
ixge_rx_queue_no_wrap (ixge_main_t * xm,
		       ixge_device_t * xd,
		       ixge_dma_queue_t * dq,
		       ixge_rx_state_t * rx_state,
		       u32 start_descriptor_index,
		       u32 n_descriptors)
{
  vlib_main_t * vm = xm->vlib_main;
  vlib_node_runtime_t * node = rx_state->node;
  ixge_descriptor_t * d;
  static ixge_descriptor_t * d_trace_save;
  static u32 * d_trace_buffers;
  u32 n_descriptors_left = n_descriptors;
  u32 * to_rx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 * to_add;
  u32 bi_sop = rx_state->saved_start_of_packet_buffer_index;
  u32 bi_last = rx_state->saved_last_buffer_index;
  u32 next_index_sop = rx_state->saved_start_of_packet_next_index;
  u32 is_sop = rx_state->is_start_of_packet;
  u32 next_index, n_left_to_next, * to_next;
  u32 n_packets = 0;
  u32 n_bytes = 0;
  u32 n_trace = vlib_get_trace_count (vm, node);
  vlib_buffer_t * b_last, b_dummy;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index];

  b_last = bi_last != ~0 ? vlib_get_buffer (vm, bi_last) : &b_dummy;
  next_index = rx_state->next_index;

  if (n_trace > 0)
    {
      u32 n = clib_min (n_trace, n_descriptors);
      if (d_trace_save)
	{
	  _vec_len (d_trace_save) = 0;
	  _vec_len (d_trace_buffers) = 0;
	}
      vec_add (d_trace_save, d, n);
      vec_add (d_trace_buffers, to_rx, n);
    }

  {
    uword l = vec_len (xm->rx_buffers_to_add);

    if (l < n_descriptors_left)
      {
	u32 n_to_alloc = 2 * dq->n_descriptors - l;
	u32 n_allocated;

	vec_validate (xm->rx_buffers_to_add, n_to_alloc + l - 1);

	_vec_len (xm->rx_buffers_to_add) = l;
	n_allocated = vlib_buffer_alloc_from_free_list
	  (vm, xm->rx_buffers_to_add + l, n_to_alloc,
	   xm->vlib_buffer_free_list_index);
	_vec_len (xm->rx_buffers_to_add) += n_allocated;
	ASSERT (vec_len (xm->rx_buffers_to_add) >= n_descriptors_left);
      }

    /* Add buffers from end of vector going backwards. */
    to_add = vec_end (xm->rx_buffers_to_add) - 1;
  }

  while (n_descriptors_left > 0)
    {
      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_descriptors_left >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * b0, * b1;
	  u32 bi0, fi0, len0, l3_offset0, s20, s00;
	  u32 bi1, fi1, len1, l3_offset1, s21, s01;
	  u8 is_eop0, error0, next0;
	  u8 is_eop1, error1, next1;

	  vlib_prefetch_buffer_with_index (vm, to_rx[2], STORE);
	  vlib_prefetch_buffer_with_index (vm, to_rx[3], STORE);
	  CLIB_PREFETCH (d + 2, 32, LOAD);

	  s00 = d[0].rx_from_hw.status[0];
	  s01 = d[1].rx_from_hw.status[0];

	  s20 = d[0].rx_from_hw.status[2];
	  s21 = d[1].rx_from_hw.status[2];

	  if (! ((s20 | s21) & IXGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE))
	    goto found_hw_owned_descriptor_x2;

	  bi0 = to_rx[0];
	  bi1 = to_rx[1];

	  ASSERT (to_add - 1 >= xm->rx_buffers_to_add);
	  fi0 = to_add[0];
	  fi1 = to_add[-1];

	  to_rx[0] = fi0;
	  to_rx[1] = fi1;
	  to_rx += 2;
	  to_add -= 2;

	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, bi0));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, bi1));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, fi0));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, fi1));

	  b0 = vlib_get_buffer (vm, bi0);
	  b1 = vlib_get_buffer (vm, bi1);

	  is_eop0 = (s20 & IXGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;
	  is_eop1 = (s21 & IXGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;

	  ixge_rx_next_and_error_from_status_x2 (s00, s20, s01, s21,
						 &next0, &error0,
						 &next1, &error1);

	  next0 = is_sop ? next0 : next_index_sop;
	  next1 = is_eop0 ? next1 : next0;
	  next_index_sop = next1;

	  b0->flags |= (!is_eop0 << VLIB_BUFFER_LOG2_NEXT_PRESENT);
	  b1->flags |= (!is_eop1 << VLIB_BUFFER_LOG2_NEXT_PRESENT);

	  b0->sw_if_index[VLIB_RX] = xd->vlib_sw_if_index;
	  b1->sw_if_index[VLIB_RX] = xd->vlib_sw_if_index;

	  b0->error = node->errors[error0];
	  b1->error = node->errors[error1];

	  len0 = d[0].rx_from_hw.n_packet_bytes_this_descriptor;
	  len1 = d[1].rx_from_hw.n_packet_bytes_this_descriptor;
	  n_bytes += len0 + len1;
	  n_packets += is_eop0 + is_eop1;

	  /* Give new buffers to hardware. */
	  d[0].rx_to_hw.tail_address = vlib_get_buffer_data_physical_address (vm, fi0);
	  d[1].rx_to_hw.tail_address = vlib_get_buffer_data_physical_address (vm, fi1);
	  d[0].rx_to_hw.head_address = 0; /* must set low bit to zero */
	  d[1].rx_to_hw.head_address = 0; /* must set low bit to zero */
	  d += 2;
	  n_descriptors_left -= 2;

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = (is_sop && next0 != IXGE_RX_NEXT_ETHERNET_INPUT
			? IXGE_RX_DESCRIPTOR_STATUS0_L3_OFFSET (s00)
			: 0);
	  l3_offset1 = (is_eop0 && next1 != IXGE_RX_NEXT_ETHERNET_INPUT
			? IXGE_RX_DESCRIPTOR_STATUS0_L3_OFFSET (s01)
			: 0);

	  b0->current_length = len0 + l3_offset0;
	  b1->current_length = len1 + l3_offset1;
	  b0->current_data = l3_offset0;
	  b1->current_data = l3_offset1;

	  b_last->next_buffer = is_sop ? 0 : bi0;
	  b0->next_buffer = is_eop0 ? 0 : bi1;
	  b_last = b1;

	  if (PREDICT_TRUE (next0 == next_index && next1 == next_index))
	    {
	      bi_sop = is_sop ? bi0 : bi_sop;
	      to_next[0] = bi_sop;
	      to_next += is_eop0;
	      n_left_to_next -= is_eop0;

	      bi_sop = is_eop0 ? bi1 : bi_sop;
	      to_next[0] = bi_sop;
	      to_next += is_eop1;
	      n_left_to_next -= is_eop1;

	      is_sop = is_eop1;
	    }
	  else
	    {
	      bi_sop = is_sop ? bi0 : bi_sop;
	      if (next0 != next_index && is_eop0)
		vlib_set_next_frame_buffer (vm, node, next0, bi_sop);
	      bi_sop = is_eop0 ? bi1 : bi_sop;
	      if (next1 != next_index && is_eop1)
		vlib_set_next_frame_buffer (vm, node, next1, bi_sop);
	      is_sop = is_eop1;

	      if (next0 == next1)
		{
		  vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		  next_index = next0;
		  vlib_get_next_frame (vm, node, next_index,
				       to_next, n_left_to_next);
		}
	    }
	}

      /* Bail out of dual loop and proceed with single loop. */
    found_hw_owned_descriptor_x2:

      while (n_descriptors_left > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * b0;
	  u32 bi0, fi0, len0, l3_offset0, s20, s00;
	  u8 is_eop0, error0, next0;

	  s00 = d[0].rx_from_hw.status[0];
	  s20 = d[0].rx_from_hw.status[2];
	  if (! (s20 & IXGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE))
	    goto found_hw_owned_descriptor_x1;

	  bi0 = to_rx[0];
	  ASSERT (to_add >= xm->rx_buffers_to_add);
	  fi0 = to_add[0];

	  to_rx[0] = fi0;
	  to_rx += 1;
	  to_add -= 1;

	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, bi0));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, fi0));

	  b0 = vlib_get_buffer (vm, bi0);

	  is_eop0 = (s20 & IXGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;
	  ixge_rx_next_and_error_from_status_x1 (s00, s20, &next0, &error0);

	  next0 = is_sop ? next0 : next_index_sop;
	  next_index_sop = next0;

	  b0->flags |= (!is_eop0 << VLIB_BUFFER_LOG2_NEXT_PRESENT);

	  b0->sw_if_index[VLIB_RX] = xd->vlib_sw_if_index;

	  b0->error = node->errors[error0];

	  len0 = d[0].rx_from_hw.n_packet_bytes_this_descriptor;
	  n_bytes += len0;
	  n_packets += is_eop0;

	  /* Give new buffer to hardware. */
	  d[0].rx_to_hw.tail_address = vlib_get_buffer_data_physical_address (vm, fi0);
	  d[0].rx_to_hw.head_address = 0; /* must set low bit to zero */
	  d += 1;
	  n_descriptors_left -= 1;

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = (is_sop && next0 != IXGE_RX_NEXT_ETHERNET_INPUT
			? IXGE_RX_DESCRIPTOR_STATUS0_L3_OFFSET (s00)
			: 0);
	  b0->current_length = len0 + l3_offset0;
	  b0->current_data = l3_offset0;

	  b_last->next_buffer = is_sop ? 0 : bi0;
	  b_last = b0;

	  if (PREDICT_TRUE (next0 == next_index))
	    {
	      bi_sop = is_sop ? bi0 : bi_sop;
	      to_next[0] = bi_sop;
	      to_next += is_eop0;
	      n_left_to_next -= is_eop0;
	      is_sop = is_eop0;
	    }
	  else
	    {
	      bi_sop = is_sop ? bi0 : bi_sop;
	      if (next0 != next_index && is_eop0)
		vlib_set_next_frame_buffer (vm, node, next0, bi_sop);
	      is_sop = is_eop0;

	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index,
				   to_next, n_left_to_next);
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

 found_hw_owned_descriptor_x1:
  if (n_descriptors_left > 0)
    vlib_put_next_frame (vm, node, next_index, n_left_to_next);

  _vec_len (xm->rx_buffers_to_add) = (to_add + 1) - xm->rx_buffers_to_add;

  {
    u32 n_done = n_descriptors - n_descriptors_left;

    if (n_trace > 0 && n_done > 0)
      {
	u32 n = clib_min (n_trace, n_done);
	ixge_rx_trace (xm, xd, dq, rx_state,
		       d_trace_save,
		       d_trace_buffers,
		       &dq->descriptors[start_descriptor_index],
		       n);
	vlib_set_trace_count (vm, node, n_trace - n);
      }
    if (d_trace_save)
      {
	_vec_len (d_trace_save) = 0;
	_vec_len (d_trace_buffers) = 0;
      }

    rx_state->n_descriptors_done_this_call = n_done;
    rx_state->n_descriptors_done_total += n_done;
    rx_state->is_start_of_packet = is_sop;
    rx_state->saved_start_of_packet_buffer_index = bi_sop;
    rx_state->saved_last_buffer_index = bi_last;
    rx_state->saved_start_of_packet_next_index = next_index_sop;
    rx_state->next_index = next_index;
    rx_state->n_bytes += n_bytes;

    return n_packets;
  }
}

static uword
ixge_rx_queue (ixge_main_t * xm,
	       ixge_device_t * xd,
	       ixge_rx_state_t * rx_state,
	       u32 queue_index)
{
  ixge_dma_queue_t * dq = vec_elt_at_index (xd->dma_queues[VLIB_RX], queue_index);
  ixge_dma_regs_t * dr = get_dma_regs (xd, VLIB_RX, dq->queue_index);
  uword n_packets = 0;
  u32 hw_head_index, sw_head_index;

  rx_state->is_start_of_packet = 1;
  rx_state->saved_start_of_packet_buffer_index = ~0;
  rx_state->saved_last_buffer_index = ~0;
  rx_state->n_descriptors_done_total = 0;
  rx_state->n_bytes = 0;

  /* Fetch head from hardware and compare to where we think we are. */
  hw_head_index = dr->head_index;
  sw_head_index = dq->head_index;
  if (hw_head_index == sw_head_index)
    goto done;

  if (hw_head_index < sw_head_index)
    {
      u32 n_tried = dq->n_descriptors - sw_head_index;
      n_packets += ixge_rx_queue_no_wrap (xm, xd, dq, rx_state, sw_head_index, n_tried);
      sw_head_index = ixge_ring_add (dq, sw_head_index, rx_state->n_descriptors_done_this_call);
      if (rx_state->n_descriptors_done_this_call != n_tried)
	goto done;
    }
  if (hw_head_index >= sw_head_index)
    {
      u32 n_tried = hw_head_index - sw_head_index;
      n_packets += ixge_rx_queue_no_wrap (xm, xd, dq, rx_state, sw_head_index, n_tried);
      sw_head_index = ixge_ring_add (dq, sw_head_index, rx_state->n_descriptors_done_this_call);
    }

 done:
  dq->head_index = sw_head_index;
  dq->tail_index = ixge_ring_add (dq, dq->tail_index, rx_state->n_descriptors_done_total);

  /* Give head/tail back to hardware. */
  CLIB_MEMORY_BARRIER ();

  dr->head_index = dq->head_index;
  dr->tail_index = dq->tail_index;

  vlib_increment_combined_counter (xm->vlib_main->interface_main.combined_sw_if_counters
				   + VLIB_INTERFACE_COUNTER_RX,
				   xd->vlib_sw_if_index,
				   n_packets,
				   rx_state->n_bytes);

  return n_packets;
}

static void ixge_interrupt (ixge_main_t * xm, ixge_device_t * xd, u32 i)
{
  vlib_main_t * vm = xm->vlib_main;
  ixge_regs_t * r = xd->regs;

  {
    ELOG_TYPE_DECLARE (e) = {
      .function = (char *) __FUNCTION__,
      .format = "ixge %d, %s",
      .format_args = "i1t1",
      .n_enum_strings = 16,
      .enum_strings = {
	"flow director",
	"rx miss",
	"pci exception",
	"mailbox",
	"link status change",
	"linksec key exchange",
	"manageability event",
	"reserved23",
	"sdp0",
	"sdp1",
	"sdp2",
	"sdp3",
	"ecc",
	"descriptor handler error",
	"tcp timer",
	"other",
      },
    };
    struct { u8 instance; u8 index; } * ed;
    ed = ELOG_DATA (&vm->elog_main, e);
    ed->instance = xd->device_index;
    ed->index = i - 16;
  }

  if (i == 20)
    {
      uword was_up = vlib_hw_interface_is_link_up (vm, xd->vlib_hw_if_index);
      u32 v = r->xge_mac.link_status;
      uword is_up = (v & (1 << 30)) != 0;
      if (was_up != is_up)
	{
	  xd->link_status_at_last_link_change = v;
	  vlib_hw_interface_set_flags (vm, xd->vlib_hw_if_index,
				       is_up ? VLIB_HW_INTERFACE_FLAG_LINK_UP : 0);
	}
    }

  {
    u32 s = r->pcie.pcie_interrupt_status;
    r->pcie.pcie_interrupt_status = s;
    if (s != 0)
      clib_warning ("0x%x", s);
  }
}

static uword
ixge_device_input (ixge_main_t * xm,
		   ixge_device_t * xd,
		   ixge_rx_state_t * rx_state)
{
  ixge_regs_t * r = xd->regs;
  u32 i, s, t;
  uword n_rx_packets = 0;

  s = r->interrupt.status_write_1_to_clear;
  t = s & xd->interrupt_status_no_auto_clear_mask;
  if (PREDICT_FALSE (t != 0))
    r->interrupt.status_write_1_to_clear = t;

  foreach_set_bit (i, s, ({
    if (i < 16)
      n_rx_packets += ixge_rx_queue (xm, xd, rx_state, i);
    else
      ixge_interrupt (xm, xd, i);
  }));

  return n_rx_packets;
}

static uword
ixge_input (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd;
  ixge_rx_state_t _rx_state, * rx_state = &_rx_state;
  uword n_rx_packets = 0;

  rx_state->node = node;
  rx_state->next_index = node->cached_next_index;

  /* Fetch interrupting device. */
  if (node->state == VLIB_NODE_STATE_INTERRUPT)
    {    
      uword i;
      foreach_set_bit (i, node->runtime_data[0], ({
	xd = vec_elt_at_index (xm->devices, i);
	n_rx_packets += ixge_device_input (xm, xd, rx_state);

	/* Re-enable interrupts since we're in interrupt mode. */
	xd->regs->interrupt.enable_write_1_to_set = ~0;
      }));
    }
  else
    {
      /* Poll all devices for input/interrupts. */
      vec_foreach (xd, xm->devices)
	n_rx_packets += ixge_device_input (xm, xd, rx_state);
    }

  return n_rx_packets;
}

static char * ixge_rx_error_strings[] = {
#define _(n,s) s,
    foreach_ixge_rx_error
#undef _
};

static VLIB_REGISTER_NODE (ixge_input_node) = {
  .function = ixge_input,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "ixge-input",

  /* Will be enabled if/when hardware is detected. */
  .state = VLIB_NODE_STATE_DISABLED,

  .format_buffer = format_ethernet_header_with_length,
  .format_trace = format_ixge_dma_rx_trace,

  .n_errors = IXGE_RX_N_ERROR,
  .error_strings = ixge_rx_error_strings,

  .n_next_nodes = IXGE_RX_N_NEXT,
  .next_nodes = {
    [IXGE_RX_NEXT_DROP] = "error-drop",
    [IXGE_RX_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [IXGE_RX_NEXT_IP4_INPUT] = "ip4-input-no-checksum",
    [IXGE_RX_NEXT_IP6_INPUT] = "ip6-input",
  },
};

static u8 * format_ixge_device_name (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, i);
  return format (s, "TenGigabitEthernet%U",
		 format_os_pci_handle, xd->pci_device.os_handle);
}

#define IXGE_COUNTER_IS_64_BIT (1 << 0)
#define IXGE_COUNTER_NOT_CLEAR_ON_READ (1 << 1)

static u8 ixge_counter_flags[] = {
#define _(a,f) 0,
#define _64(a,f) IXGE_COUNTER_IS_64_BIT,
  foreach_ixge_counter
#undef _
#undef _64
};

static void ixge_update_counters (ixge_device_t * xd)
{
  /* Byte offset for counter registers. */
  static u32 reg_offsets[] = {
#define _(a,f) (a) / sizeof (u32),
#define _64(a,f) _(a,f)
    foreach_ixge_counter
#undef _
#undef _64
  };
  volatile u32 * r = (volatile u32 *) xd->regs;
  int i;

  for (i = 0; i < ARRAY_LEN (xd->counters); i++)
    {
      u32 o = reg_offsets[i];
      xd->counters[i] += r[o];
      if (ixge_counter_flags[i] & IXGE_COUNTER_NOT_CLEAR_ON_READ)
	r[o] = 0;
      if (ixge_counter_flags[i] & IXGE_COUNTER_IS_64_BIT)
	xd->counters[i] += (u64) r[o+1] << (u64) 32;
    }
}

static u8 * format_ixge_device_id (u8 * s, va_list * args)
{
  u32 device_id = va_arg (*args, u32);
  char * t = 0;
  switch (device_id)
    {
#define _(f,n) case n: t = #f; break;
      foreach_ixge_pci_device_id;
#undef _
    default:
      t = 0;
      break;
    }
  if (t == 0)
    s = format (s, "unknown 0x%x", device_id);
  else
    s = format (s, "%s", t);
  return s;
}

static u8 * format_ixge_link_status (u8 * s, va_list * args)
{
  ixge_device_t * xd = va_arg (*args, ixge_device_t *);
  u32 v = xd->link_status_at_last_link_change;

  s = format (s, "%s", (v & (1 << 30)) ? "up" : "down");

  {
    char * modes[] = {
      "1g", "10g parallel", "10g serial", "autoneg",
    };
    char * speeds[] = {
      "unknown", "100m", "1g", "10g",
    };
    s = format (s, ", mode %s, speed %s",
		modes[(v >> 26) & 3],
		speeds[(v >> 28) & 3]);
  }

  return s;
}

static u8 * format_ixge_device (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, dev_instance);
  ixge_phy_t * phy = xd->phys + xd->phy_index;
  uword indent = format_get_indent (s);

  ixge_update_counters (xd);

  s = format (s, "Intel 8259X: id %U\n%Ulink %U",
	      format_ixge_device_id, xd->device_id,
	      format_white_space, indent + 2,
	      format_ixge_link_status, xd);

  {
    pcie_config_regs_t * r = pci_config_find_capability (&xd->pci_device.config0, PCI_CAP_ID_PCIE);

    s = format (s, "\n%U", format_white_space, indent + 2);
    if (r)
      s = format (s, "PCIE %.1fGb/s width x%d",
		  (2.5 * (r->link_status & 0xf)),
		  (r->link_status >> 4) & 0x3f);
    else
      s = format (s, "PCIE unknown speed and width");
  }

  s = format (s, "\n%U", format_white_space, indent + 2);
  if (phy->mdio_address != ~0)
    s = format (s, "PHY address %d, id 0x%x", phy->mdio_address, phy->id);
  else if (xd->sfp_eeprom.id == SFP_ID_sfp)
    s = format (s, "SFP %U", format_sfp_eeprom, &xd->sfp_eeprom);
  else
    s = format (s, "PHY not found");

  {
    u32 i;
    u64 v;
    static char * names[] = {
#define _(a,f) #f,
#define _64(a,f) _(a,f)
    foreach_ixge_counter
#undef _
#undef _64
    };

    for (i = 0; i < ARRAY_LEN (names); i++)
      {
	v = xd->counters[i] - xd->counters_last_clear[i];
	if (v != 0)
	  s = format (s, "\n%U%-40U%16Ld",
		      format_white_space, indent + 2,
		      format_c_identifier, names[i],
		      v);
      }
  }

  return s;
}

static void ixge_clear_hw_interface_counters (u32 instance)
{
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, instance);
  ixge_update_counters (xd);
  memcpy (xd->counters_last_clear, xd->counters, sizeof (xd->counters));
}

static VLIB_DEVICE_CLASS (ixge_device_class) = {
    .name = "ixge",
    .tx_function = ixge_interface_tx,
    .format_device_name = format_ixge_device_name,
    .format_device = format_ixge_device,
    .clear_counters = ixge_clear_hw_interface_counters,
};

static clib_error_t *
ixge_dma_init (ixge_device_t * xd, vlib_rx_or_tx_t rt, u32 queue_index)
{
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
  ixge_dma_queue_t * dq;
  clib_error_t * error = 0;

  vec_validate (xd->dma_queues[rt], queue_index);
  dq = vec_elt_at_index (xd->dma_queues[rt], queue_index);

  if (! xm->n_descriptors_per_cache_line)
    xm->n_descriptors_per_cache_line = CLIB_CACHE_LINE_BYTES / sizeof (dq->descriptors[0]);

  if (! xm->n_bytes_in_rx_buffer)
    xm->n_bytes_in_rx_buffer = 1024;
  xm->n_bytes_in_rx_buffer = round_pow2 (xm->n_bytes_in_rx_buffer, 1024);
  if (! xm->vlib_buffer_free_list_index)
    {
      xm->vlib_buffer_free_list_index = vlib_buffer_get_or_create_free_list (vm, xm->n_bytes_in_rx_buffer);
      ASSERT (xm->vlib_buffer_free_list_index != 0);
    }

  if (! xm->n_descriptors[rt])
    xm->n_descriptors[rt] = 3 * VLIB_FRAME_SIZE / 2;

  dq->queue_index = queue_index;
  dq->n_descriptors = round_pow2 (xm->n_descriptors[rt], xm->n_descriptors_per_cache_line);
  dq->head_index = dq->tail_index = 0;

  dq->descriptors = vlib_physmem_alloc_aligned (vm, &error,
						dq->n_descriptors * sizeof (dq->descriptors[0]),
						CLIB_CACHE_LINE_BYTES);
  if (error)
    return error;

  memset (dq->descriptors, 0, dq->n_descriptors * sizeof (dq->descriptors[0]));
  vec_resize (dq->descriptor_buffer_indices, dq->n_descriptors);

  if (rt == VLIB_RX)
    {
      u32 n_alloc, i;

      n_alloc = vlib_buffer_alloc_from_free_list
	(vm, dq->descriptor_buffer_indices, vec_len (dq->descriptor_buffer_indices),
	 xm->vlib_buffer_free_list_index);
      ASSERT (n_alloc == vec_len (dq->descriptor_buffer_indices));
      for (i = 0; i < n_alloc; i++)
	{
	  vlib_buffer_t * b = vlib_get_buffer (vm, dq->descriptor_buffer_indices[i]);
	  dq->descriptors[i].rx_to_hw.tail_address = vlib_physmem_virtual_to_physical (vm, b->data);
	}
    }
  else
    {
      u32 i;

      dq->tx.head_index_write_back = vlib_physmem_alloc (vm, &error, CLIB_CACHE_LINE_BYTES);

      for (i = 0; i < dq->n_descriptors; i++)
	dq->descriptors[i].tx = xm->tx_descriptor_template;

      vec_validate (xm->tx_buffers_pending_free, dq->n_descriptors - 1);
    }

  {
    ixge_dma_regs_t * dr = get_dma_regs (xd, rt, queue_index);
    uword a;

    a = vlib_physmem_virtual_to_physical (vm, dq->descriptors);
    dr->descriptor_address[0] = (u64) a;
    dr->descriptor_address[1] = (u64) a >> (u64) 32;
    dr->n_descriptor_bytes = dq->n_descriptors * sizeof (dq->descriptors[0]);
    dq->head_index = dq->tail_index = 0;

    if (rt == VLIB_RX)
      {
	ASSERT ((xm->n_bytes_in_rx_buffer / 1024) < 32);
	dr->rx_split_control =
	  (/* buffer size */ ((xm->n_bytes_in_rx_buffer / 1024) << 0)
	   | (/* lo free descriptor threshold (units of 64 descriptors) */
	      (1 << 22))
	   | (/* descriptor type: advanced one buffer */
	      (1 << 25))
	   | (/* drop if no descriptors available */
	      (1 << 28)));

	/* Give hardware all but last cache line of descriptors. */
	dq->tail_index = dq->n_descriptors - xm->n_descriptors_per_cache_line;
      }
    else
      {
	/* Make sure its initialized before hardware can get to it. */
	dq->tx.head_index_write_back[0] = dq->head_index;

	a = vlib_physmem_virtual_to_physical (vm, dq->tx.head_index_write_back);
	dr->tx.head_index_write_back_address[0] = /* enable bit */ 1 | a;
	dr->tx.head_index_write_back_address[1] = a >> 32;
      }

    /* DMA on 82599 does not work with [13] rx data write relaxed ordering
       and [12] undocumented set. */
    if (rt == VLIB_RX)
      dr->dca_control &= ~((1 << 13) | (1 << 12));

    CLIB_MEMORY_BARRIER ();

    if (rt == VLIB_RX)
      xd->regs->rx_enable |= 1;
    else
      xd->regs->tx_dma_control |= (1 << 0);

    /* Enable this queue and wait for hardware to initialize before adding to tail. */
    dr->control |= 1 << 25;
    while (! (dr->control & (1 << 25)))
      ;

    /* Set head/tail indices and enable DMA. */
    dr->head_index = dq->head_index;
    dr->tail_index = dq->tail_index;
  }

  return error;
}

static void ixge_device_init (ixge_main_t * xm)
{
  vlib_main_t * vm = xm->vlib_main;
  ixge_device_t * xd;
    
  /* Reset chip(s). */
  vec_foreach (xd, xm->devices)
    {
      ixge_regs_t * r = xd->regs;
      const u32 reset_bit = 1 << 26;

      r->control |= reset_bit;

      /* No need to suspend.  Timed to take ~1e-6 secs */
      while (r->control & reset_bit)
	;

      /* Software loaded. */
      r->extended_control |= (1 << 28);

      ixge_phy_init (xd);

      /* Register ethernet interface. */
      {
	u8 addr8[6];
	u32 i, addr32[2];
	clib_error_t * error;

	addr32[0] = r->rx_ethernet_address0[0][0];
	addr32[1] = r->rx_ethernet_address0[0][1];
	for (i = 0; i < 6; i++)
	  addr8[i] = addr32[i / 4] >> ((i % 4) * 8);

	error = ethernet_register_interface
	  (vm,
	   ixge_device_class.index,
	   xd->device_index,
	   /* ethernet address */ addr8,
	   /* phy */ 0,
	   &xd->vlib_hw_if_index);
	if (error)
	  clib_error_report (error);
      }

      {
	vlib_sw_interface_t * sw = vlib_get_hw_sw_interface (vm, xd->vlib_hw_if_index);
	xd->vlib_sw_if_index = sw->sw_if_index;
      }

      ixge_dma_init (xd, VLIB_RX, /* queue_index */ 0);
      ixge_dma_init (xd, VLIB_TX, /* queue_index */ 0);

      /* RX queue gets mapped to interrupt bit 0.
	 We don't use TX interrupts. */
      r->interrupt.queue_mapping[0] = ((1 << 7) | 0) << 0;

      /* No use in getting too many interrupts.
	 Limit them to one every 3/4 ring size at line rate
	 min sized packets. */
      {
	f64 line_rate_max_pps = 10e9 / (8 * (64 + /* interframe padding */ 20));
	ixge_throttle_queue_interrupt (r, 0, .75 * xm->n_descriptors[VLIB_RX] / line_rate_max_pps);
      }

      /* Accept all broadcast packets.  Multicasts must be explicitly
	 added to dst_ethernet_address register array. */
      r->filter_control |= (1 << 10);

      /* Enable frames up to size in mac frame size register. */
      r->xge_mac.control |= 1 << 2;

      if (0)
	/* sets mac loopback */
	xd->regs->xge_mac.control |= 1<< 15;

      /* Kernel should have already set pci master for us.
	 If its not set you don't get much DMA done. */
      if (0)
	{
	  u16 tmp[1];
	  clib_error_t * e;
	  e = os_read_pci_config_u16 (xd->pci_device.os_handle, 0x4, &tmp[0]);
	  if (e) clib_error_report (e);
	  ASSERT (tmp[0] & (1 << 2));
	  tmp[0] |= 1 << 2;
	  e = os_write_pci_config_u16 (xd->pci_device.os_handle, 0x4, &tmp[0]);
	  if (e) clib_error_report (e);
	}

      /* Enable all interrupts. */
#define IXGE_INTERRUPT_DISABLE 0
      if (! IXGE_INTERRUPT_DISABLE)
	r->interrupt.enable_write_1_to_set = ~0;

      /* Enable auto-clear for all RX/TX queues. */
      {
	u32 m = 0xffff;
	xd->interrupt_status_no_auto_clear_mask = ~m;
	r->interrupt.status_auto_clear_enable = m;
      }
    }
}

static uword
ixge_process (vlib_main_t * vm,
	      vlib_node_runtime_t * rt,
	      vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
  uword event_type, * event_data = 0;
    
  ixge_device_init (xm);

  while (1)
    {
      /* 36 bit stat counters could overflow in ~50 secs.
	 We poll every 30 secs to be conservative. */
	vlib_process_wait_for_event_or_clock (vm, 30. /* seconds */);

	event_type = vlib_process_get_events (vm, &event_data);

	switch (event_type) {
	case ~0:
	    /* No events found: timer expired. */
	    break;

	default:
	    ASSERT (0);
	}

	if (event_data)
	    _vec_len (event_data) = 0;

	/* Query stats every 30 secs. */
	{
	  f64 now = vlib_time_now (vm);
	  if (now - xm->time_last_stats_update > 30)
	    {
	      ixge_device_t * xd;
	      xm->time_last_stats_update = now;
	      vec_foreach (xd, xm->devices)
		ixge_update_counters (xd);
	    }
	}
    }
	    
  return 0;
}

static vlib_node_registration_t ixge_process_node = {
    .function = ixge_process,
    .type = VLIB_NODE_TYPE_PROCESS,
    .name = "ixge-process",
};

clib_error_t * ixge_init (vlib_main_t * vm)
{
  ixge_main_t * xm = &ixge_main;
  clib_error_t * error;

  xm->vlib_main = vm;
  memset (&xm->tx_descriptor_template, 0, sizeof (xm->tx_descriptor_template));
  memset (&xm->tx_descriptor_template_mask, 0, sizeof (xm->tx_descriptor_template_mask));
  xm->tx_descriptor_template.status[0] =
    (IXGE_TX_DESCRIPTOR_STATUS0_ADVANCED
     | IXGE_TX_DESCRIPTOR_STATUS0_IS_ADVANCED
     | IXGE_TX_DESCRIPTOR_STATUS0_INSERT_FCS);
  xm->tx_descriptor_template_mask.status[0] = 0xffff0000;
  xm->tx_descriptor_template_mask.status[1] = 0x00003fff;

  xm->tx_descriptor_template_mask.status[0] &=
    ~(IXGE_TX_DESCRIPTOR_STATUS0_IS_END_OF_PACKET);

  error = vlib_call_init_function (vm, pci_bus_init);

  return error;
}

VLIB_INIT_FUNCTION (ixge_init);

static clib_error_t *
ixge_pci_init (vlib_main_t * vm, pci_device_t * dev)
{
  ixge_main_t * xm = &ixge_main;
  clib_error_t * error;
  void * r;
  ixge_device_t * xd;
  
  /* Device found: make sure we have dma memory. */
  error = unix_physmem_init (vm, /* physical_memory_required */ 1);
  if (error)
    return error;

  error = os_map_pci_resource (dev->os_handle, 0, &r);
  if (error)
    return error;

  vec_add2 (xm->devices, xd, 1);
  xd->pci_device = dev[0];
  xd->device_id = xd->pci_device.config0.header.device_id;
  xd->regs = r;
  xd->device_index = xd - xm->devices;
  xd->pci_function = dev->bus_address.slot_function & 1;

  /* Chip found so enable node. */
  {
    linux_pci_device_t * lp = pci_dev_for_linux (dev);

    vlib_node_set_state (vm, ixge_input_node.index,
			 (IXGE_INTERRUPT_DISABLE
			  ? VLIB_NODE_STATE_POLLING
			  : VLIB_NODE_STATE_INTERRUPT));
    lp->device_input_node_index = ixge_input_node.index;
    lp->device_index = xd->device_index;
  }

  if (vec_len (xm->devices) == 1)
    {
      vlib_register_node (vm, &ixge_process_node);
      xm->process_node_index = ixge_process_node.index;
    }

  os_add_pci_disable_interrupts_reg
    (dev->os_handle,
     /* resource */ 0,
     STRUCT_OFFSET_OF (ixge_regs_t, interrupt.enable_write_1_to_clear),
     /* value to write */ ~0);

  return 0;
}

static PCI_REGISTER_DEVICE (ixge_pci_device_registration) = {
  .init_function = ixge_pci_init,
  .supported_devices = {
#define _(t,i) { .vendor_id = PCI_VENDOR_ID_INTEL, .device_id = i, },
    foreach_ixge_pci_device_id
#undef _
    { 0 },
  },
};
