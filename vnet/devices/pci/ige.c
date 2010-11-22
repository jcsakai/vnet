#include <vnet/devices/pci/ige.h>
#include <vnet/ethernet/ethernet.h>
#include <vlib/unix/unix.h>
#include <vlib/unix/pci.h>

ige_main_t ige_main;

static void ige_semaphore_get (ige_device_t * xd)
{
  ige_main_t * xm = &ige_main;
  vlib_main_t * vm = xm->vlib_main;
  ige_regs_t * r = xd->regs;
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

static void ige_semaphore_release (ige_device_t * xd)
{
  ige_regs_t * r = xd->regs;
  r->software_semaphore &= ~3;
}

static void ige_software_firmware_sync (ige_device_t * xd, u32 sw_mask)
{
  ige_main_t * xm = &ige_main;
  vlib_main_t * vm = xm->vlib_main;
  ige_regs_t * r = xd->regs;
  u32 fw_mask = sw_mask << 5;
  u32 m, done = 0;

  while (! done)
    {
      ige_semaphore_get (xd);
      m = r->software_firmware_sync;
      done = (m & fw_mask) == 0;
      if (done)
	r->software_firmware_sync = m | sw_mask;
      ige_semaphore_release (xd);
      if (! done)
	vlib_process_suspend (vm, 10e-3);
    }
}

static void ige_software_firmware_sync_release (ige_device_t * xd, u32 sw_mask)
{
  ige_regs_t * r = xd->regs;
  ige_semaphore_get (xd);
  r->software_firmware_sync &= ~sw_mask;
  ige_semaphore_release (xd);
}

static clib_error_t *
ige_read_write_phy_reg (ethernet_phy_t * phy, u32 reg_index, u32 * data,
                        vlib_read_or_write_t rw)
{
  ige_device_t * xd = vec_elt_at_index (ige_main.devices, phy->opaque);
  ige_regs_t * r = xd->regs;
  const u32 ready_bit = 1 << 28;
  u32 x;
  
  ASSERT (xd->phy_index < 2);
  ige_software_firmware_sync (xd, 1 << (1 + xd->phy_index));

  /* Busy wait timed to take 28e-6 secs.  No suspend. */
  while (! (r->mdi_control & ready_bit))
    ;

  r->mdi_control =
    ((/* write data */ (data[0] & 0xffff) << 0)
     | ((reg_index & 0x1f)  << 16)
     | /* phy address */ (1 << 21)
     | /* opcode */ (((rw == VLIB_READ) ? 2 : 1) << 26));
  
  while (! ((x = r->mdi_control) & ready_bit))
    ;

  if (rw == VLIB_READ)
    data[0] = x & 0xffff;

  ige_software_firmware_sync_release (xd, 1 << (1 + xd->phy_index));

  return /* no error */ 0;
}

static clib_error_t *
ige_interface_admin_up_down (vlib_main_t * vm, u32 hw_if_index, u32 flags)
{
#if 0
  vlib_hw_interface_t * hif = vlib_get_hw_interface (vm, hw_if_index);
  uword is_up = (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) != 0;
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, hif->dev_instance);

  /* fixme do something?  show link up to partner? */
#endif

  return /* no error */ 0;
}

static u8 * format_ige_rx_from_hw_descriptor (u8 * s, va_list * va)
{
  ixge_rx_from_hw_descriptor_t * d = va_arg (*va, ixge_rx_from_hw_descriptor_t *);
  u32 s2 = d->status[2];
  u32 is_ip4, is_tcp, is_udp;
  uword indent = format_get_indent (s);

  s = format (s, "%s-owned",
	      (s2 & IXGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE) ? "sw" : "hw");
  s = format (s, ", length this descriptor %d", d->n_packet_bytes_this_descriptor);
  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET)
    s = format (s, ", end-of-packet");

  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_PASSED_MULTICAST_FILTER)
    s = format (s, ", passed multicast-filter");
  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_UDP_MYSTERY)
    s = format (s, ", udp-mystery");

  s = format (s, "\n%U", format_white_space, indent);

  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_IS_VLAN)
    s = format (s, "vlan header 0x%x\n%U", d->vlan_tag,
		format_white_space, indent);

  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_CRC_ERROR)
    s = format (s, ", crc-error");
  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_SYMBOL_ERROR)
    s = format (s, ", symbol-error");
  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_SEQUENCE_ERROR)
    s = format (s, ", sequence-error");
  if (s2 & IGE_RX_DESCRIPTOR_STATUS2_RX_DATA_ERROR)
    s = format (s, ", rx-data-error");

  if ((is_ip4 = !(s2 & IGE_RX_DESCRIPTOR_STATUS2_NOT_IP4)))
    {
      s = format (s, "ip4 checksum %s",
                  (s2 & IGE_RX_DESCRIPTOR_STATUS2_IP4_CHECKSUM_ERROR) ? "bad" : "ok");
      is_tcp = (s2 & IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_TCP_CHECKSUMMED) != 0;
      is_udp = (s2 & IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_UDP_CHECKSUMMED) != 0;
      if (is_tcp || is_udp)
	s = format (s, ", %s checksum %s",
                    is_tcp ? "tcp" : "udp",
                    (s2 & IGE_RX_DESCRIPTOR_STATUS2_IP4_TCP_UDP_CHECKSUM_ERROR) ? "bad" : "ok");
    }

  return s;
}

