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

static void ixge_phy_init (ixge_device_t * xd)
{
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
  ixge_phy_t * phy = xd->phys + xd->phy_index;

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
      {
	i2c_bus_t * ib = &xd->i2c_bus;
	u8 start_address[1];
	u32 timed_out;

	ib->private = xd->index;
	ib->put_bits = ixge_i2c_put_bits;
	ib->get_bits = ixge_i2c_get_bits;
	i2c_init (ib);

	start_address[0] = 0;
	timed_out = i2c_write_read (ib, 0xa0,
				    &start_address, 1,
				    &xd->sfp_eeprom, 128);
	if (timed_out || ! sfp_eeprom_is_valid (&xd->sfp_eeprom))
	  xd->sfp_eeprom.id = SFP_ID_unknown;

	phy->mdio_address = ~0;
	return;
      }
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
    ed->instance = xd->index;
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

always_inline uword
ixge_ring_diff (ixge_dma_queue_t * q, u32 i0, u32 i1)
{
  i32 d = i1 - i0;
  ASSERT (i0 < q->n_descriptors);
  ASSERT (i1 < q->n_descriptors);
  return d < 0 ? -d : d;
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

static uword
ixge_tx_no_wrap (ixge_main_t * xm,
		 ixge_device_t * xd,
		 ixge_dma_queue_t * dq,
		 u32 * buffers,
		 u32 start_descriptor_index,
		 u32 n_descriptors,
		 u32 * last_is_sop)
{
  vlib_main_t * vm = xm->vlib_main;
  ixge_tx_descriptor_t * d;
  u32 n_left = n_descriptors;
  u32 * to_free = vec_end (xm->tx_buffers_pending_free);
  u32 * to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 is_sop = *last_is_sop;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index].tx;
  is_sop = 0;

  while (n_left > 0)
    {
      u32 bi0, fi0, len0;
      vlib_buffer_t * b0;
      u8 is_eop0;

      bi0 = buffers[0];

      to_free[0] = fi0 = to_tx[0];
      to_tx[0] = bi0;

      buffers += 1;
      n_left -= 1;
      to_tx += 1;
      to_free += fi0 != 0;

      b0 = vlib_get_buffer (vm, bi0);

      is_eop0 = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;
      len0 = vlib_buffer_length_in_chain (vm, b0);
      len0 = is_sop ? len0 : 0;

      ASSERT (ixge_tx_descriptor_matches_template (xm, d));
      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;
      d[0].status[0] |=
	((is_eop0 << IXGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET)
	 | IXGE_TX_DESCRIPTOR_STATUS0_N_BYTES_THIS_BUFFER (b0->current_length));
      ASSERT (d[0].status[1] == 0);
      d[0].status[1] = IXGE_TX_DESCRIPTOR_STATUS1_N_BYTES_IN_PACKET (len0);

      is_sop = is_eop0;
    }

  _vec_len (xm->tx_buffers_pending_free) = to_free - xm->tx_buffers_pending_free;
  *last_is_sop = is_sop;

  return 0;
}

static uword
ixge_interface_tx (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
  vlib_interface_output_runtime_t * rd = (void *) node->runtime_data;
  vlib_hw_interface_t * hw_if = vlib_get_hw_interface (vm, rd->hw_if_index);
  ixge_device_t * xd = vec_elt_at_index (xm->devices, hw_if->dev_instance);
  ixge_dma_queue_t * dq;
  u32 * from, n_left_from, n_left_tx, n_descriptors_to_tx;
  u32 queue_index = 0;		/* fixme parameter */
  u32 is_sop = 1;
  
  from = vlib_frame_vector_args (f);
  n_left_from = f->n_vectors;

  dq = vec_elt_at_index (xd->dma_queues[VLIB_TX], queue_index);

  n_left_tx = dq->n_descriptors - dq->tx.n_descriptors_active;

  /* There might be room on the ring due to packets already transmitted. */
  {
    u32 hw_head_index = dq->tx.head_index_write_back[0];
    n_left_tx += ixge_ring_diff (dq, hw_head_index, dq->head_index);
    dq->head_index = hw_head_index;
  }

  n_descriptors_to_tx = clib_min (n_left_tx, n_left_from);

  _vec_len (xm->tx_buffers_pending_free) = 0;

  /* Process from tail to end of descriptor ring. */
  if (n_descriptors_to_tx > 0 && dq->tail_index < dq->n_descriptors)
    {
      u32 n = clib_min (dq->n_descriptors - dq->tail_index, n_descriptors_to_tx);
      n = ixge_tx_no_wrap (xm, xd, dq, from, dq->tail_index, n, &is_sop);
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
      u32 n = ixge_tx_no_wrap (xm, xd, dq, from, 0, n_descriptors_to_tx, &is_sop);
      from += n;
      n_left_from -= n;
      ASSERT (n == n_descriptors_to_tx);
      dq->tail_index += n;
      ASSERT (dq->tail_index <= dq->n_descriptors);
      if (dq->tail_index == dq->n_descriptors)
	dq->tail_index = 0;
    }

  /* Give new descriptors to hardware. */
  {
    ixge_dma_regs_t * dr = get_dma_regs (xd, VLIB_TX, queue_index);
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

static u8 * format_ixge_device_name (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, i);
  return format (s, "TenGigabitEthernet%U",
		 format_os_pci_handle, xd->pci_device.os_handle);
}

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
  static u8 is_64bit[] = {
#define _(a,f) 0,
#define _64(a,f) 1,
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
      if (is_64bit[i])
	xd->counters[i] += (u64) r[o+1] << (u64) 32;
    }
}

static u8 * format_ixge_device (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, i);
  ixge_phy_t * phy = xd->phys + xd->phy_index;

  ixge_update_counters (xd);

  if (phy->mdio_address != ~0)
    s = format (s, "PHY address %d, id 0x%x", phy->mdio_address, phy->id);
  else if (xd->sfp_eeprom.id == SFP_ID_sfp)
    s = format (s, "SFP optics %U", format_sfp_eeprom, &xd->sfp_eeprom);
  else
    s = format (s, "PHY not found");

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
	  dq->descriptors[i].rx_to_hw.packet_address = vlib_physmem_virtual_to_physical (vm, b->data);
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
      r->extended_control |= 1 << 28;

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
	   xd->index,
	   /* ethernet address */ addr8,
	   /* phy */ 0,
	   &xd->vlib_hw_if_index);
	if (error)
	  clib_error_report (error);
      }

      ixge_dma_init (xd, VLIB_RX, /* queue_index */ 0);
      ixge_dma_init (xd, VLIB_TX, /* queue_index */ 0);

      /* Enable TX & RX. */
      r->tx_dma_control |= (1 << 0);
      r->rx_enable |= 1;
    }
}

