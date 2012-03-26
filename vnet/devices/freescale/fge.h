/*
 * devices/freescale/fge.h: Freescale 85xx ethernet (TSEC) defines
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

#ifndef included_devices_fge_h
#define included_devices_fge_h

#include <vnet/ethernet/phy.h>

typedef volatile struct {
  struct {
    u32 id[2];
    CLIB_PAD_FROM_TO (0x8, 0x10);

    /* [31] rx babble
       [30] rx control frame received
       [29] rx packet arrived but no buffers available
       [28] system bus error on dma transaction
       [26] mib counter overflow
       [25] tx graceful stop complete
       [24] tx babble
       [23] tx control packet sent
       [22] tx channel error
       [21] tx buffer descriptor updated (interrupt bit set in descriptor)
       [20] tx frame transmitted (interrupt bit set in descriptor)
       [18] late collision
       [17] collision retry limit
       [16] tx fifo underrun
       [15] rx buffer with interrupt bit set
       [11] rx magic packet
       [10] mii read done
       [9] mii write done
       [8] rx graceful stop done
       [7] rx frame
       [4] rx filer rule interrupt
       [3] rx filer rule invalid
       [2] rx filer frame filed to invalid queue
       [1] fifo/filer parity error
       [0] tcp/ip parse error. */
    u32 interrupt_status_write_1_to_clear;
    u32 interrupt_enable;
    u32 interrupt_error_disable;
    CLIB_PAD_FROM_TO (0x1c, 0x20);

    /* [14] clear mib counters
       [13] mib counter enable clear on read
       [12] mib counter enable
       [6] 1 => gmii mode, 0 => rgmii, rmii, mii mode
       [5] ten bit interface mode enable
       [4] reduced pin mode for 1000m (rgmii or rtbi)
       [3] 1 => rgmii in 100m mode; 0 => rgmii in 10m mode
       [2] reduced pin mode (rmii)
       [1] serdes mode. */
    u32 control;
    CLIB_PAD_FROM_TO (0x24, 0x28);

    u32 pause_time_value;

    /* [15] buffer descriptors little endian
       [7] tx buffer data snoop enable
       [6] tx buffer descriptor snoop enable
       [4] rx graceful stop
       [3] tx graceful stop
       [2] tx on demain ring 0
       [1] tx wait for response before setting interrupt status bits
       [0] tx wait/poll ring 0. */
    u32 dma_control;

    u32 tbi_phy_address;
    CLIB_PAD_FROM_TO (0x34, 0x100);
  } main;

  struct {
    /* [14] ip checksum enable
       [13] tcp checksum enable
       [12] vlan tag insertion enable
       [11] tx half-duplex flow control enable
       [4] pause frame received
       [3] tx pause frame
       [2:1] tx scheduling mode (0 => ring 0 only, 1 => strict priority, 2 => weighted round robin) */
    u32 control;

    /* [31:16] tx halt ring (31 - i)
       [15:0] tx frame done (15 - i). */
    u32 status;

    /* [31:16] ethertype for 802.1q packets (0x8100)
       [15:0] vlan tag to be inserted into tx packets. */
    u32 vlan_config;
    CLIB_PAD_FROM_TO (0x10c, 0x110);

    /* [31] enable
       [30] 1 => every 64 system clocks, 0 => every 64 ethernet tx clocks
       [28:21] frame count
       [15:0] timer threshold. */
    u32 interrupt_coalesce;

    /* [15:0] tx queue enable (15 - i). */
    u32 queue_control;
    CLIB_PAD_FROM_TO (0x118, 0x140);

    /* [31:24] ring 0, etc. round robin weights. */
    u32 queue_round_robin_weights[2];
    CLIB_PAD_FROM_TO (0x148, 0x180);

    /* next_descriptor_address[0][0] gives high 4 bits
       of all tx buffer addresses.  descriptors only have low 32 bits.
       next_descriptor_address[i][1] gives low 32 bit address of next descriptor
       to be processed. */
    u32 next_descriptor_address[8][2];
    CLIB_PAD_FROM_TO (0x1c0, 0x200);

    /* base_descriptor_address[0][0] gives high 4 bits of all buffer descriptor addresses.
       base_descriptor_address[i][0] i > 0 are unused. */
    u32 base_descriptor_address[8][2];
    CLIB_PAD_FROM_TO (0x240, 0x280);

    u32 time_stamp_id[2];
    CLIB_PAD_FROM_TO (0x288, 0x2c0);

    u32 time_stamps[2][2];
    CLIB_PAD_FROM_TO (0x2d0, 0x300);
  } tx;

  struct {
    /* [31:25] byte offset of start of ethernet header
       [24] time stamp enable
       [23:16] number of padding bytes to align headers
       [14] lossless flow control
       [13] automatic vlan extract & delete
       [12] filer enable
       [11] filer always use rx queue 0
       [10] multicast group filter extend to 512 entries
       [9] ip checksum verify enable
       [8] tcp/udp checksum verify enable
       [7:6] parser control (0 => disable, 1 => l2, 2 => l2/l3, 3 => l2/l3/l4).
       [5] l2 parsing in fifo mode
       [4] reject broadcast frames
       [3] promiscuous mode (receive all frames)
       [2] rx short frames (< 64 bytes)
       [1] exact match filter enable. */
    u32 control;

    /* [23:16] queue halted (23 - i)
       [7:0] queue received frame (7 - i). */
    u32 status;
    CLIB_PAD_FROM_TO (0x308, 0x310);

    /* As above for tx. */
    u32 interrupt_coalesce;

    /* [23:16] deposit data in cache (23 - i)
       [7:0] queue enable. */
    u32 queue_control;
    CLIB_PAD_FROM_TO (0x318, 0x330);

    struct {
      /* [31:30] byte 0 control (0 => extract 0, 1 => offset - 8 from start of ethernet header,
         2 => offset from end of l2 header
	 3 => offset from end of l3 header.
	 [29:24] byte 0 offset
	 etc. for bytes 1-3. */
      u32 byte_field_extract_control;

      /* [7:0] index of 256 element table. */
      u32 address;

      /* [31] interrupt
	 [15:10] rx queue index
	 [9] cluster start/end
	 [8] drop if frame matches else file frame
	 [7] and (1 => and, 0 => or)
	 [6:5] compare
	 [3:0] property. */
      u32 control;

      u32 properties;
    } filer;

    /* In bytes must be multiple of 64 bytes. */
    u32 rx_buffer_bytes;
    CLIB_PAD_FROM_TO (0x344, 0x380);

    u32 current_descriptor_address[8][2];
    CLIB_PAD_FROM_TO (0x3c0, 0x400);

    u32 base_descriptor_address[8][2];
    CLIB_PAD_FROM_TO (0x440, 0x4c0);

    u32 time_stamp[2];
    CLIB_PAD_FROM_TO (0x4c8, 0x500);
  } rx;

  struct {
    /* [0]
         [31] soft reset
	 [19] rx mac reset
	 [18] tx mac reset
	 [17] rx function block reset
	 [16] tx function block reset
	 [8] mac loopback enable
	 [5] rx flow control enable
	 [4] tx flow control enable
	 [3] rx synchronized to stream (read only)
	 [2] rx enable
	 [1] tx synchronized (read only)
	 [0] tx enable.
       [1]
         [15:12] preamble length
	 [9:8] 1 => nibble mode (100m), 2 => byte mode (1000m)
	 [7] return preamble in packets
	 [6] user defined tx preamble
	 [5] huge frame enable
	 [4] rx length check enable
	 [3] magic packet enable
	 [2] pad and append crc
	 [1] crc enable
	 [0] full duplex. */
    u32 config[2];

    u32 inter_packet_and_frame_gap;
    u32 half_duplex_control;

    /* Both RX and TX.  Default 1536 bytes. */
    u32 max_frame_length;
    CLIB_PAD_FROM_TO (0x514, 0x520);

    struct {
      /* [31] reset
	 [4] preamble suppress
	 [2:0] mii clock frequency. */
      u32 config;

      /* [1] scan cycle
	 [0] read cycle (0 -> 1 transition starts read cycle; not self-clearing). */
      u32 read_command;

      /* [16:8] phy address
	 [7:0] register index. */
      u32 address;

      /* [15:0] write data (write-only)
	 If written performs a write with address/register above. */
      u32 write_data;

      /* [15:0] result of read cycle. */
      u32 read_data;

      /* [2] read data not valid
	 [1] scan in progress
	 [0] busy performing read or write cycle. */
      u32 status;
    } mii;

    u32 reserved;
    u32 interface_status;

    /* [0] [31:0] address [47:16]
       [1] [31:16] address [15:0]. */
    u32 ethernet_address[2];

    u32 ethernet_address_exact_match[16][2];
    CLIB_PAD_FROM_TO (0x5c0, 0x680);
  } mac;

  struct {
    u32 values[44];
    u32 carry[2];
    u32 carry_interrut_enable[2];
    CLIB_PAD_FROM_TO (0x740, 0x800);
  } counters;

  struct {
    /* For 256 entry table: this is the hash filter for unicast packets.
       Otherwise its the first 256 entries of 512 bit table. */
    u32 individual_group_address[8];
    CLIB_PAD_FROM_TO (0x820, 0x880);

    /* For 256 entry tables: group table (multicast).
       Otherwise last 256 entries of 512 bit table. */
    u32 group_address[8];
  } hash;
  CLIB_PAD_FROM_TO (0x8a0, 0xbf8);

  struct {
    u32 attr;
    u32 eli;
  } dma_attribute;
  CLIB_PAD_FROM_TO (0xc00, 0x1000);
} fge_regs_t;

