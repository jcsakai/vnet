/*
 * devices/freescale/fge.c: Freescale 85xx ethernet (TSEC) driver
 *
 * Copyright (c) 2012 Eliot Dresselhaus
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
#include <vnet/devices/freescale/fge.h>
#include <vnet/ethernet/ethernet.h>
#include <vlib/unix/unix.h>

fge_main_t fge_main;

#define FGE_RX_BUFFER_N_BYTES 512

#define FGE_ALWAYS_POLL 0

#define EVENT_SET_FLAGS 0

static vlib_node_registration_t fge_input_node;
static vlib_node_registration_t fge_process_node;

static clib_error_t *
fge_read_write_phy_reg (ethernet_phy_t * phy, u32 reg_index, u32 * data,
                        vlib_read_or_write_t rw)
{
  fge_device_t * fd = vec_elt_at_index (fge_main.devices, phy->opaque);
  fge_regs_t * r = fd->regs;

  r->mac.mii.address = reg_index | (fd->phy.phy_address << 8);

  if (rw == VLIB_READ)
    {
      /* 0 -> 1 transition starts read cycle. */
      ASSERT ((r->mac.mii.read_command & (1 << 0)) == 0);
      r->mac.mii.read_command |= (1 << 0);

      /* Wait for busy & read data ready bits. */
      while (r->mac.mii.status & ((1 << 2) | (1 << 0)))
	;

      data[0] = r->mac.mii.read_data;

      /* Clear for next command. */
      r->mac.mii.read_command &= ~(1 << 0);
    }

  else
    {
      ASSERT ((r->mac.mii.status & (1 << 0)) == 0);

      /* Start write cycle. */
      r->mac.mii.write_data = data[0];

      /* Wait for busy bit to clear. */
      while (r->mac.mii.status & (1 << 0))
	;
    }

  return /* no error */ 0;
}

static clib_error_t *
fge_interface_admin_up_down (vnet_main_t * vm, u32 hw_if_index, u32 flags)
{
  vnet_hw_interface_t * hif = vnet_get_hw_interface (vm, hw_if_index);
  uword is_up = (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP) != 0;
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, hif->dev_instance);
  fge_regs_t * r = fd->regs;
  
  ASSERT (r == 0);

  /* RX/TX enable. */
  if (is_up)
    {
    }
  else
    {
    }

  return /* no error */ 0;
}

static u8 * format_fge_rx_or_tx_descriptor (u8 * s, va_list * va)
{
  fge_dma_descriptor_t * d = va_arg (*va, fge_dma_descriptor_t *);
  vlib_rx_or_tx_t rx_or_tx = va_arg (*va, vlib_rx_or_tx_t);
  int is_wb = va_arg (*va, int); /* Descriptor was written back? */
  uword indent = format_get_indent (s);
  uword is_rx = rx_or_tx == VLIB_RX;
  u16 t;

  s = format (s, "buffer 0x%Lx, %d bytes this %s",
	      d->buffer_address,
	      d->n_bytes_this_buffer_or_packet,
	      is_rx && (t & (1 << 11)) ? "packet plus CRC/offload" : "buffer");

  s = format (s, "\n%U", format_white_space, indent);

  t = d->status;
  s = format (s, "own %s, %s%s%s%s",
	      (t & (1 << 15)) ? "hw" : "sw",
	      (t & (1 << 13)) ? "wrap, " : "",
	      (t & (1 << 12)) ? "interrupt, " : "",
	      (t & (1 << 10)) && is_rx ? "sop, " : "",
	      (t & (1 << 11)) ? "eop, " : "");

  if (is_rx)
    s = format (s, "%s%s%s%s%s%s%s%s%s",
		(t & (1 << 8)) ? "dst address miss" : "",
		(t & (1 << 7)) ? "broadcast" : "",
		(t & (1 << 6)) ? "multicast" : "",
		(t & (1 << 5)) ? "oversize" : "",
		(t & (1 << 4)) ? "bad byte alignment" : "",
		(t & (1 << 3)) ? "undersize" : "",
		(t & (1 << 2)) ? "crc error" : "",
		(t & (1 << 1)) ? "fifo overrun" : "",
		(t & (1 << 0)) ? "frame truncated" : "");
  else
    s = format
      (s, "%s%s%s%s%s%s%s",
       (t & (1 << 14)) ? "tx pad to 64 bytes, " : "",
       (t & (1 << 10)) ? "tx append-crc, " : "",
       (t & (1 << 9)) ? (is_wb ? "tx deferred, " : "tx user preamble, ") : "",
       (t & (1 << 7)) ? (is_wb ? "tx late collision, " : "tx huge frame, ") : "",
       (t & (1 << 6)) ? (is_wb ? "tx retry limit hit, " : "tx control frame, ") : "",
       (t & (1 << 1)) ? (is_wb ? "tx underrun, " : "tx tcp/ip offload, ") : "",
       (t & (1 << 0)) ? "tx frame truncated, " : "");
	      
  return s;
}

#define foreach_fge_error			\
  _ (none, "no error")                          \
  _ (rx_data_error, "rx data error")            \
  _ (ip4_checksum_error, "ip4 checksum errors")	\
  _ (tx_full_drops, "tx ring full drops")

typedef enum {
#define _(f,s) FGE_ERROR_##f,
  foreach_fge_error
#undef _
  FGE_N_ERROR,
} fge_error_t;

typedef enum {
  FGE_RX_NEXT_IP4_INPUT,
  FGE_RX_NEXT_IP6_INPUT,
  FGE_RX_NEXT_ETHERNET_INPUT,
  FGE_RX_NEXT_DROP,
  FGE_RX_N_NEXT,
} fge_rx_next_t;

always_inline void
fge_rx_next_and_error_from_status_x1 (fge_dma_descriptor_t * d0,
				      fge_offload_header_t * o0,
				      u8 * next0, u8 * error0, u32 * flags0)
{
  u8 n0, e0, is_ip0, is_ip60;
  u16 f0, s0;
  u32 l0;

  e0 = FGE_ERROR_none;
  n0 = FGE_RX_NEXT_ETHERNET_INPUT;

  f0 = o0->flags;
  is_ip0 = (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_OR_IP6) != 0;
  is_ip60 = (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP6) != 0;
  
  n0 = is_ip0 && is_ip60 ? FGE_RX_NEXT_IP6_INPUT : n0;
  n0 = is_ip0 && ! is_ip60 ? FGE_RX_NEXT_IP4_INPUT : n0;

  e0 = (((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_COMPUTED)
	 && (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_ERROR))
	? FGE_ERROR_ip4_checksum_error
	: e0);

  l0 = ((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_COMPUTED)
	? IP_BUFFER_L4_CHECKSUM_COMPUTED
	: 0);

  l0 |= ((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_ERROR)
	 ? 0
	 : IP_BUFFER_L4_CHECKSUM_CORRECT);

  s0 = d0->status;

  /* Classify rx error bits [5:0] as "data error". */
  e0 = (s0 & 0x3f) != 0 ? FGE_ERROR_rx_data_error : e0;

  n0 = e0 != FGE_ERROR_none ? FGE_RX_NEXT_DROP : n0;

  *next0 = n0;
  *error0 = e0;
  *flags0 = l0;
}

