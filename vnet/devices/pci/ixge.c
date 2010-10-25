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
ixge_phy_init_from_eeprom (ixge_device_t * xd, u16 sfp_type)
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

  {
    u32 v, last, i;

  again:
    xd->regs->xge_mac.auto_negotiation_control2 &= ~(3 << 16);
    xd->regs->xge_mac.auto_negotiation_control2 |= 2 << 16;

    v = xd->regs->xge_mac.auto_negotiation_control;
    v &= ~(3 << 7);
    v |= (1 << 7);
    v &= ~(7 << 13);
    v |= (3 << 13);
    v |= (1 << 12);
    xd->regs->xge_mac.auto_negotiation_control = v;

    /* spd 5 => is_10g speed.
       spd 3 => disable laser.  both outputs. */
    /* Configure pins 3 & 5 as output. */
    v = ((1 << 3) | (1 << 5)) << 8;
    /* Select 10g and enable laser. */
    v |= ((1 << 5) | (0 << 3));
    xd->regs->sdp_control = v;

    i = 0;
    last = 0;
    while (1)
      {
	v = xd->regs->xge_mac.link_status;
	while (v != last)
	  {
	    ELOG_TYPE_DECLARE (e) = {
	      .function = (char *) __FUNCTION__,
	      .format = "ixge %d, link 0x%x",
	      .format_args = "i4i4",
	    };
	    struct { u32 instance, link; } * ed;
	    ed = ELOG_DATA (&vlib_global_main.elog_main, e);
	    ed->instance = xd->device_index;
	    ed->link = v;
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
	    ed = ELOG_DATA (&vlib_global_main.elog_main, e);
	    ed->instance = xd->device_index;

	    xd->regs->control |= 1 << 3;
	    while (xd->regs->control & (1 << 3))
	      ;
	    goto again;
	  }
      }
  }

  return /* no error */ 0;
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
	    clib_error_t * e = ixge_phy_init_from_eeprom (xd, 5 + xd->pci_function);
	    if (e)
	      clib_error_report (e);
	  }

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
      d[0].status[1] = IXGE_TX_DESCRIPTOR_STATUS1_N_BYTES_IN_PACKET (len0);

      is_sop = is_eop0;
      d += 1;
    }

  _vec_len (xm->tx_buffers_pending_free) = to_free - xm->tx_buffers_pending_free;
  *last_is_sop = is_sop;

  return n_descriptors;
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
    n_left_tx += ixge_ring_sub (dq, hw_head_index, dq->head_index);
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

#define foreach_ixge_rx_error				\
  _ (none, "no error")					\
  _ (ip4_checksum_errors, "ip4 checksum errors")	\
  _ (mac_errors, "ethernet mac errors")

typedef enum {
#define _(f,s) IXGE_RX_ERROR_##f,
  foreach_ixge_rx_error
#undef _
  IXGE_RX_N_ERROR,
} ixge_rx_error_t;

typedef enum {
  IXGE_RX_NEXT_ETHERNET_INPUT,
  IXGE_RX_NEXT_DROP,
  IXGE_RX_N_NEXT,
} ixge_rx_next_t;