#define foreach_fge_counter			\
  _ (rx_tx_64_byte_packets)			\
  _ (rx_tx_65_127_byte_packets)			\
  _ (rx_tx_128_255_byte_packets)		\
  _ (rx_tx_256_511_byte_packets)		\
  _ (rx_tx_512_1023_byte_packets)		\
  _ (rx_tx_1024_1518_byte_packets)		\
  _ (rx_tx_1519_1522_byte_good_vlan_packets)	\
  _ (rx_bytes)					\
  _ (rx_packets)				\
  _ (rx_crc_errors)				\
  _ (rx_multicast_packets)			\
  _ (rx_broadcast_packets)			\
  _ (rx_control_packets)			\
  _ (rx_pause_packets)				\
  _ (rx_unknown_op_packets)			\
  _ (rx_alignement_errors)			\
  _ (rx_frame_length_errors)			\
  _ (rx_code_errors)				\
  _ (rx_carrier_sense_errors)			\
  _ (rx_undersize_packets)			\
  _ (rx_oversize_packets)			\
  _ (rx_fragments)				\
  _ (rx_jabbers)				\
  _ (rx_dropped_packets)			\
  _ (tx_bytes)					\
  _ (tx_packets)				\
  _ (tx_multicast_packets)			\
  _ (tx_broadcast_packets)			\
  _ (tx_pause_packets)				\
  _ (tx_deferrals)				\
  _ (tx_excessive_deferrals)			\
  _ (tx_single_collision_packets)		\
  _ (tx_multiple_collision_packets)		\
  _ (tx_late_collision_packets)			\
  _ (tx_excessive_collision_packets)		\
  _ (tx_collisions)				\
  _ (reserved)					\
  _ (tx_dropped_packets)			\
  _ (tx_jabbers)				\
  _ (tx_crc_errors)				\
  _ (tx_control_packets)			\
  _ (tx_oversize_packets)			\
  _ (tx_undersize_packets)			\
  _ (tx_fragments)