always_inline void
fge_rx_next_and_error_from_status_x2 (fge_dma_descriptor_t * d0,
				      fge_dma_descriptor_t * d1,
				      fge_offload_header_t * o0,
				      fge_offload_header_t * o1,
				      u8 * next0, u8 * error0, u32 * flags0,
				      u8 * next1, u8 * error1, u32 * flags1)
{
  u8 n0, e0, is_ip0, is_ip60;
  u8 n1, e1, is_ip1, is_ip61;
  u16 f0, s0, f1, s1;
  u32 l0, l1;

  e0 = e1 = FGE_ERROR_none;
  n0 = n1 = FGE_RX_NEXT_ETHERNET_INPUT;

  f0 = o0->flags;
  f1 = o1->flags;

  is_ip0 = (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_OR_IP6) != 0;
  is_ip1 = (f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_OR_IP6) != 0;

  is_ip60 = (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP6) != 0;
  is_ip61 = (f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP6) != 0;
  
  n0 = is_ip0 && is_ip60 ? FGE_RX_NEXT_IP6_INPUT : n0;
  n1 = is_ip1 && is_ip61 ? FGE_RX_NEXT_IP6_INPUT : n1;

  n0 = is_ip0 && ! is_ip60 ? FGE_RX_NEXT_IP4_INPUT : n0;
  n1 = is_ip1 && ! is_ip61 ? FGE_RX_NEXT_IP4_INPUT : n1;

  e0 = (((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_COMPUTED)
	 && (f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_ERROR))
	? FGE_ERROR_ip4_checksum_error
	: e0);
  e1 = (((f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_COMPUTED)
	 && (f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_ERROR))
	? FGE_ERROR_ip4_checksum_error
	: e1);

  l0 = ((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_COMPUTED)
	? IP_BUFFER_L4_CHECKSUM_COMPUTED
	: 0);
  l1 = ((f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_COMPUTED)
	? IP_BUFFER_L4_CHECKSUM_COMPUTED
	: 0);

  l0 |= ((f0 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_ERROR)
	 ? 0
	 : IP_BUFFER_L4_CHECKSUM_CORRECT);
  l1 |= ((f1 & FGE_OFFLOAD_HEADER_FLAGS_IS_TCP_UDP_CHECKSUM_ERROR)
	 ? 0
	 : IP_BUFFER_L4_CHECKSUM_CORRECT);

  s0 = d0->status;
  s1 = d1->status;

  /* Classify rx error bits [5:0] as "data error". */
  e0 = (s0 & 0x3f) != 0 ? FGE_ERROR_rx_data_error : e0;
  e1 = (s1 & 0x3f) != 0 ? FGE_ERROR_rx_data_error : e1;

  n0 = e0 != FGE_ERROR_none ? FGE_RX_NEXT_DROP : n0;
  n1 = e1 != FGE_ERROR_none ? FGE_RX_NEXT_DROP : n1;

  *next0 = n0; *error0 = e0; *flags0 = l0;
  *next1 = n1; *error1 = e1; *flags1 = l1;
}

typedef struct {
  fge_dma_descriptor_t before, after;

  fge_offload_header_t offload;

  u32 buffer_index;

  u16 device_index;

  u8 queue_index;

  u8 is_start_of_packet;

  /* Copy of VLIB buffer; packet data stored in pre_data. */
  vlib_buffer_t buffer;
} fge_rx_dma_trace_t;

static u8 * format_fge_rx_dma_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  vlib_node_t * node = va_arg (*va, vlib_node_t *);
  vnet_main_t * vnm = &vnet_main;
  fge_rx_dma_trace_t * t = va_arg (*va, fge_rx_dma_trace_t *);
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, t->device_index);
  format_function_t * f;
  uword indent = format_get_indent (s);

  {
    vnet_sw_interface_t * sw = vnet_get_sw_interface (vnm, fd->vnet_sw_if_index);
    s = format (s, "%U rx queue %d",
		format_vnet_sw_interface_name, vnm, sw,
		t->queue_index);
  }

  s = format (s, "\n%Ubefore: %U",
	      format_white_space, indent,
	      format_fge_rx_or_tx_descriptor, &t->before, VLIB_RX, /* is_written_back */ 0);
  s = format (s, "\n%Uafter: %U",
	      format_white_space, indent,
	      format_fge_rx_or_tx_descriptor, &t->before, VLIB_RX, /* is_written_back */ 0);

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

always_inline void
fge_rx_trace (fge_main_t * fm,
	      fge_device_t * fd,
	      fge_dma_queue_t * dq,
	      fge_dma_descriptor_t * before_descriptors,
	      u32 * before_buffers,
	      fge_dma_descriptor_t * after_descriptors,
	      uword n_descriptors)
{
  vlib_main_t * vm = &vlib_global_main;
  vlib_node_runtime_t * node = dq->rx.node;
  fge_dma_descriptor_t * bd;
  fge_dma_descriptor_t * ad;
  u32 * b, n_left, is_sop, next_index_sop;

  n_left = n_descriptors;
  b = before_buffers;
  bd = before_descriptors;
  ad = after_descriptors;
  is_sop = dq->rx.is_start_of_packet;
  next_index_sop = dq->rx.saved_start_of_packet_next_index;

  while (n_left >= 2)
    {
      u32 bi0, bi1, flags0, flags1;
      vlib_buffer_t * b0, * b1;
      fge_offload_header_t * o0, * o1;
      fge_rx_dma_trace_t * t0, * t1;
      u8 next0, error0, next1, error1;

      bi0 = b[0];
      bi1 = b[1];
      n_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      o0 = (void *) b0->data;
      o1 = (void *) b1->data;

      fge_rx_next_and_error_from_status_x2 (&bd[0], &bd[1],
					    o0, o1,
					    &next0, &error0, &flags0,
					    &next1, &error1, &flags1);

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
      t0->device_index = fd->device_index;
      t1->device_index = fd->device_index;
      t0->before = bd[0];
      t1->before = bd[1];
      t0->after = ad[0];
      t1->after = ad[1];
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
      u32 bi0, flags0;
      vlib_buffer_t * b0;
      fge_offload_header_t * o0;
      fge_rx_dma_trace_t * t0;
      u8 next0, error0;

      bi0 = b[0];
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      o0 = (void *) b0->data;

      fge_rx_next_and_error_from_status_x1 (&bd[0], o0,
					    &next0, &error0, &flags0);

      next_index_sop = is_sop ? next0 : next_index_sop;
      vlib_trace_buffer (vm, node, next_index_sop, b0, /* follow_chain */ 0);
      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->is_start_of_packet = is_sop;
      is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      t0->queue_index = dq->queue_index;
      t0->device_index = fd->device_index;
      t0->before = bd[0];
      t0->after = ad[0];
      t0->buffer_index = bi0;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));

      b += 1;
      bd += 1;
      ad += 1;
    }
}