typedef struct {
  vlib_node_runtime_t * node;

  u32 next_index;

  u32 saved_start_of_packet_buffer_index;

  u32 is_start_of_packet;

  u32 n_descriptors_done_total;

  u32 n_descriptors_done_this_call;
} ixge_rx_state_t;

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
  u32 n_descriptors_left = n_descriptors;
  u32 * to_rx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 * to_add;
  u32 bi_sop = rx_state->saved_start_of_packet_buffer_index;
  u32 is_sop = rx_state->is_start_of_packet;
  u32 next_index, n_left_to_next, * to_next;
  vlib_buffer_t * b_sop;
  u32 n_packets = 0;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index];

  b_sop = 0;
  next_index = rx_state->next_index;

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

      while (n_descriptors_left > 0 && n_left_to_next > 0)
	{
	  u32 bi0, fi0;
	  vlib_buffer_t * b0;
	  u32 s20;
	  u8 is_eop0, next0, error0;

	  bi0 = to_rx[0];
	  ASSERT (to_add >= xm->rx_buffers_to_add);
	  fi0 = to_add[0];

	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, bi0));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, fi0));

	  b0 = vlib_get_buffer (vm, bi0);

	  s20 = d[0].rx_from_hw.status[2];
	  if (! (s20 & IXGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE))
	    goto found_hw_owned_descriptor;
	  is_eop0 = (s20 & IXGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;

	  b0->flags |= (!is_eop0 << VLIB_BUFFER_LOG2_NEXT_PRESENT);
	  b0->current_length = d[0].rx_from_hw.n_packet_bytes_this_descriptor;

	  /* Give new buffer to hardware. */
	  d[0].rx_to_hw.tail_address = vlib_get_buffer_data_physical_address (vm, fi0);
	  d[0].rx_to_hw.head_address = d[0].rx_to_hw.tail_address;
	  to_rx[0] = fi0;
	  to_rx += 1;
	  to_add -= 1;

	  next0 = is_sop ? IXGE_RX_NEXT_ETHERNET_INPUT : next0;

	  error0 = IXGE_RX_ERROR_none;

	  next0 = error0 != IXGE_RX_ERROR_none ? IXGE_RX_NEXT_DROP : next0;

	  b0->error = node->errors[error0];

	  bi_sop = is_sop ? bi0 : bi_sop;
	  to_next[0] = bi_sop;
	  to_next += is_eop0;
	  n_left_to_next -= is_eop0;
	  n_packets += is_eop0;
	  d += 1;
	  n_descriptors_left -= 1;

	  is_sop = is_eop0;

	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);
	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	      to_next[0] = bi_sop;
	      to_next += is_eop0;
	      n_left_to_next -= is_eop0;
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

 found_hw_owned_descriptor:
  if (n_descriptors_left > 0)
    vlib_put_next_frame (vm, node, next_index, n_left_to_next);

  _vec_len (xm->rx_buffers_to_add) = (to_add + 1) - xm->rx_buffers_to_add;

  {
    u32 n_done = n_descriptors - n_descriptors_left;

    rx_state->n_descriptors_done_this_call = n_done;
    rx_state->n_descriptors_done_total += n_done;
    rx_state->is_start_of_packet = is_sop;
    rx_state->saved_start_of_packet_buffer_index = bi_sop;
    rx_state->next_index = next_index;

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
  rx_state->n_descriptors_done_total = 0;

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
	"reserved29",
	"tcp timer",
	"reserved31",
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
	  clib_warning ("mode %d speed %d", (v >> 26) & 3, (v >> 28) & 3);
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
  u32 i, s;
  uword n_rx_packets = 0;

  s = r->interrupt.status_write_1_to_clear;
  r->interrupt.status_write_1_to_clear = s;
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
      }));
    }
  else
    {
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

  .format_trace = 0,		/* fixme */

  .n_errors = IXGE_RX_N_ERROR,
  .error_strings = ixge_rx_error_strings,

  .n_next_nodes = IXGE_RX_N_NEXT,
  .next_nodes = {
    [IXGE_RX_NEXT_DROP] = "error-drop",
    [IXGE_RX_NEXT_ETHERNET_INPUT] = "ethernet-input",
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

static u8 * format_ixge_device (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  ixge_main_t * xm = &ixge_main;
  ixge_device_t * xd = vec_elt_at_index (xm->devices, dev_instance);
  ixge_phy_t * phy = xd->phys + xd->phy_index;
  uword indent = format_get_indent (s);

  ixge_update_counters (xd);

  s = format (s, "Intel 10G: %U", format_ixge_device_id, xd->device_id);

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

      ixge_dma_init (xd, VLIB_RX, /* queue_index */ 0);
      ixge_dma_init (xd, VLIB_TX, /* queue_index */ 0);

      if (0) r->interrupt.misc_mapping = ((1 << 7) | 0) << 8;

      /* RX queue gets mapped to interrupt bit 0.
	 We don't use TX interrupts. */
      r->interrupt.queue_mapping[0] = ((1 << 7) | 0) << 0;

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
      if (0) r->interrupt.enable_write_1_to_set = ~0;
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

    vlib_node_set_state (vm, ixge_input_node.index, VLIB_NODE_STATE_POLLING);
    lp->device_input_node_index = ixge_input_node.index;
    lp->device_index = xd->device_index;
  }

  if (vec_len (xm->devices) == 1)
    {
      vlib_register_node (vm, &ixge_process_node);
      xm->process_node_index = ixge_process_node.index;
    }

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