typedef enum {
#define _(f) FGE_COUNTER_##f,
  foreach_fge_counter
#undef _
  FGE_N_COUNTER,
} fge_counter_type_t;

/* RX/TX buffer descriptors. */
typedef struct {
  /* tx:
     [15] is_owned_by_hardware
     [14] tx pad short frames to 64 bytes
     [13] wrap
     [12] interrupt when serviced
     [11] buffer is end-of-packet
     [10] tx append crc
     [9] tx user-defined preamble; when written back: deferred
     [8]
     [7] tx huge frame enable; when written back: late collision
     [6] tx control frame (tx even when paused); when written back: tx retry limit hit
     [5:2] tx retry count (written back)
     [1] tcp/ip offload enable; when written back: underrun
     [0] tx frame truncated.

     rx:
     [15] is_owned_by_hardware
     [14] software defined
     [13] wrap
     [12] interrupt when serviced
     [11] buffer is end-of-packet
     [10] buffer is start-of-packet
     [8] rx dst address lookup miss (packet accepted in promiscuous mode)
     [7] rx broadcast packet
     [6] rx multicast packet
     [5] rx frame too large
     [4] non octet aligned frame
     [3] rx short frame (< 64 bytes)
     [2] rx crc error
     [1] rx fifo overrun
     [0] rx frame truncated. */
  u16 status;
#define FGE_DMA_DESCRIPTOR_IS_OWNED_BY_HARDWARE (1 << 15)
#define FGE_DMA_DESCRIPTOR_IS_LAST_IN_RING (1 << 13)
#define FGE_DMA_DESCRIPTOR_LOG2_IS_END_OF_PACKET 11
#define FGE_DMA_DESCRIPTOR_IS_END_OF_PACKET (1 << FGE_DMA_DESCRIPTOR_LOG2_IS_END_OF_PACKET)
#define FGE_DMA_DESCRIPTOR_IS_START_OF_PACKET (1 << 10)

  u16 n_bytes_this_buffer;

  u32 buffer_address;
} fge_dma_descriptor_t;

  /* RX/TX */