typedef struct {
  fge_dma_descriptor_t descriptor;

  u32 buffer_index;

  u16 device_index;

  u8 queue_index;

  u8 is_start_of_packet;

  /* Copy of VLIB buffer; packet data stored in pre_data. */
  vlib_buffer_t buffer;
} fge_tx_dma_trace_t;

static u8 * format_fge_tx_dma_trace (u8 * s, va_list * va)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  vnet_main_t * vnm = &vnet_main;
  fge_tx_dma_trace_t * t = va_arg (*va, fge_tx_dma_trace_t *);
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, t->device_index);
  format_function_t * f;
  uword indent = format_get_indent (s);

  {
    vnet_sw_interface_t * sw = vnet_get_sw_interface (vnm, fd->vnet_sw_if_index);
    s = format (s, "%U tx queue %d",
		format_vnet_sw_interface_name, vnm, sw,
		t->queue_index);
  }

  s = format (s, "\n%Udescriptor: %U",
	      format_white_space, indent,
	      format_fge_rx_or_tx_descriptor, &t->descriptor, VLIB_TX, /* is_written_back */ 0);

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

static void
fge_tx_trace (fge_main_t * fm,
	      fge_device_t * fd,
	      fge_dma_queue_t * dq,
	      fge_dma_descriptor_t * descriptors,
	      u32 * buffers,
	      uword n_descriptors)
{
  vlib_main_t * vm = &vlib_global_main;
  vlib_node_runtime_t * node = dq->saved_node;
  fge_dma_descriptor_t * d;
  u32 * b, n_left, is_sop;

  n_left = n_descriptors;
  b = buffers;
  d = descriptors;
  is_sop = dq->is_start_of_packet;

  while (n_left >= 2)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      fge_tx_dma_trace_t * t0, * t1;

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
      t0->device_index = fd->device_index;
      t1->device_index = fd->device_index;
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
      fge_tx_dma_trace_t * t0;

      bi0 = b[0];
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t0->is_start_of_packet = is_sop;
      is_sop = (b0->flags & VLIB_BUFFER_NEXT_PRESENT) == 0;

      t0->queue_index = dq->queue_index;
      t0->device_index = fd->device_index;
      t0->descriptor = d[0];
      t0->buffer_index = bi0;
      memcpy (&t0->buffer, b0, sizeof (b0[0]) - sizeof (b0->pre_data));
      memcpy (t0->buffer.pre_data, b0->data, sizeof (t0->buffer.pre_data));

      b += 1;
      d += 1;
    }
}

static uword
fge_tx_no_wrap (fge_main_t * fm,
		fge_device_t * fd,
		fge_dma_queue_t * dq,
		u32 * buffers,
		u32 start_descriptor_index,
		u32 n_descriptors)
{
  vlib_main_t * vm = &vlib_global_main;
  fge_dma_descriptor_t * d;
  u32 n_left = n_descriptors;
  u32 * to_free = vec_end (fm->tx_buffers_pending_free);
  u32 * to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 d_status = dq->descriptor_status;
  u32 is_sop = dq->is_start_of_packet;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index];

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

      /* Descriptors should be software owned (e.g. TX of buffer from previous
	 time around ring should be complete now). */
      ASSERT (! (d[0].status & FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE));
      ASSERT (! (d[1].status & FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE));

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

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;
      d[1].buffer_address = vlib_get_buffer_data_physical_address (vm, bi1) + b1->current_data;

      d[0].n_bytes_this_buffer_or_packet = len0;
      d[1].n_bytes_this_buffer_or_packet = len1;

      d[0].status = d_status | (is_eop0 << FGE_DMA_DESCRIPTOR_LOG2_IS_END_OF_PACKET);

      /* Descriptors after first become owned by hardware. */
      d_status |= FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;

      d[1].status = d_status | (is_eop1 << FGE_DMA_DESCRIPTOR_LOG2_IS_END_OF_PACKET);

      d += 2;
      is_sop = is_eop1;
    }

  while (n_left > 0)
    {
      vlib_buffer_t * b0;
      u32 bi0, fi0, len0;
      u8 is_eop0;

      ASSERT (! (d[0].status & FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE));

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

      d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, bi0) + b0->current_data;

      d[0].n_bytes_this_buffer_or_packet = len0;

      d[0].status = d_status | (is_eop0 << FGE_DMA_DESCRIPTOR_LOG2_IS_END_OF_PACKET);

      /* Descriptors after first become owned by hardware. */
      d_status |= FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;

      d += 1;
      is_sop = is_eop0;
    }

  if (dq->saved_node->flags & VLIB_NODE_FLAG_TRACE)
    {
      to_tx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
      fge_tx_trace (fm, fd, dq,
		    &dq->descriptors[start_descriptor_index],
		    to_tx,
		    n_descriptors);
    }

  _vec_len (fm->tx_buffers_pending_free) = to_free - fm->tx_buffers_pending_free;

  dq->is_start_of_packet = is_sop;
  dq->descriptor_status = d_status;

  return n_descriptors;
}