static u8 * format_ige_tx_descriptor (u8 * s, va_list * va)
{
  ige_tx_descriptor_t * d = va_arg (*va, ige_tx_descriptor_t *);
  u32 s0 = d->status0;
  uword indent = format_get_indent (s);

  s = format (s, "buffer 0x%Lx, %d bytes this buffer",
	      d->buffer_address, d->n_bytes_this_buffer);

  s = format (s, "\n%U", format_white_space, indent);

  s = format (s, "%s%s%s%s%s%s%s%s",
	      (s0 & (1 << 8)) ? "eop, " : "",
	      (s0 & (1 << 9)) ? "insert-fcs, " : "",
	      (s0 & (1 << 10)) ? "insert-l4-checksum, " : "",
	      (s0 & (1 << 11)) ? "report-status, " : "",
	      (s0 & (1 << 12)) ? "reserved12, " : "",
	      (s0 & (1 << 13)) ? "is-advanced, " : "",
	      (s0 & (1 << 14)) ? "vlan-enable, " : "",
	      (s0 & (1 << 15)) ? "interrupt-delay-enable, " : "");
	      
  return s;
}

typedef struct {
  ige_descriptor_t before, after;

  u32 buffer_index;

  u16 device_index;

  u8 queue_index;

  u8 is_start_of_packet;

  /* Copy of VLIB buffer; packet data stored in pre_data. */
  vlib_buffer_t buffer;
} ige_rx_dma_trace_t;

static u8 * format_ige_rx_dma_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_node_t * node = va_arg (*va, vlib_node_t *);
  ige_rx_dma_trace_t * t = va_arg (*va, ige_rx_dma_trace_t *);
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, t->device_index);
  ige_dma_queue_t * dq;
  format_function_t * f;
  uword indent = format_get_indent (s);

  dq = vec_elt_at_index (xd->dma_queues[VLIB_RX], t->queue_index);

  {
    vlib_sw_interface_t * sw = vlib_get_sw_interface (vm, xd->vlib_sw_if_index);
    s = format (s, "%U rx queue %d",
		format_vlib_sw_interface_name, vm, sw,
		t->queue_index);
  }

  s = format (s, "\n%Ubefore: %U",
	      format_white_space, indent,
	      format_ige_rx_from_hw_descriptor, &t->before);
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
} ige_rx_state_t;

#define foreach_ige_rx_error                    \
  _ (none, "no error")                          \
  _ (rx_data_error, "rx data error")            \
  _ (ip4_checksum_error, "ip4 checksum errors")

typedef enum {
#define _(f,s) IGE_RX_ERROR_##f,
  foreach_ige_rx_error
#undef _
  IGE_RX_N_ERROR,
} ige_rx_error_t;

typedef enum {
  IGE_RX_NEXT_IP4_INPUT,
  IGE_RX_NEXT_ETHERNET_INPUT,
  IGE_RX_NEXT_DROP,
  IGE_RX_N_NEXT,
} ige_rx_next_t;

always_inline void
ige_rx_next_and_error_from_status_x1 (u32 s00, u32 s02,
                                      u8 * next0, u8 * error0)
{
  u8 is0_ip4, n0, e0;

  e0 = IGE_RX_ERROR_none;
  n0 = IGE_RX_NEXT_ETHERNET_INPUT;

  is0_ip4 = s02 & IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_CHECKSUMMED;
  n0 = is0_ip4 ? IGE_RX_NEXT_IP4_INPUT : n0;
  e0 = (is0_ip4 && (s02 & IGE_RX_DESCRIPTOR_STATUS2_IP4_CHECKSUM_ERROR)
	? IGE_RX_ERROR_ip4_checksum_error
	: e0);

  e0 = ((s02 &
         (IGE_RX_DESCRIPTOR_STATUS2_CRC_ERROR
          | IGE_RX_DESCRIPTOR_STATUS2_SYMBOL_ERROR
          | IGE_RX_DESCRIPTOR_STATUS2_SEQUENCE_ERROR
          | IGE_RX_DESCRIPTOR_STATUS2_RX_DATA_ERROR))
        ? IGE_RX_ERROR_rx_data_error
        : e0);

  /* Check for error. */
  n0 = e0 != IGE_RX_ERROR_none ? IGE_RX_NEXT_DROP : n0;

  *error0 = e0;
  *next0 = n0;
}

always_inline void
ige_rx_next_and_error_from_status_x2 (u32 s00, u32 s02,
                                      u32 s10, u32 s12,
                                      u8 * next0, u8 * error0,
                                      u8 * next1, u8 * error1)
{
  ige_rx_next_and_error_from_status_x1 (s00, s02, next0, error0);
  ige_rx_next_and_error_from_status_x1 (s10, s12, next1, error1);
}