#define DEVICES_FGE_BUFFER_IS_OWNED_BY_HARDWARE (1 << 15)
#define DEVICES_FGE_BUFFER_IS_END_OF_RING (1 << 13)
#define DEVICES_FGE_BUFFER_INTERRUPT_WHEN_DONE (1 << 12)
#define DEVICES_FGE_BUFFER_IS_END_OF_PACKET (1 << 11)

  /* RX only */
#define DEVICES_FGE_BUFFER_RX_IS_START_OF_PACKET (1 << 10)

  /* TX only */
#define DEVICES_FGE_BUFFER_TX_APPEND_CRC (1 << 10)

/* Starts frame when RX/TX offload enabled. */
typedef struct {
  /* [15] vlan valid
     [14] is ip4/ip6
     [13] is ip6
     [12] is tcp/udp
     [11] tx: is udp, rx: ip checksum computed
     [10] tx: checksum ip header, rx: tcp/udp checksum computed
     [9] tx: checksum udp/tcp header, rx: ip checksum error
     [8] tx: disable tcp/udp psuedo header checksum (and use pseudo_header_checksum field below)
         rx: tcp/udp checksum error (if ip_protocol == tcp or udp)
     [3:2] rx: parse error (0 => none, 2 => inconsistent/unsupported l3 headers)
     [0] tx: ptp packet; rx: filer event packet.
  */
  u16 flags;
#define FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_OR_IP6 (1 << 14)
#define FGE_OFFLOAD_HEADER_FLAGS_IS_IP6 (1 << 13)
#define FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_COMPUTED (1 << 10)
#define FGE_OFFLOAD_HEADER_FLAGS_IS_IP4_CHECKSUM_ERROR (1 << 9)

  union {
    struct {
      u8 l4_offset_from_start_of_l3;

      u8 l3_offset_from_start_of_frame;
    } tx;

    struct {
      /* RX queue for this packet. */
      u8 queue;

      u8 ip_protocol;
    } rx;
  };

  /* Only valid if disable tcp/udp pseudo header checksum is set above. */
  u16 tx_pseudo_header_checksum;

  /* TX: vlan tag to insert; RX: vlan tag extracted. */
  u16 vlan_tag;
} fge_offload_header_t;

typedef struct {
  /* Cache aligned descriptors. */
  fge_dma_descriptor_t * descriptors;

  /* Number of descriptors in table. */
  u32 n_descriptors;

  /* Software head and tail pointers into descriptor ring. */
  u32 head_index, tail_index;

  /* Index into dma_queues vector. */
  u32 queue_index;

  /* Buffer indices corresponding to each active descriptor. */
  u32 * descriptor_buffer_indices;

  union {
    struct {
      u32 n_tx_descriptors_on_ring;
    } tx;

    struct {
      /* Buffer indices to use to replenish each descriptor. */
      u32 * replenish_buffer_indices;

      vlib_node_runtime_t * node;
      u32 next_index;

      u32 saved_start_of_packet_buffer_index;

      u32 saved_start_of_packet_next_index;
      u32 saved_last_buffer_index;

      u32 is_start_of_packet;

      u32 n_descriptors_done_total;

      u32 n_descriptors_done_this_call;

      u32 n_bytes;
    } rx;
  };
} fge_dma_queue_t;

struct fge_main_t;

typedef struct fge_device_t {
  fge_regs_t * regs;

  u16 device_index;

  /* VNET interface for this instance. */
  u32 vnet_hw_if_index, vnet_sw_if_index;

  fge_dma_queue_t * dma_queues[VLIB_N_RX_TX];

  ethernet_phy_t phy;

  /* Counters. */
  u64 counters[FGE_N_COUNTER], counters_last_clear[FGE_N_COUNTER];
} fge_device_t;

typedef struct fge_main_t {
  vlib_main_t * vlib_main;

  /* Vector of devices. */
  fge_device_t * devices;

  /* Descriptor ring sizes. */
  u32 n_descriptors[VLIB_N_RX_TX];

  /* RX buffer size. */
  u32 n_bytes_in_rx_buffer;

  u32 n_descriptors_per_cache_line;

  u32 vlib_buffer_free_list_index;

  u32 process_node_index;

  /* Vector of buffers for which TX is done and can be freed. */
  u32 * tx_buffers_pending_free;

  u32 * rx_buffers_to_add;

  f64 time_last_stats_update;
} fge_main_t;

fge_main_t fge_main;
vnet_device_class_t fge_device_class;

#endif /* included_devices_fge_h */