static uword
fge_interface_tx (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * f)
{
  fge_main_t * fm = &fge_main;
  vnet_interface_output_runtime_t * rd = (void *) node->runtime_data;
  fge_device_t * fd = vec_elt_at_index (fm->devices, rd->dev_instance);
  fge_regs_t * r = fd->regs;
  fge_dma_queue_t * dq;
  u32 * from, sw_index, hw_index, n_left_on_ring, n_descriptors_to_tx, n_tail_drop;
  u32 queue_index = 0;		/* fixme parameter */

  dq->saved_node = node;
  dq->is_start_of_packet = 1;

  /* First descriptor we write will not have IS_OWNED_BY_HARDWARE bit set.
     We'll set it when we're done munging descriptors. */
  dq->descriptor_status = 0;
  
  from = vlib_frame_vector_args (f);

  dq = vec_elt_at_index (fd->dma_queues[VLIB_TX], queue_index);

  hw_index = ((r->tx.next_descriptor_address[queue_index][1]
	       - dq->descriptors_physical_address_low_32bits)
	      / sizeof (dq->descriptors[0]));
  sw_index = dq->sw_index;

  /* Compute number of slots that remain on TX ring.
     Never completely fill up ring so that sw_index == hw_index
     implies ring is empty (otherwise there would be an ambiguity between
     ring full and empty). */
  n_left_on_ring = dq->n_descriptors - 1;

  {
    word d = sw_index - hw_index;
    n_left_on_ring -= d < 0 ? -d : d;
  }

  _vec_len (fm->tx_buffers_pending_free) = 0;

  n_descriptors_to_tx = f->n_vectors;
  n_tail_drop = 0;
  if (PREDICT_FALSE (n_descriptors_to_tx > n_left_on_ring))
    {
      i32 i, n_ok, i_eop, i_sop;

      i_sop = i_eop = ~0;
      for (i = n_left_on_ring - 1; i >= 0; i--)
	{
	  vlib_buffer_t * b = vlib_get_buffer (vm, from[i]);
	  if (! (b->flags & VLIB_BUFFER_NEXT_PRESENT))
	    {
	      if (i_sop != ~0 && i_eop != ~0)
		break;
	      i_eop = i;
	      i_sop = i + 1;
	    }
	}
      if (i == 0)
	n_ok = 0;
      else
	n_ok = i_eop + 1;

      {
	ELOG_TYPE_DECLARE (e) = {
	  .function = (char *) __FUNCTION__,
	  .format = "fge %d, ring full to tx %d sw %d hw %d",
	  .format_args = "i2i2i2i2",
	};
	struct { u16 instance, to_tx, sw, hw; } * ed;
	ed = ELOG_DATA (&vm->elog_main, e);
	ed->instance = fd->device_index;
	ed->to_tx = n_descriptors_to_tx;
	ed->sw = sw_index;
	ed->hw = hw_index;
      }

      if (n_ok < n_descriptors_to_tx)
	{
	  n_tail_drop = n_descriptors_to_tx - n_ok;
	  vec_add (fm->tx_buffers_pending_free, from + n_ok, n_tail_drop);
	  vlib_error_count (vm, fge_input_node.index, FGE_ERROR_tx_full_drops, n_tail_drop);
	}

      n_descriptors_to_tx = n_ok;
    }

  /* Process from tail to end of descriptor ring. */
  if (n_descriptors_to_tx > 0 && sw_index < dq->n_descriptors)
    {
      u32 n = clib_min (dq->n_descriptors - sw_index, n_descriptors_to_tx);
      n = fge_tx_no_wrap (fm, fd, dq, from, sw_index, n);
      from += n;
      n_descriptors_to_tx -= n;
      sw_index += n;
      ASSERT (sw_index <= dq->n_descriptors);
      if (sw_index == dq->n_descriptors)
	sw_index = 0;
    }

  if (n_descriptors_to_tx > 0)
    {
      u32 n = clib_min (dq->n_descriptors - sw_index, n_descriptors_to_tx);
      n = fge_tx_no_wrap (fm, fd, dq, from, 0, n);
      from += n;
      sw_index += n;
      n_descriptors_to_tx -= n;
      ASSERT (sw_index <= dq->n_descriptors);
      if (sw_index == dq->n_descriptors)
	sw_index = 0;
    }

  /* We should only get full packets. */
  ASSERT (dq->is_start_of_packet);

  /* Set wrap bit on last descriptor in ring. */
  {
    fge_dma_descriptor_t * d = dq->descriptors + dq->n_descriptors - 1;
    d->status |= FGE_DMA_DESCRIPTOR_IS_LAST_IN_RING;
  }

  /* Give new descriptors to hardware. */
  CLIB_MEMORY_BARRIER ();

  /* Set owned by hardware bit on first descriptor.  This should
     start up the hardware in case it was stalled waiting for a descriptor. */
  {
    fge_dma_descriptor_t * d = dq->descriptors + dq->sw_index;
    d->status |= FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;
  }

  dq->sw_index = sw_index;

  /* Clear halt bit for this TX queue. */
  r->tx.status |= 1 << (31 - queue_index);

  /* Free any buffers that are done. */
  {
    u32 n = _vec_len (fm->tx_buffers_pending_free);
    if (n > 0)
      {
	vlib_buffer_free_no_next (vm, fm->tx_buffers_pending_free, n);
	_vec_len (fm->tx_buffers_pending_free) = 0;
      }
  }

  return f->n_vectors;
}