static void
ige_rx_trace (ige_main_t * xm,
	       ige_device_t * xd,
	       ige_dma_queue_t * dq,
	       ige_rx_state_t * rx_state,
	       ige_descriptor_t * before_descriptors,
	       u32 * before_buffers,
	       ige_descriptor_t * after_descriptors,
	       uword n_descriptors)
{
  vlib_main_t * vm = xm->vlib_main;
  vlib_node_runtime_t * node = rx_state->node;
  ige_rx_from_hw_descriptor_t * bd;
  ige_rx_to_hw_descriptor_t * ad;
  u32 * b, n_left, is_sop, next_index_sop;

  n_left = n_descriptors;
  b = before_buffers;
  bd = &before_descriptors->rx_from_hw;
  ad = &after_descriptors->rx_to_hw;
  is_sop = rx_state->is_start_of_packet;
  next_index_sop = rx_state->saved_start_of_packet_next_index;

  while (n_left >= 2)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      ige_rx_dma_trace_t * t0, * t1;
      u8 next0, error0, next1, error1;

      bi0 = b[0];
      bi1 = b[1];
      n_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      ige_rx_next_and_error_from_status_x2 (bd[0].status[0], bd[0].status[2],
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

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      ige_rx_dma_trace_t * t0;
      u8 next0, error0;

      bi0 = b[0];
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      ige_rx_next_and_error_from_status_x1 (bd[0].status[0], bd[0].status[2],
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

typedef struct {
  ige_tx_descriptor_t descriptor;

  u32 buffer_index;

  u16 device_index;

  u8 queue_index;

  u8 is_start_of_packet;

  /* Copy of VLIB buffer; packet data stored in pre_data. */
  vlib_buffer_t buffer;
} ige_tx_dma_trace_t;

static u8 * format_ige_tx_dma_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ige_tx_dma_trace_t * t = va_arg (*va, ige_tx_dma_trace_t *);
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, t->device_index);
  ige_dma_queue_t * dq;
  format_function_t * f;
  uword indent = format_get_indent (s);

  dq = vec_elt_at_index (xd->dma_queues[VLIB_TX], t->queue_index);

  {
    vlib_sw_interface_t * sw = vlib_get_sw_interface (vm, xd->vlib_sw_if_index);
    s = format (s, "%U tx queue %d",
		format_vlib_sw_interface_name, vm, sw,
		t->queue_index);
  }

  s = format (s, "\n%Udescriptor: %U",
	      format_white_space, indent,
	      format_ige_tx_descriptor, &t->descriptor);

  s = format (s, "\n%Ubuffer 0x%x: %U",
	      format_white_space, indent,
	      t->buffer_index,
	      format_vlib_buffer, &t->buffer);

  s = format (s, "\n%U",
	      format_white_space, indent);

  f = format_ethernet_header_with_length;
  if (! f || ! t->is_start_of_packet)
    f = format_hex_bytes;
  s = format (s, "%U", f, t->buffer.pre_data, sizeof (t->buffer.pre_data));

  return s;
}

typedef struct {
  vlib_node_runtime_t * node;

  u32 is_start_of_packet;
} ige_tx_state_t;

static void
ige_tx_trace (ige_main_t * xm,
	       ige_device_t * xd,
	       ige_dma_queue_t * dq,
	       ige_tx_state_t * tx_state,
	       ige_tx_descriptor_t * descriptors,
	       u32 * buffers,
	       uword n_descriptors)
{
  vlib_main_t * vm = xm->vlib_main;
  vlib_node_runtime_t * node = tx_state->node;
  ige_tx_descriptor_t * d;
  u32 * b, n_left, is_sop;

  n_left = n_descriptors;
  b = buffers;
  d = descriptors;
  is_sop = tx_state->is_start_of_packet;

  while (n_left >= 2)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      ige_tx_dma_trace_t * t0, * t1;

      bi0 = b[0];
      bi1 = b[1];
      n_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->is_start_of_packet = is_sop;
      is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
      t1->is_start_of_packet = is_sop;
      is_sop = (b1->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      t0->queue_index = dq->queue_index;
      t1->queue_index = dq->queue_index;
      t0->device_index = xd->device_index;
      t1->device_index = xd->device_index;
      t0->descriptor = d[0];
      t1->descriptor = d[1];
      t0->buffer_index = bi0;
      t1->buffer_index = bi1;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (&t1->buffer, b1, sizeof (b1[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));
      memcpy (t1->buffer.pre_data, b1->data, sizeof (t1->buffer.pre_data));

      b += 2;
      d += 2;
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      ige_tx_dma_trace_t * t0;

      bi0 = b[0];
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->is_start_of_packet = is_sop;
      is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      t0->queue_index = dq->queue_index;
      t0->device_index = xd->device_index;
      t0->descriptor = d[0];
      t0->buffer_index = bi0;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));

      b += 1;
      d += 1;
    }
}

always_inline uword
ige_ring_sub (ige_dma_queue_t * q, u32 i0, u32 i1)
{
  i32 d = i1 - i0;
  ASSERT (i0 < q->n_descriptors);
  ASSERT (i1 < q->n_descriptors);
  return d < 0 ? -d : d;
}

always_inline uword
ige_ring_add (ige_dma_queue_t * q, u32 i0, u32 i1)
{
  u32 d = i0 + i1;
  ASSERT (i0 < q->n_descriptors);
  ASSERT (i1 < q->n_descriptors);
  d -= d >= q->n_descriptors ? q->n_descriptors : 0;
  return d;
}

always_inline ige_dma_regs_t *
get_dma_regs (ige_device_t * xd, vlib_rx_or_tx_t rt, u32 qi)
{
  ige_regs_t * r = xd->regs;
  ASSERT (qi < 2);
  if (rt == VLIB_RX)
    return &r->rx_dma[qi];
  else
    return &r->tx_dma[qi];
}