static uword
ixge_process (vlib_main_t * vm,
	      vlib_node_runtime_t * rt,
	      vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
    
  ixge_device_init (xm);

  while (1)
    {
      ixge_device_t * xd;
      vlib_process_suspend (vm, 250e-3);
      vec_foreach (xd, xm->devices)
	{
	  uword was_up = vlib_hw_interface_is_link_up (vm, xd->vlib_hw_if_index);
	  uword is_up = (xd->regs->xge_mac.link_status & (1 << 30)) != 0;
	  if (was_up != is_up)
	    vlib_hw_interface_set_flags (vm, xd->vlib_hw_if_index,
					 is_up ? VLIB_HW_INTERFACE_FLAG_LINK_UP : 0);
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

  error = unix_physmem_init (vm, /* physical_memory_required */ 1);
  if (error)
    return error;

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
  
  error = os_map_pci_resource (dev->os_handle, 0, &r);
  if (error)
    return error;

  vec_add2 (xm->devices, xd, 1);
  xd->pci_device = dev[0];
  xd->regs = r;
  xd->index = xd - xm->devices;

  if (vec_len (xm->devices) == 1)
    {
      vlib_register_node (vm, &ixge_process_node);
      xm->process_node_index = ixge_process_node.index;
    }

  return 0;
}

#define foreach_ixge_pci_device_id		\
  _ (82598, 0x10b6)				\
  _ (82598_bx, 0x1508)				\
  _ (82598af_dual_port, 0x10c6)			\
  _ (82598af_single_port, 0x10c7)		\
  _ (82598at, 0x10c8)				\
  _ (82598at2, 0x150b)				\
  _ (82598eb_sfp_lom, 0x10db)			\
  _ (82598eb_cx4, 0x10dd)			\
  _ (82598_cx4_dual_port, 0x10ec)		\
  _ (82598_da_dual_port, 0x10f1)		\
  _ (82598_sr_dual_port_em, 0x10e1)		\
  _ (82598eb_xf_lr, 0x10f4)			\
  _ (82599_kx4, 0x10f7)				\
  _ (82599_kx4_mezz, 0x1514)			\
  _ (82599_kr, 0x1517)				\
  _ (82599_combo_backplane, 0x10f8)		\
  _ (82599_cx4, 0x10f9)				\
  _ (82599_sfp, 0x10fb)				\
  _ (82599_backplane_fcoe, 0x152a)		\
  _ (82599_sfp_fcoe, 0x1529)			\
  _ (82599_sfp_em, 0x1507)			\
  _ (82599_xaui_lom, 0x10fc)			\
  _ (82599_t3_lom, 0x151c)			\
  _ (x540t, 0x1528)

static PCI_REGISTER_DEVICE (ixge_pci_device_registration) = {
  .init_function = ixge_pci_init,
  .supported_devices = {
#define _(t,i) { .vendor_id = PCI_VENDOR_ID_INTEL, .device_id = i, },
    foreach_ixge_pci_device_id
#undef _
    { 0 },
  },
};