static uword
fge_rx_queue_no_wrap (fge_main_t * fm,
		      fge_device_t * fd,
		      fge_dma_queue_t * dq,
		      u32 start_descriptor_index,
		      u32 n_descriptors)
{
  vlib_main_t * vm = &vlib_global_main;
  vlib_node_runtime_t * node = dq->rx.node;
  fge_dma_descriptor_t * d;
  static fge_dma_descriptor_t * d_trace_save;
  static u32 * d_trace_buffers;
  u32 n_descriptors_left = n_descriptors;
  u32 * to_rx = vec_elt_at_index (dq->descriptor_buffer_indices, start_descriptor_index);
  u32 * to_add;
  u32 bi_sop = dq->rx.saved_start_of_packet_buffer_index;
  u32 bi_last = dq->rx.saved_last_buffer_index;
  u32 next_index_sop = dq->rx.saved_start_of_packet_next_index;
  u32 d_status = dq->descriptor_status;
  u32 is_sop = dq->is_start_of_packet;
  u32 next_index, n_left_to_next, * to_next;
  u32 n_packets = 0;
  u32 n_bytes = 0;
  u32 n_trace = vlib_get_trace_count (vm, node);
  vlib_buffer_t * b_last, b_dummy;

  ASSERT (start_descriptor_index + n_descriptors <= dq->n_descriptors);
  d = &dq->descriptors[start_descriptor_index];

  b_last = bi_last != ~0 ? vlib_get_buffer (vm, bi_last) : &b_dummy;
  next_index = dq->rx.next_index;

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
    uword l = vec_len (fm->rx_buffers_to_add);

    if (l < n_descriptors_left)
      {
	u32 n_to_alloc = 2 * dq->n_descriptors - l;
	u32 n_allocated;

	vec_validate (fm->rx_buffers_to_add, n_to_alloc + l - 1);

	_vec_len (fm->rx_buffers_to_add) = l;
	n_allocated = vlib_buffer_alloc_from_free_list
	  (vm, fm->rx_buffers_to_add + l, n_to_alloc,
	   fm->vlib_buffer_free_list_index);
	_vec_len (fm->rx_buffers_to_add) += n_allocated;
	ASSERT (vec_len (fm->rx_buffers_to_add) >= n_descriptors_left);
      }

    /* Add buffers from end of vector going backwards. */
    to_add = vec_end (fm->rx_buffers_to_add) - 1;
  }

  while (n_descriptors_left > 0)
    {
      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

#if 0
      while (n_descriptors_left >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * b0, * b1, * b2, * b3;
	  fge_offload_header_t * o0, * o1;
	  u32 bi0, fi0, len0, l3_offset0, s00, flags0;
	  u32 bi1, fi1, len1, l3_offset1, s01, flags1;
	  u8 is_eop0, is_vlan0, error0, next0;
	  u8 is_eop1, is_vlan1, error1, next1;

	  b2 = vlib_get_buffer (vm, to_rx[2]);
	  b3 = vlib_get_buffer (vm, to_rx[2]);
	  CLIB_PREFETCH (b2, CLIB_CACHE_LINE_BYTES, STORE);
	  CLIB_PREFETCH (b3, CLIB_CACHE_LINE_BYTES, STORE);
	  CLIB_PREFETCH (b2->data, CLIB_CACHE_LINE_BYTES, LOAD);
	  CLIB_PREFETCH (b3->data, CLIB_CACHE_LINE_BYTES, LOAD);
	  CLIB_PREFETCH (d + 4, CLIB_CACHE_LINE_BYTES, LOAD);

	  s00 = d[0].status;
	  s01 = d[1].status;
	  if ((s00 | s01) & FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE)
	    goto found_hw_owned_descriptor_x2;

	  bi0 = to_rx[0];
	  bi1 = to_rx[1];

	  ASSERT (to_add - 1 >= fm->rx_buffers_to_add);
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

	  o0 = (void *) b0->data;
	  o1 = (void *) b1->data;

	  is_eop0 = (s00 & FGE_DMA_DESCRIPTOR_IS_END_OF_PACKET);
	  is_eop1 = (s01 & FGE_DMA_DESCRIPTOR_IS_END_OF_PACKET);

	  fge_rx_next_and_error_from_status_x2 (&d[0], &d[1],
						o0, o1,
						&next0, &error0, &flags0,
						&next1, &error1, &flags1);

	  len0 = d[0].n_bytes_this_buffer_or_packet;
	  len1 = d[1].n_bytes_this_buffer_or_packet;

	  next0 = is_sop ? next0 : next_index_sop;
	  next1 = is_eop0 ? next1 : next0;
	  next_index_sop = next1;

	  b0->flags |= flags0 | (!is_eop0 << VLIB_BUFFER_LOG2_NEXT_PRESENT);
	  b1->flags |= flags1 | (!is_eop1 << VLIB_BUFFER_LOG2_NEXT_PRESENT);

	  vnet_buffer (b0)->sw_if_index[VLIB_RX] = fd->vnet_sw_if_index;
	  vnet_buffer (b1)->sw_if_index[VLIB_RX] = fd->vnet_sw_if_index;

	  b0->error = node->errors[error0];
	  b1->error = node->errors[error1];

	  n_bytes += len0 + len1;
	  n_packets += is_eop0 + is_eop1;

	  /* Give new buffers to hardware.  Works also for legacy descriptors. */
	  d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, fi0);
	  d[1].buffer_address = vlib_get_buffer_data_physical_address (vm, fi1);
	  /* Buffers must be 64 byte aligned. */
	  ASSERT ((d[0].buffer_address % 64) == 0);
	  ASSERT ((d[1].buffer_address % 64) == 0);

	  d += 2;
	  n_descriptors_left -= 2;

	  /* Always skip offload header. */
	  l3_offset0 = l3_offset1 = sizeof (fge_offload_header_t);

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = (is_sop && next0 == FGE_RX_NEXT_IP4_INPUT
			? sizeof (ethernet_header_t) + (is_vlan0 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);
	  l3_offset1 = (is_eop0 && next1 == FGE_RX_NEXT_IP4_INPUT
			? sizeof (ethernet_header_t) + (is_vlan1 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);

	  b0->current_length = len0 - l3_offset0;
	  b1->current_length = len1 - l3_offset1;
	  b0->current_data = l3_offset0;
	  b1->current_data = l3_offset1;

	  b_last->next_buffer = is_sop ? ~0 : bi0;
	  b0->next_buffer = is_eop0 ? ~0 : bi1;
	  bi_last = bi1;
	  b_last = b1;

	  if (CLIB_DEBUG > 0)
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
#endif

      while (n_descriptors_left > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * b0;
	  fge_offload_header_t * o0;
	  u32 bi0, fi0, len0, len_eop0, l3_offset0, s00, flags0;
	  u8 is_eop0, is_vlan0, error0, next0;

	  s00 = d[0].status;
	  if (s00 & FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE)
	    goto found_hw_owned_descriptor_x1;

	  bi0 = to_rx[0];
	  ASSERT (to_add >= fm->rx_buffers_to_add);
	  fi0 = to_add[0];

	  to_rx[0] = fi0;
	  to_rx += 1;
	  to_add -= 1;

	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, bi0));
	  ASSERT (VLIB_BUFFER_KNOWN_ALLOCATED == vlib_buffer_is_known (vm, fi0));

	  b0 = vlib_get_buffer (vm, bi0);

	  o0 = (void *) b0->data;

	  is_eop0 = (s00 & FGE_DMA_DESCRIPTOR_IS_END_OF_PACKET);

	  fge_rx_next_and_error_from_status_x1 (&d[0],
						o0,
						&next0, &error0, &flags0);

	  next0 = is_sop ? next0 : next_index_sop;
	  next_index_sop = next0;

	  b0->flags |= flags0 | (!is_eop0 << VLIB_BUFFER_LOG2_NEXT_PRESENT);

	  vnet_buffer (b0)->sw_if_index[VLIB_RX] = fd->vnet_sw_if_index;

	  b0->error = node->errors[error0];

	  len0 = d[0].n_bytes_this_buffer_or_packet;

	  /* Last buffer gets length of packet. */
	  len_eop0 = (len0 - (sizeof (o0[0]) + sizeof (u32))) % FGE_RX_BUFFER_N_BYTES;

	  len_eop0 = len_eop0 == 0 ? FGE_RX_BUFFER_N_BYTES : len_eop0;

	  len0 = is_eop0 ? len_eop0 : len0;

	  n_bytes += len0;
	  n_packets += is_eop0;

	  /* Give new buffers to hardware.  Works also for legacy descriptors. */
	  d[0].buffer_address = vlib_get_buffer_data_physical_address (vm, fi0);
	  /* Buffers must be 64 byte aligned. */
	  ASSERT ((d[0].buffer_address % 64) == 0);

	  d[0].status |= d_status;

	  /* First descriptor is empty but subsequent ones are owned by hardware. */
	  d_status |= FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;

	  d += 1;
	  n_descriptors_left -= 1;

	  /* Point to either l2 or l3 header depending on next. */
	  l3_offset0 = sizeof (o0[0]);
	  l3_offset0 += (is_sop && next0 != FGE_RX_NEXT_ETHERNET_INPUT
			? sizeof (ethernet_header_t) + (is_vlan0 ? sizeof (ethernet_vlan_header_t) : 0)
			: 0);

	  b0->current_length = len0 - l3_offset0;
	  b0->current_data = l3_offset0;

	  b_last->next_buffer = is_sop ? ~0 : bi0;
	  bi_last = bi0;
	  b_last = b0;

	  bi_sop = is_sop ? bi0 : bi_sop;

	  if (CLIB_DEBUG > 0 && is_eop0)
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

  _vec_len (fm->rx_buffers_to_add) = (to_add + 1) - fm->rx_buffers_to_add;

  {
    u32 n_done = n_descriptors - n_descriptors_left;

    if (n_trace > 0 && n_done > 0)
      {
	u32 n = clib_min (n_trace, n_done);
	fge_rx_trace (fm, fd, dq,
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

    /* Don't keep a reference to b_last if we don't have to.
       Otherwise we can over-write a next_buffer pointer after already haven
       enqueued a packet. */
    if (is_sop)
      {
        b_last->next_buffer = ~0;
        bi_last = ~0;
      }

    dq->is_start_of_packet = is_sop;
    dq->descriptor_status = d_status;
    dq->rx.n_descriptors_done_this_call = n_done;
    dq->rx.n_descriptors_done_total += n_done;
    dq->rx.saved_start_of_packet_buffer_index = bi_sop;
    dq->rx.saved_last_buffer_index = bi_last;
    dq->rx.saved_start_of_packet_next_index = next_index_sop;
    dq->rx.next_index = next_index;
    dq->rx.n_bytes_total += n_bytes;

    return n_packets;
  }
}

static uword
fge_rx_queue (fge_main_t * fm,
	      fge_device_t * fd,
	      vlib_node_runtime_t * node,
	      u32 queue_index)
{
  fge_dma_queue_t * dq = vec_elt_at_index (fd->dma_queues[VLIB_RX], queue_index);
  fge_regs_t * r = fd->regs;
  uword n_packets = 0;
  u32 sw_index, hw_index;

  /* One time initialization. */
  if (! dq->rx.node)
    {
      dq->rx.node = node;
      dq->rx.is_start_of_packet = 1;
      dq->rx.saved_start_of_packet_buffer_index = ~0;
      dq->rx.saved_last_buffer_index = ~0;
    }

  dq->rx.next_index = node->cached_next_index;

  dq->rx.n_descriptors_done_total = 0;
  dq->rx.n_descriptors_done_this_call = 0;
  dq->rx.n_bytes_total = 0;

  /* Fetch head from hardware and compare to where we think we are. */
  hw_index = ((r->rx.next_descriptor_address[queue_index][1]
	       - dq->descriptors_physical_address_low_32bits)
	      / sizeof (dq->descriptors[0]));
  sw_index = dq->sw_index;

  /* No descriptors to process? */
  if (hw_index == sw_index)
    goto done;

  /* Try cleaning ring from current sw index to end of ring. */
  if (hw_index < sw_index)
    {
      u32 n_tried = dq->n_descriptors - sw_index;
      n_packets += fge_rx_queue_no_wrap (fm, fd, dq, sw_index, n_tried);
      sw_index += dq->rx.n_descriptors_done_this_call;
      if (sw_index == dq->n_descriptors)
	sw_index = 0;
      if (dq->rx.n_descriptors_done_this_call != n_tried)
	goto done;
    }

  /* Clean ring from start of ring to current hw index. */
  if (hw_index >= sw_index)
    {
      u32 n_tried = hw_index - sw_index;
      n_packets += fge_rx_queue_no_wrap (fm, fd, dq, sw_index, n_tried);
      sw_index += dq->rx.n_descriptors_done_this_call;
      if (sw_index == dq->n_descriptors)
	sw_index = 0;
    }

 done:
  /* Set wrap bit on last descriptor in ring. */
  {
    fge_dma_descriptor_t * d = dq->descriptors + dq->n_descriptors - 1;
    d->status |= FGE_DMA_DESCRIPTOR_IS_LAST_IN_RING;
  }

  /* Give new descriptors to hardware. */
  CLIB_MEMORY_BARRIER ();

  /* Set owned by hardware bit on first descriptor.  This should
     start up the hardware in case it was stalled waiting for a descriptor. */
  {
    fge_dma_descriptor_t * d = dq->descriptors + dq->sw_index;
    d->status |= FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;
  }

  dq->sw_index = sw_index;

  vlib_increment_combined_counter (vnet_main.interface_main.combined_sw_if_counters
				   + VNET_INTERFACE_COUNTER_RX,
				   fd->vnet_sw_if_index,
				   n_packets,
				   dq->rx.n_bytes_total);

  return n_packets;
}

static void fge_interrupt (fge_main_t * fm, fge_device_t * fd, u32 i)
{
  ASSERT (0);
}

static uword
fge_device_input (fge_main_t * fm,
		  fge_device_t * fd,
		  vlib_node_runtime_t * node)
{
  fge_regs_t * r = fd->regs;
  u32 i, s;
  uword n_rx_packets = 0;

  s = r->main.interrupt_status_write_1_to_clear;
  r->main.interrupt_status_write_1_to_clear = s;
  foreach_set_bit (i, s, ({
    if (i == 7)
      n_rx_packets += fge_rx_queue (fm, fd, node, 0);
    else
      fge_interrupt (fm, fd, i);
  }));

  return n_rx_packets;
}

static uword
fge_input (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * f)
{
  fge_main_t * fm = &fge_main;
  fge_device_t * fd;
  uword n_rx_packets = 0;

  if (node->state == VLIB_NODE_STATE_INTERRUPT)
    {    
      uword i;

      /* Loop over devices with interrupts. */
      foreach_set_bit (i, node->runtime_data[0], ({
	fd = vec_elt_at_index (fm->devices, i);
	n_rx_packets += fge_device_input (fm, fd, node);

	/* Re-enable interrupts since we're going to stay in interrupt mode. */
	if (! (node->flags & VLIB_NODE_FLAG_SWITCH_FROM_INTERRUPT_TO_POLLING_MODE))
	  fd->regs->main.interrupt_enable = ~0;
      }));

      /* Clear mask of devices with pending interrupts. */
      node->runtime_data[0] = 0;
    }
  else
    {
      /* Poll all devices for input/interrupts. */
      vec_foreach (fd, fm->devices)
	{
	  n_rx_packets += fge_device_input (fm, fd, node);

	  /* Re-enable interrupts when switching out of polling mode. */
	  if (node->flags & VLIB_NODE_FLAG_SWITCH_FROM_POLLING_TO_INTERRUPT_MODE)
	    fd->regs->main.interrupt_enable = ~0;
	}
    }

  return n_rx_packets;
}

static char * fge_error_strings[] = {
#define _(n,s) s,
  foreach_fge_error
#undef _
};

static VLIB_REGISTER_NODE (fge_input_node) = {
  .function = fge_input,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "fge-input",

  /* Will be enabled if/when hardware is detected. */
  .state = VLIB_NODE_STATE_DISABLED,

  .format_buffer = format_ethernet_header_with_length,
  .format_trace = format_fge_rx_dma_trace,

  .n_errors = FGE_N_ERROR,
  .error_strings = fge_error_strings,

  .n_next_nodes = FGE_RX_N_NEXT,
  .next_nodes = {
    [FGE_RX_NEXT_DROP] = "error-drop",
    [FGE_RX_NEXT_ETHERNET_INPUT] = "ethernet-input",
    [FGE_RX_NEXT_IP4_INPUT] = "ip4-input-no-checksum",
    [FGE_RX_NEXT_IP6_INPUT] = "ip6-input",
  },
};

static u8 * format_fge_device_name (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, i);
  return format (s, "GigabitEthernet%d", fd->device_index);
}

static void fge_update_counters (fge_device_t * fd)
{
  fge_regs_t * r = fd->regs;
  int i;

  for (i = 0; i < ARRAY_LEN (r->counters.values); i++)
    fd->counters[i] += r->counters.values[i];
}

static u8 * format_fge_device (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, dev_instance);
  ethernet_phy_t * phy = &fd->phy;
  uword indent = format_get_indent (s);

  fge_update_counters (fd);

  s = format (s, "Freescale 85xx ethernet\n%U%U",
	      format_white_space, indent + 2,
	      format_ethernet_media, &phy->media);

  {
    u32 i;
    u64 v;
    static char * names[] = {
#define _(f) #f,
      foreach_fge_counter
#undef _
    };

    for (i = 0; i < ARRAY_LEN (names); i++)
      {
	v = fd->counters[i] - fd->counters_last_clear[i];
	if (v != 0)
	  s = format (s, "\n%U%-40U%16Ld",
		      format_white_space, indent + 2,
		      format_c_identifier, names[i],
		      v);
      }
  }

  return s;
}

static void fge_clear_hw_interface_counters (u32 instance)
{
  fge_main_t * fm = &fge_main;
  fge_device_t * fd = vec_elt_at_index (fm->devices, instance);
  fge_update_counters (fd);
  memcpy (fd->counters_last_clear, fd->counters, sizeof (fd->counters));
}

VNET_DEVICE_CLASS (fge_device_class) = {
  .name = "fge",
  .tx_function = fge_interface_tx,
  .format_device_name = format_fge_device_name,
  .format_device = format_fge_device,
  .format_tx_trace = format_fge_tx_dma_trace,
  .clear_counters = fge_clear_hw_interface_counters,
  .admin_up_down_function = fge_interface_admin_up_down,
};

static clib_error_t *
fge_dma_init (fge_device_t * fd, vlib_rx_or_tx_t rt, u32 queue_index)
{
  fge_main_t * fm = &fge_main;
  vlib_main_t * vm = &vlib_global_main;
  fge_dma_queue_t * dq;
  clib_error_t * error = 0;

  vec_validate (fd->dma_queues[rt], queue_index);
  dq = vec_elt_at_index (fd->dma_queues[rt], queue_index);

  fm->vlib_buffer_free_list_index
    = vlib_buffer_get_or_create_free_list (vm, FGE_RX_BUFFER_N_BYTES, "fge rx");

  if (! fm->n_descriptors[rt])
      fm->n_descriptors[rt] = 3 * VLIB_FRAME_SIZE / 2;

  dq->queue_index = queue_index;
  dq->n_descriptors = round_pow2 (fm->n_descriptors[rt], 64 / sizeof (dq->descriptors[0]));
  dq->sw_index = 0;

  /* Descriptors must be 64 byte aligned; hardware limit. */
  dq->descriptors = vlib_physmem_alloc_aligned (vm, &error,
						dq->n_descriptors * sizeof (dq->descriptors[0]),
						64);
  if (error)
    return error;

  memset (dq->descriptors, 0, dq->n_descriptors * sizeof (dq->descriptors[0]));
  vec_resize (dq->descriptor_buffer_indices, dq->n_descriptors);

  if (rt == VLIB_RX)
    {
      u32 n_alloc, i;

      n_alloc = vlib_buffer_alloc_from_free_list
	(vm, dq->descriptor_buffer_indices, vec_len (dq->descriptor_buffer_indices),
	 fm->vlib_buffer_free_list_index);
      ASSERT (n_alloc == vec_len (dq->descriptor_buffer_indices));
      for (i = 0; i < n_alloc; i++)
	{
	  vlib_buffer_t * b = vlib_get_buffer (vm, dq->descriptor_buffer_indices[i]);
	  dq->descriptors[i].buffer_address = vlib_physmem_virtual_to_physical (vm, b->data);
	  dq->descriptors[i].status = FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE;
	}
    }
  else
    vec_validate (fm->tx_buffers_pending_free, dq->n_descriptors - 1);

  {
    fge_regs_t * r = fd->regs;
    u64 a;
    u32 b[2];

    a = vlib_physmem_virtual_to_physical (vm, dq->descriptors);
    b[0] = (u64) a >> (u64) 32;
    b[1] = (u64) a >> (u64) 0;

    if (rt == VLIB_RX)
      {
	r->rx.base_descriptor_address[queue_index][0] = b[0];
	r->rx.base_descriptor_address[queue_index][1] = b[1];
      }
    else
      {
	r->tx.base_descriptor_address[queue_index][0] = b[0];
	r->tx.base_descriptor_address[queue_index][1] = b[1];
      }

    dq->sw_index = 0;

    CLIB_MEMORY_BARRIER ();
  }

  return error;
}

static void fge_device_init (fge_main_t * fm)
{
  vnet_main_t * vnm = &vnet_main;
  vlib_main_t * vm = &vlib_global_main;
  fge_device_t * fd;
    
  /* Reset chip(s). */
  vec_foreach (fd, fm->devices)
    {
      fge_regs_t * r = fd->regs;

      /* Soft reset mac. */
      r->mac.config[0] |= 1 << 31;
      r->mac.config[0] &= ~(1 << 31);

      /* Clear counters, enable clear-on-read, enable counters. */
      r->main.control |= (1 << 14) | (1 << 13) | (1 << 12);

      /* FIXME other bits in main.control from e.g. eeprom. */

      /* Don't poll TX descriptors. */
      r->main.dma_control |= 1 << 0;

      fd->phy.opaque = fd->device_index;
      fd->phy.read_write = fge_read_write_phy_reg;
      fd->phy.vlib_main = vm;
      ethernet_phy_reset (&fd->phy);
      ethernet_phy_init (&fd->phy);
      ethernet_phy_negotiate_media (&fd->phy);

      /* Register ethernet interface. */
      {
	u8 addr8[6];
	u32 i, addr32[2];
	clib_error_t * error;

	addr32[0] = r->mac.ethernet_address_exact_match[0][0];
	addr32[1] = r->mac.ethernet_address_exact_match[0][1];
	for (i = 0; i < 6; i++)
	  addr8[i] = addr32[i / 4] >> ((3 - (i % 4)) * 8);

	error = ethernet_register_interface
	  (vnm,
	   fge_device_class.index,
	   fd->device_index,
	   /* ethernet address */ addr8,
	   /* phy */ &fd->phy,
	   &fd->vnet_hw_if_index);
	if (error)
	  clib_error_report (error);
      }

      {
	vnet_sw_interface_t * sw = vnet_get_hw_sw_interface (vnm, fd->vnet_hw_if_index);
	fd->vnet_sw_if_index = sw->sw_if_index;
      }

      fge_dma_init (fd, VLIB_RX, /* queue_index */ 0);
      fge_dma_init (fd, VLIB_TX, /* queue_index */ 0);

      r->main.interrupt_enable = ~0;
    }
}

static uword
fge_process (vlib_main_t * vm,
	     vlib_node_runtime_t * rt,
	     vlib_frame_t * f)
{
  vnet_main_t * vnm = &vnet_main;
  fge_main_t * fm = &fge_main;
  fge_device_t * fd;
  uword event_type, * event_data = 0;
  int i;
    
  fge_device_init (fm);

  /* Clear all counters. */
  vec_foreach (fd, fm->devices)
    {
      fge_update_counters (fd);
      memset (fd->counters, 0, sizeof (fd->counters));
    }

  while (1)
    {
      /* 36 bit stat counters could overflow in ~50 secs.
	 We poll every 30 secs to be conservative. */
      vlib_process_wait_for_event_or_clock (vm, 30. /* seconds */);

      event_type = vlib_process_get_events (vm, &event_data);

      switch (event_type) {

      case EVENT_SET_FLAGS:
        for (i = 0; i < vec_len (event_data); i++) 
          {
            u32 is_up = (event_data[i] >> 31);
            u32 hw_if_index = event_data[i] & 0x7fffffff;
            
            vnet_hw_interface_set_flags 
              (vnm, hw_if_index, is_up ? VNET_HW_INTERFACE_FLAG_LINK_UP : 0);
          }
        break;

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
	if (now - fm->time_last_stats_update > 30)
	  {
	    fm->time_last_stats_update = now;
	    vec_foreach (fd, fm->devices)
	      fge_update_counters (fd);
	  }
      }
    }
	    
  return 0;
}

static vlib_node_registration_t fge_process_node = {
  .function = fge_process,
  .type = VLIB_NODE_TYPE_PROCESS,
  .name = "fge-process",
};

#if 0
static clib_error_t *
fge_pci_init (vlib_main_t * vm, pci_device_t * dev)
{
  fge_main_t * fm = &fge_main;
  clib_error_t * error;
  void * r;
  fge_device_t * fd;
  
  /* Device found: make sure we have dma memory. */
  error = unix_physmem_init (vm, /* physical_memory_required */ 1);
  if (error)
    return error;

  error = os_map_pci_resource (dev->os_handle, 0, &r);
  if (error)
    return error;

  vec_add2 (fm->devices, fd, 1);
  fd->pci_device = dev[0];
  fd->regs = r;
  fd->device_index = fd - fm->devices;
  fd->pci_function = dev->bus_address.slot_function & 1;

  fd->device_id = fd->pci_device.config0.header.device_id;
  switch (fd->device_id)
    {
    default:
      fd->is_8254x = 0;
      fd->rx_queue_no_wrap = fge_8257x_rx_queue_no_wrap;
      break;

#define _(f,i) case FGE_##f:
      foreach_fge_8254x_pci_device_id;
#undef _
      fd->is_8254x = 1;
      fd->rx_queue_no_wrap = fge_8254x_rx_queue_no_wrap;
      break;
    }

  /* Chip found so enable node. */
  {
    linux_pci_device_t * lp = pci_dev_for_linux (dev);

    vlib_node_set_state (vm, fge_input_node.index, 
			 (FGE_ALWAYS_POLL
			  ? VLIB_NODE_STATE_POLLING
			  : VLIB_NODE_STATE_INTERRUPT));
    lp->device_input_node_index = fge_input_node.index;
    lp->device_index = fd->device_index;
  }

  if (vec_len (fm->devices) == 1)
    {
      vlib_register_node (vm, &fge_process_node);
      fm->process_node_index = fge_process_node.index;
    }

  os_add_pci_disable_interrupts_reg
    (dev->os_handle,
     /* resource */ 0,
     STRUCT_OFFSET_OF (fge_regs_t, interrupt.enable_write_1_to_clear),
     /* value to write */ ~0);

  return 0;
}

static PCI_REGISTER_DEVICE (fge_pci_device_registration) = {
  .init_function = fge_pci_init,
  .supported_devices = {
#define _(t,i) { .vendor_id = PCI_VENDOR_ID_INTEL, .device_id = i, },
    foreach_fge_8254x_pci_device_id
    foreach_fge_8257x_pci_device_id
#undef _
    { 0 },
  },
};
#endif

static clib_error_t *
fge_config (vlib_main_t * vm, unformat_input_t * input)
{
  fge_main_t * fm = &fge_main;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (0)
	;

      else
	return clib_error_return (0, "unknown input `%U'",
				  format_unformat_error, input);
    }

  return 0;
}

/* fge { ... } configuration. */
VLIB_CONFIG_FUNCTION (fge_config, "fge");

static clib_error_t * fge_init (vlib_main_t * vm)
{
  return 0;
}

VLIB_INIT_FUNCTION (fge_init);