always_inline uword
ige_tx_descriptor_matches_template (ige_main_t * xm, ige_tx_descriptor_t * d)
{
  u32 cmp;

  cmp = ((d->status0 & xm->tx_descriptor_template_mask.status0)
	 ^ xm->tx_descriptor_template.status0);
  if (cmp)
    return 0;
  cmp = ((d->status1 & xm->tx_descriptor_template_mask.status1)
	 ^ xm->tx_descriptor_template.status1);
  if (cmp)
    return 0;

  return 1;
}

static uword
ige_tx_no_wrap (ige_main_t * xm,
		 ige_device_t * xd,
		 ige_dma_queue_t * dq,
		 u32 * buffers,
		 u32 start_descriptor_index,
		 u32 n_descriptors,
		 ige_tx_state_t * tx_state)
{
  vlib_main_t * vm = xm->vlib_main;
  ige_tx_descriptor_t * d;
  u32 n_left = n_descriptors;
  u32 * to_free = vec_end (xm->tx_buffers_pending_free);
  u32 * to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 is_sop = tx_state->is_start_of_packet;
  u16 template_status = xm->tx_descriptor_template.status0;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index].tx;

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

      ASSERT (ige_tx_descriptor_matches_template (xm, d + 0));
      ASSERT (ige_tx_descriptor_matches_template (xm, d + 1));

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;
      d[1].buffer_address = vlib_get_buffer_data_physical_address (vm, bi1) + b1->current_data;

      d[0].n_bytes_this_buffer = len0;
      d[1].n_bytes_this_buffer = len1;

      d[0].status0 = template_status | (is_eop0 << IGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET);
      d[0].status1 = 0;
      d[1].status0 = template_status | (is_eop1 << IGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET);
      d[1].status1 = 0;

      d += 2;
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

      ASSERT (ige_tx_descriptor_matches_template (xm, d + 0));

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;

      d[0].n_bytes_this_buffer = len0;

      d[0].status0 = template_status | (is_eop0 << IGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET);
      d[0].status1 = 0;

      d += 1;
      is_sop = is_eop0;
    }

  if (tx_state->node->flags & VLIB_NODE_FLAG_TRACE)
    {
      to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
      ige_tx_trace (xm, xd, dq, tx_state,
		     &dq->descriptors[start_descriptor_index].tx,
		     to_tx,
		     n_descriptors);
    }

  _vec_len (xm->tx_buffers_pending_free) = to_free - xm->tx_buffers_pending_free;

  tx_state->is_start_of_packet = is_sop;

  return n_descriptors;
}

static uword
ige_interface_tx (vlib_main_t * vm,
		   vlib_node_runtime_t * node,
		   vlib_frame_t * f)
{
  ige_main_t * xm = &ige_main;
  vlib_interface_output_runtime_t * rd = (void *) node->runtime_data;
  ige_device_t * xd = vec_elt_at_index (xm->devices, rd->dev_instance);
  ige_dma_queue_t * dq;
  u32 * from, n_left_from, n_left_tx, n_descriptors_to_tx;
  u32 queue_index = 0;		/* fixme parameter */
  ige_tx_state_t tx_state;
  ige_dma_regs_t * dr = get_dma_regs (xd, VLIB_TX, queue_index);

  tx_state.node = node;
  tx_state.is_start_of_packet = 1;
  
  from = vlib_frame_vector_args (f);
  n_left_from = f->n_vectors;

  dq = vec_elt_at_index (xd->dma_queues[VLIB_TX], queue_index);

  n_left_tx = dq->n_descriptors;

  /* There might be room on the ring due to packets already transmitted. */
  {
    u32 hw_head_index = dr->head_index;
    n_left_tx += ige_ring_sub (dq, hw_head_index, dq->head_index);
    dq->head_index = hw_head_index;
  }

  n_descriptors_to_tx = clib_min (n_left_tx, n_left_from);

  _vec_len (xm->tx_buffers_pending_free) = 0;

  /* Process from tail to end of descriptor ring. */
  if (n_descriptors_to_tx > 0 && dq->tail_index < dq->n_descriptors)
    {
      u32 n = clib_min (dq->n_descriptors - dq->tail_index, n_descriptors_to_tx);
      n = ige_tx_no_wrap (xm, xd, dq, from, dq->tail_index, n, &tx_state);
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
      u32 n = ige_tx_no_wrap (xm, xd, dq, from, 0, n_descriptors_to_tx, &tx_state);
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
  CLIB_MEMORY_BARRIER ();

  dr->tail_index = dq->tail_index;

  clib_warning ("%d %d", dr->head_index, dr->tail_index);

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
      abort ();
    }

  return f->n_vectors;
}

