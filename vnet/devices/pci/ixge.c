#include <vnet/devices/pci/ixge.h>
#include <vnet/devices/xge/xge.h>
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
  ixge_main_t * xm = &ixge_main;
  vlib_main_t * vm = xm->vlib_main;
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

static void ixge_device_init (ixge_device_t * xd)
{
  ixge_phy_init (xd);
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

    /* Set head/tail indices and enable DMA. */
    dr->head_index = dq->head_index;
    dr->tail_index = dq->tail_index;

    dr->control |= 1 << 25;
  }

  return error;
}

#if 0
static VLIB_DEVICE_CLASS (ixge_device_class) = {
    .name = "ixge",
    .tx_function = ixge_interface_tx,
    .format_device_name = format_ixge_interface_instance,
    .format_device = format_ixge_device,
    .clear_counters = ixge_clear_hw_interface_counters,
};
#endif

static uword
ixge_process (vlib_main_t * vm,
	      vlib_node_runtime_t * rt,
	      vlib_frame_t * f)
{
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd;
  ixge_regs_t * r;
    
  vec_foreach (xd, xm->devices)
    ixge_device_init (xd);

#if 0
  error = ethernet_register_interface
    (vm,
     ixge_device_class.index,
     xd->index,
     /* ethernet address */ &dummy,
     /* phy */ 0,
     &xd->vlib_hw_if_index);
  if (error)
    return error;
#endif

  /* Enable all interrupts. */

  {
    uword event_type, i, * event_data = 0;

    while (1)
      {
	vlib_process_wait_for_event (vm);
	event_type = vlib_process_get_events (vm, &event_data);

	switch (event_type) {
	default:
	  ASSERT (0);
	  break;
	}

	if (event_data)
	  _vec_len (event_data) = 0;
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
  xm->vlib_main = vm;
  return vlib_call_init_function (vm, pci_bus_init);
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
    { .vendor_id = 0x104c, .device_id = 0x8024, },
    { 0 },
  },
};