static uword
ige_rx_queue_no_wrap (ige_main_t * xm,
		       ige_device_t * xd,
		       ige_dma_queue_t * dq,
		       ige_rx_state_t * rx_state,
		       u32 start_descriptor_index,
		       u32 n_descriptors)
{
  vlib_main_t * vm = xm->vlib_main;
  vlib_node_runtime_t * node = rx_state->node;
  ige_descriptor_t * d;
  static ige_descriptor_t * d_trace_save;
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
	  u8 is_eop0, is_vlan0, error0, next0;
	  u8 is_eop1, is_vlan1, error1, next1;

	  vlib_prefetch_buffer_with_index (vm, to_rx[2], STORE);
	  vlib_prefetch_buffer_with_index (vm, to_rx[3], STORE);
	  CLIB_PREFETCH (d + 4, CLIB_CACHE_LINE_BYTES, LOAD);

	  s00 = d[0].rx_from_hw.status[0];
	  s01 = d[1].rx_from_hw.status[0];

	  s20 = d[0].rx_from_hw.status[2];
	  s21 = d[1].rx_from_hw.status[2];

	  if (! ((s20 | s21) & IGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE))
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

	  CLIB_PREFETCH (b0->data, CLIB_CACHE_LINE_BYTES, LOAD);
	  CLIB_PREFETCH (b1->data, CLIB_CACHE_LINE_BYTES, LOAD);

	  is_eop0 = (s20 & IGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;
	  is_eop1 = (s21 & IGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;

	  ige_rx_next_and_error_from_status_x2 (s00, s20, s01, s21,
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

          is_vlan0 = (s20 & IGE_RX_DESCRIPTOR_STATUS2_IS_VLAN) != 0;
          is_vlan1 = (s21 & IGE_RX_DESCRIPTOR_STATUS2_IS_VLAN) != 0;

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = (is_sop && next0 == IGE_RX_NEXT_IP4_INPUT
			? sizeof (ethernet_header_t) + (is_vlan0 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);
	  l3_offset1 = (is_sop && next1 == IGE_RX_NEXT_IP4_INPUT
			? sizeof (ethernet_header_t) + (is_vlan1 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);

	  b0->current_length = len0 - l3_offset0;
	  b1->current_length = len1 - l3_offset1;
	  b0->current_data = l3_offset0;
	  b1->current_data = l3_offset1;

	  b_last->next_buffer = is_sop ? 0 : bi0;
	  b0->next_buffer = is_eop0 ? 0 : bi1;
	  bi_last = bi1;
	  b_last = b1;

	  if (DEBUG > 0)
	    {
	      u32 bi_sop0 = is_sop ? bi0 : bi_sop;
	      u32 bi_sop1 = is_eop0 ? bi1 : bi_sop0;

	      if (is_eop0)
		{
		  u8 * msg = vlib_validate_buffer (vm, bi_sop0, /* follow_buffer_next */ 1);
		  ASSERT (! msg);
		}
	      if (is_eop1)
		{
		  u8 * msg = vlib_validate_buffer (vm, bi_sop1, /* follow_buffer_next */ 1);
		  ASSERT (! msg);
		}
	    }

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
	  u8 is_eop0, is_vlan0, error0, next0;

	  s00 = d[0].rx_from_hw.status[0];
	  s20 = d[0].rx_from_hw.status[2];
	  if (! (s20 & IGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE))
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

	  is_eop0 = (s20 & IGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET) != 0;
	  ige_rx_next_and_error_from_status_x1 (s00, s20, &next0, &error0);

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

          is_vlan0 = (s20 & IGE_RX_DESCRIPTOR_STATUS2_IS_VLAN) != 0;

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = (is_sop && next0 == IGE_RX_NEXT_IP4_INPUT
			? sizeof (ethernet_header_t) + (is_vlan0 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);
	  b0->current_length = len0 - l3_offset0;
	  b0->current_data = l3_offset0;

	  b_last->next_buffer = is_sop ? 0 : bi0;
	  bi_last = bi0;
	  b_last = b0;

	  bi_sop = is_sop ? bi0 : bi_sop;

	  if (DEBUG > 0 && is_eop0)
	    {
	      u8 * msg = vlib_validate_buffer (vm, bi_sop, /* follow_buffer_next */ 1);
	      ASSERT (! msg);
	    }

	  if (PREDICT_TRUE (next0 == next_index))
	    {
	      to_next[0] = bi_sop;
	      to_next += is_eop0;
	      n_left_to_next -= is_eop0;
	      is_sop = is_eop0;
	    }
	  else
	    {
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
	ige_rx_trace (xm, xd, dq, rx_state,
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
ige_rx_queue (ige_main_t * xm,
	       ige_device_t * xd,
	       ige_rx_state_t * rx_state,
	       u32 queue_index)
{
  ige_dma_queue_t * dq = vec_elt_at_index (xd->dma_queues[VLIB_RX], queue_index);
  ige_dma_regs_t * dr = get_dma_regs (xd, VLIB_RX, dq->queue_index);
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
      n_packets += ige_rx_queue_no_wrap (xm, xd, dq, rx_state, sw_head_index, n_tried);
      sw_head_index = ige_ring_add (dq, sw_head_index, rx_state->n_descriptors_done_this_call);
      if (rx_state->n_descriptors_done_this_call != n_tried)
	goto done;
    }
  if (hw_head_index >= sw_head_index)
    {
      u32 n_tried = hw_head_index - sw_head_index;
      n_packets += ige_rx_queue_no_wrap (xm, xd, dq, rx_state, sw_head_index, n_tried);
      sw_head_index = ige_ring_add (dq, sw_head_index, rx_state->n_descriptors_done_this_call);
    }

 done:
  dq->head_index = sw_head_index;
  dq->tail_index = ige_ring_add (dq, dq->tail_index, rx_state->n_descriptors_done_total);

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

static void ige_interrupt (ige_main_t * xm, ige_device_t * xd, u32 i)
{
  vlib_main_t * vm = xm->vlib_main;
  ige_regs_t * r = xd->regs;

  if (i != 2)
    {
      ELOG_TYPE_DECLARE (e) = {
	.function = (char *) __FUNCTION__,
	.format = "ige %d, %s",
	.format_args = "i1t1",
	.n_enum_strings = 26,
	.enum_strings = {
	  "tx descriptor written back",
	  "tx queue empty",
	  "link status change",
	  "rx sequence error",
          "rx descriptor min threshold",
	  "reserved5",
	  "rx overrrun",
	  "rx timer interrupt",
	  "reserved8",
	  "mdi access complete",
	  "rx ordered sets",
	  "sdp0",
	  "sdp1",
	  "sdp2",
	  "sdp3",
	  "tx descriptors low",
	  "rx small packet",
	  "rx ack",
	  "reserved18",
	  "reserved19",
          "rx queue 0 descriptor fifo parity error",
          "tx queue 0 descriptor fifo parity error",
          "pci master fifo parity error",
          "packet buffer parity error",
          "rx queue 1 descriptor fifo parity error",
          "tx queue 1 descriptor fifo parity error",
	},
      };
      struct { u8 instance; u8 index; } * ed;
      ed = ELOG_DATA (&vm->elog_main, e);
      ed->instance = xd->device_index;
      ed->index = i;
    }
  else
    {
      ethernet_phy_status (&xd->phy);
      uword is_up = ethernet_phy_is_link_up (&xd->phy);

      ELOG_TYPE_DECLARE (e) = {
	.function = (char *) __FUNCTION__,
	.format = "ige %d, status 0x%x",
	.format_args = "i4i4",
      };
      struct { u32 instance, status; } * ed;

      if (is_up)
	r->control |= 1 << 6;
      else
	r->control &= ~(1 << 6);

      ed = ELOG_DATA (&vm->elog_main, e);
      ed->instance = xd->device_index;
      ed->status = r->status;

      vlib_hw_interface_set_flags (vm, xd->vlib_hw_if_index,
				   is_up ? VLIB_HW_INTERFACE_FLAG_LINK_UP : 0);
    }
}

static uword
ige_device_input (ige_main_t * xm,
                  ige_device_t * xd,
                  ige_rx_state_t * rx_state)
{
  ige_regs_t * r = xd->regs;
  u32 i, s;
  uword n_rx_packets = 0;

  s = r->interrupt.status_clear_to_read;
  foreach_set_bit (i, s, ({
    if (i == 7)
      n_rx_packets += ige_rx_queue (xm, xd, rx_state, 0);
    else
      ige_interrupt (xm, xd, i);
  }));

  return n_rx_packets;
}

static uword
ige_input (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * f)
{
  ige_main_t * xm = &ige_main;
  ige_device_t * xd;
  ige_rx_state_t _rx_state, * rx_state = &_rx_state;
  uword n_rx_packets = 0;

  rx_state->node = node;
  rx_state->next_index = node->cached_next_index;

  if (node->state == VLIB_NODE_STATE_INTERRUPT)
    {    
      uword i;

      /* Loop over devices with interrupts. */
      foreach_set_bit (i, node->runtime_data[0], ({
	xd = vec_elt_at_index (xm->devices, i);
	n_rx_packets += ige_device_input (xm, xd, rx_state);

	/* Re-enable interrupts since we're going to stay in interrupt mode. */
	if (! (node->flags & VLIB_NODE_FLAG_SWITCH_FROM_INTERRUPT_TO_POLLING_MODE))
	  xd->regs->interrupt.enable_write_1_to_set = ~0;
      }));

      /* Clear mask of devices with pending interrupts. */
      node->runtime_data[0] = 0;
    }
  else
    {
      /* Poll all devices for input/interrupts. */
      vec_foreach (xd, xm->devices)
	{
	  n_rx_packets += ige_device_input (xm, xd, rx_state);

	  /* Re-enable interrupts when switching out of polling mode. */
	  if (node->flags & VLIB_NODE_FLAG_SWITCH_FROM_POLLING_TO_INTERRUPT_MODE)
	    xd->regs->interrupt.enable_write_1_to_set = ~0;
	}
    }

  return n_rx_packets;
}

static char * ige_rx_error_strings[] = {
#define _(n,s) s,
    foreach_ige_rx_error
#undef _
};

static VLIB_REGISTER_NODE (ige_input_node) = {
  .function = ige_input,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "ige-input",

  /* Will be enabled if/when hardware is detected. */
  .state = VLIB_NODE_STATE_DISABLED,

  .format_buffer = format_ethernet_header_with_length,
  .format_trace = format_ige_rx_dma_trace,

  .n_errors = IGE_RX_N_ERROR,
  .error_strings = ige_rx_error_strings,

  .n_next_nodes = IGE_RX_N_NEXT,
  .next_nodes = {
    [IGE_RX_NEXT_DROP] = "error-drop",
    [IGE_RX_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [IGE_RX_NEXT_IP4_INPUT] = "ip4-input-no-checksum",
  },
};

static u8 * format_ige_device_name (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, i);
  return format (s, "GigabitEthernet%U",
		 format_os_pci_handle, xd->pci_device.os_handle);
}

#define IGE_COUNTER_IS_64_BIT (1 << 0)
#define IGE_COUNTER_NOT_CLEAR_ON_READ (1 << 1)

static u8 ige_counter_flags[] = {
#define _(a,f) 0,
#define _64(a,f) IGE_COUNTER_IS_64_BIT,
  foreach_ige_counter
#undef _
#undef _64
};

static void ige_update_counters (ige_device_t * xd)
{
  /* Byte offset for counter registers. */
  static u32 reg_offsets[] = {
#define _(a,f) (a) / sizeof (u32),
#define _64(a,f) _(a,f)
    foreach_ige_counter
#undef _
#undef _64
  };
  volatile u32 * r = (volatile u32 *) xd->regs;
  int i;

  for (i = 0; i < ARRAY_LEN (xd->counters); i++)
    {
      u32 o = reg_offsets[i];
      xd->counters[i] += r[o];
      if (ige_counter_flags[i] & IGE_COUNTER_NOT_CLEAR_ON_READ)
	r[o] = 0;
      if (ige_counter_flags[i] & IGE_COUNTER_IS_64_BIT)
	xd->counters[i] += (u64) r[o+1] << (u64) 32;
    }
}

static u8 * format_ige_device_id (u8 * s, va_list * args)
{
  u32 device_id = va_arg (*args, u32);
  char * t = 0;
  switch (device_id)
    {
#define _(f,n) case n: t = #f; break;
      foreach_ige_pci_device_id;
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

static u8 * format_ige_device (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, dev_instance);
  ethernet_phy_t * phy = &xd->phy;
  uword indent = format_get_indent (s);

  ige_update_counters (xd);

  s = format (s, "Intel 8257X: id %U\n%U%U",
	      format_ige_device_id, xd->device_id,
	      format_white_space, indent + 2,
	      format_ethernet_media, &phy->media);

  {
    u32 i;
    u64 v;
    static char * names[] = {
#define _(a,f) #f,
#define _64(a,f) _(a,f)
    foreach_ige_counter
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

static void ige_clear_hw_interface_counters (u32 instance)
{
  ige_main_t * xm = &ige_main;
  ige_device_t * xd = vec_elt_at_index (xm->devices, instance);
  ige_update_counters (xd);
  memcpy (xd->counters_last_clear, xd->counters, sizeof (xd->counters));
}

VLIB_DEVICE_CLASS (ige_device_class) = {
    .name = "ige",
    .tx_function = ige_interface_tx,
    .format_device_name = format_ige_device_name,
    .format_device = format_ige_device,
    .format_tx_trace = format_ige_tx_dma_trace,
    .clear_counters = ige_clear_hw_interface_counters,
    .admin_up_down_function = ige_interface_admin_up_down,
};

static clib_error_t *
ige_dma_init (ige_device_t * xd, vlib_rx_or_tx_t rt, u32 queue_index)
{
  ige_main_t * xm = &ige_main;
  vlib_main_t * vm = xm->vlib_main;
  ige_dma_queue_t * dq;
  clib_error_t * error = 0;

  vec_validate (xd->dma_queues[rt], queue_index);
  dq = vec_elt_at_index (xd->dma_queues[rt], queue_index);

  if (! xm->n_descriptors_per_cache_line)
    xm->n_descriptors_per_cache_line = CLIB_CACHE_LINE_BYTES / sizeof (dq->descriptors[0]);

  if (! xm->n_bytes_in_rx_buffer)
    xm->n_bytes_in_rx_buffer = 512;
  xm->n_bytes_in_rx_buffer = round_pow2 (xm->n_bytes_in_rx_buffer, 512);
  xm->vlib_buffer_free_list_index = vlib_buffer_get_or_create_free_list (vm, xm->n_bytes_in_rx_buffer);

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

      for (i = 0; i < dq->n_descriptors; i++)
	dq->descriptors[i].tx = xm->tx_descriptor_template;

      vec_validate (xm->tx_buffers_pending_free, dq->n_descriptors - 1);
    }

  {
    ige_dma_regs_t * dr = get_dma_regs (xd, rt, queue_index);
    uword a;

    a = vlib_physmem_virtual_to_physical (vm, dq->descriptors);
    dr->descriptor_address[0] = (u64) a;
    dr->descriptor_address[1] = (u64) a >> (u64) 32;
    dr->n_descriptor_bytes = dq->n_descriptors * sizeof (dq->descriptors[0]);
    dq->head_index = dq->tail_index = 0;

    /* Interrupt every 10*1.024e-6 secs. */
    dr->interrupt_delay_timer = 10;

    if (rt == VLIB_RX)
      /* Give hardware all but last cache line of descriptors. */
      dq->tail_index = dq->n_descriptors - xm->n_descriptors_per_cache_line;

    CLIB_MEMORY_BARRIER ();

    dr->control |= ((/* prefetch threshold */ 32 << 0)
		    | (/* writeback threshold */ 16 << 16));

    /* Set head/tail indices and enable DMA. */
    dr->head_index = dq->head_index;
    dr->tail_index = dq->tail_index;
  }

  return error;
}

static void ige_device_init (ige_main_t * xm)
{
  vlib_main_t * vm = xm->vlib_main;
  ige_device_t * xd;
    
  /* Reset chip(s). */
  vec_foreach (xd, xm->devices)
    {
      ige_regs_t * r = xd->regs;
      const u32 reset_bit = (1 << 26);

      r->control |= reset_bit;

      /* No need to suspend.  Timed to take ~1e-6 secs */
      while (r->control & reset_bit)
	;

      /* Software loaded. */
      r->extended_control |= (1 << 28);

      xd->phy.opaque = xd->device_index;
      xd->phy.read_write = ige_read_write_phy_reg;
      xd->phy.vlib_main = vm;
      ethernet_phy_reset (&xd->phy);
      ethernet_phy_init (&xd->phy);
      ethernet_phy_negotiate_media (&xd->phy);

      /* Register ethernet interface. */
      {
	u8 addr8[6];
	u32 i, addr32[2];
	clib_error_t * error;

	addr32[0] = r->rx_ethernet_address[0][0];
	addr32[1] = r->rx_ethernet_address[0][1];
	for (i = 0; i < 6; i++)
	  addr8[i] = addr32[i / 4] >> ((i % 4) * 8);

	error = ethernet_register_interface
	  (vm,
	   ige_device_class.index,
	   xd->device_index,
	   /* ethernet address */ addr8,
	   /* phy */ &xd->phy,
	   &xd->vlib_hw_if_index);
	if (error)
	  clib_error_report (error);
      }

      {
	vlib_sw_interface_t * sw = vlib_get_hw_sw_interface (vm, xd->vlib_hw_if_index);
	xd->vlib_sw_if_index = sw->sw_if_index;
      }

      ige_dma_init (xd, VLIB_RX, /* queue_index */ 0);
      ige_dma_init (xd, VLIB_TX, /* queue_index */ 0);

      /* RX buffers are 512 bytes. */
      r->rx_control |= (2 << 16);

      /* Strip ethernet crc and don't include in descriptor length. */
      r->rx_control |= (1 << 26);

      /* Accept packets > 1522 bytes. */
      r->rx_control |= (1 << 5);

      /* Accept all broadcast packets.  Multicasts must be explicitly
	 added to dst_ethernet_address register array. */
      r->rx_control |= (1 << 15);

      /* Pad short packets. */
      r->tx_control |= 1 << 3;

      /* Multiple descriptor read. */
      r->tx_control |= 1 << 28;

      /* RX/TX enable. */
      r->rx_control |= 1 << 1;
      r->tx_control |= 1 << 1;

      r->interrupt.enable_write_1_to_set = ~0;
    }
}

static uword
ige_process (vlib_main_t * vm,
	      vlib_node_runtime_t * rt,
	      vlib_frame_t * f)
{
  ige_main_t * xm = &ige_main;
  ige_device_t * xd;
  uword event_type, * event_data = 0;
    
  ige_device_init (xm);

  /* Clear all counters. */
  vec_foreach (xd, xm->devices)
    {
      ige_update_counters (xd);
      memset (xd->counters, 0, sizeof (xd->counters));
    }

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
	    xm->time_last_stats_update = now;
	    vec_foreach (xd, xm->devices)
	      ige_update_counters (xd);
	  }
      }
    }
	    
  return 0;
}

static vlib_node_registration_t ige_process_node = {
    .function = ige_process,
    .type = VLIB_NODE_TYPE_PROCESS,
    .name = "ige-process",
};

clib_error_t * ige_init (vlib_main_t * vm)
{
  ige_main_t * xm = &ige_main;
  clib_error_t * error;

  xm->vlib_main = vm;
  memset (&xm->tx_descriptor_template, 0, sizeof (xm->tx_descriptor_template));
  memset (&xm->tx_descriptor_template_mask, 0, sizeof (xm->tx_descriptor_template_mask));
  xm->tx_descriptor_template.status0 =
    (0*IGE_TX_DESCRIPTOR_STATUS0_IS_ADVANCED
     | IGE_TX_DESCRIPTOR_STATUS0_INSERT_FCS);
  xm->tx_descriptor_template_mask.status0 = 0xffff;
  xm->tx_descriptor_template_mask.status1 = 0x00003fff;

  xm->tx_descriptor_template_mask.status0 &=
    ~(IGE_TX_DESCRIPTOR_STATUS0_IS_END_OF_PACKET
      | IGE_TX_DESCRIPTOR_STATUS0_REPORT_STATUS);
  xm->tx_descriptor_template_mask.status1 &=
    ~(IGE_TX_DESCRIPTOR_STATUS1_DONE);

  error = vlib_call_init_function (vm, pci_bus_init);

  return error;
}

VLIB_INIT_FUNCTION (ige_init);

static clib_error_t *
ige_pci_init (vlib_main_t * vm, pci_device_t * dev)
{
  ige_main_t * xm = &ige_main;
  clib_error_t * error;
  void * r;
  ige_device_t * xd;
  
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

    vlib_node_set_state (vm, ige_input_node.index, VLIB_NODE_STATE_INTERRUPT);
    lp->device_input_node_index = ige_input_node.index;
    lp->device_index = xd->device_index;
  }

  if (vec_len (xm->devices) == 1)
    {
      vlib_register_node (vm, &ige_process_node);
      xm->process_node_index = ige_process_node.index;
    }

  os_add_pci_disable_interrupts_reg
    (dev->os_handle,
     /* resource */ 0,
     STRUCT_OFFSET_OF (ige_regs_t, interrupt.enable_write_1_to_clear),
     /* value to write */ ~0);

  return 0;
}

static PCI_REGISTER_DEVICE (ige_pci_device_registration) = {
  .init_function = ige_pci_init,
  .supported_devices = {
#define _(t,i) { .vendor_id = PCI_VENDOR_ID_INTEL, .device_id = i, },
    foreach_ige_pci_device_id
#undef _
    { 0 },
  },
};
