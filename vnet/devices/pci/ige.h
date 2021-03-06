/*
 * devices/pci/ige.h: Intel 825xx E1000 gige driver
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

#ifndef included_ige_h
#define included_ige_h

#include <vnet/vnet.h>
#include <vlib/pci/pci.h>
#include <vnet/devices/optics/sfp.h>
#include <vnet/devices/pci/ixge.h> /* for shared definitions between ige/ixge */
#include <vnet/ethernet/phy.h>
#include <vnet/ip/ip4_packet.h>

typedef volatile struct {
  /* [31:7] 128 byte aligned. */
  u32 descriptor_address[2];
  u32 n_descriptor_bytes;
  CLIB_PAD_FROM_TO (0x000c, 0x0010);
  u32 head_index;
  CLIB_PAD_FROM_TO (0x0014, 0x0018);
  u32 tail_index;

  CLIB_PAD_FROM_TO (0x001c, 0x0020);
  u32 interrupt_delay_timer;
  CLIB_PAD_FROM_TO (0x24, 0x28);
  u32 control;
  u32 interrupt_absolute_delay_timer;

  u32 tcp_segmentation_pad_control_8254x;
  CLIB_PAD_FROM_TO (0x34, 0x40);

  u32 tx_arbitration;
  CLIB_PAD_FROM_TO (0x44, 0x100);
} ige_dma_regs_t;

/* Descriptor format is shared. */
typedef ixge_rx_to_hw_descriptor_t ige_rx_to_hw_descriptor_t;
typedef ixge_rx_from_hw_descriptor_t ige_rx_from_hw_descriptor_t;
typedef ixge_tx_descriptor_t ige_tx_descriptor_t;

typedef struct {
  u64 buffer_address;

  u16 n_packet_bytes_this_descriptor;

  /* 16 bit ones complement sum of packet.  You must back out fields
     to get IP/TCP/UDP checksum. */
  u16 ip_checksum;

  u16 status;

  u16 vlan_tag;
} ige_legacy_rx_descriptor_t;

typedef union {
  ige_legacy_rx_descriptor_t rx_legacy;
  ige_rx_to_hw_descriptor_t rx_to_hw;
  ige_rx_from_hw_descriptor_t rx_from_hw;
  ige_tx_descriptor_t tx;
} ige_descriptor_t;

typedef struct {
  /* Cache aligned descriptors. */
  ige_descriptor_t * descriptors;

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
} ige_dma_queue_t;

/* e1000/1gige chip descriptors are mostly the same as ixge descriptors.
   Here are a few bits that are different. */
#define IGE_RX_DESCRIPTOR_STATUS2_IS_OWNED_BY_SOFTWARE (1 << (0 + 0))
#define IGE_RX_DESCRIPTOR_STATUS2_IS_END_OF_PACKET (1 << (0 + 1))
#define IGE_RX_DESCRIPTOR_STATUS2_NOT_IP4 (1 << (0 + 2))
#define IGE_RX_DESCRIPTOR_STATUS2_IS_VLAN (1 << (0 + 3))
#define IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_UDP_CHECKSUMMED (1 << (0 + 4))
#define IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_TCP_CHECKSUMMED (1 << (0 + 5))
#define IGE_RX_DESCRIPTOR_STATUS2_IS_IP4_CHECKSUMMED (1 << (0 + 6))
#define IGE_RX_DESCRIPTOR_STATUS2_PASSED_MULTICAST_FILTER (1 << (0 + 7))
#define IGE_RX_DESCRIPTOR_STATUS2_UDP_MYSTERY (1 << (0 + 10)) /* dont understand this one */

#define IGE_RX_DESCRIPTOR_STATUS2_CRC_ERROR (1 << (20 + 4))
#define IGE_RX_DESCRIPTOR_STATUS2_SYMBOL_ERROR (1 << (20 + 5))
#define IGE_RX_DESCRIPTOR_STATUS2_SEQUENCE_ERROR (1 << (20 + 6))
#define IGE_RX_DESCRIPTOR_STATUS2_IP4_TCP_UDP_CHECKSUM_ERROR (1 << (20 + 9))
#define IGE_RX_DESCRIPTOR_STATUS2_IP4_CHECKSUM_ERROR (1 << (20 + 10))
#define IGE_RX_DESCRIPTOR_STATUS2_RX_DATA_ERROR (1 << (20 + 11))

/* lo 8 bits of status give checksum byte offset: where to insert tcp/udp checksum. */
#define IGE_TX_DESCRIPTOR_STATUS0_IS_ADVANCED (1 << (8 + 5))
#define IGE_TX_DESCRIPTOR_STATUS0_LOG2_REPORT_STATUS (8 + 3)
#define IGE_TX_DESCRIPTOR_STATUS0_REPORT_STATUS (1 << IGE_TX_DESCRIPTOR_STATUS0_LOG2_REPORT_STATUS)
#define IGE_TX_DESCRIPTOR_STATUS0_INSERT_L4_CHECKSUM (1 << (8 + 2))
#define IGE_TX_DESCRIPTOR_STATUS0_INSERT_FCS (1 << (8 + 1))
#define IGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET (8 + 0)
#define IGE_TX_DESCRIPTOR_STATUS0_IS_END_OF_PACKET (1 << IGE_TX_DESCRIPTOR_STATUS0_LOG2_IS_END_OF_PACKET)
#define IGE_TX_DESCRIPTOR_STATUS1_DONE (1 << 0)

/* Legacy RX descriptors: only used for 8254X. */
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IS_OWNED_BY_SOFTWARE (1 << (0 + 0))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IS_END_OF_PACKET (1 << (0 + 1))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_NOT_IP4 (1 << (0 + 2))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IS_VLAN (1 << (0 + 3))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IS_IP4_TCP_CHECKSUMMED (1 << (0 + 5))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IS_IP4_CHECKSUMMED (1 << (0 + 6))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_PASSED_MULTICAST_FILTER (1 << (0 + 7))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_CRC_ERROR (1 << (8 + 0))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_SYMBOL_ERROR (1 << (8 + 1))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_SEQUENCE_ERROR (1 << (8 + 2))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IP4_TCP_CHECKSUM_ERROR (1 << (8 + 5))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_IP4_CHECKSUM_ERROR (1 << (8 + 6))
#define IGE_LEGACY_RX_DESCRIPTOR_STATUS_RX_DATA_ERROR (1 << (8 + 7))

typedef volatile struct {
  /*
   * [0]   1=full duplex
   * [2:1] reserved
   * [3]   1=link reset
   * [4]   reserved
   * [5]   1=auto speed detection enable
   * [6]   1=set (force) link up
   * [7]   1=invert loss of signal (set 0 for internal phy)
   * [9:8] speed 0b00=10mb, 0b01=100mb 0b10=1000mb 0b11 not used
   * [10]  reserved, should be 0
   * [11]  1=force speed, superceded, applicable in internal phy only
   * [12]  1=force duplex 0=ignore setting of [0]
   * [17:13] reserved, should be 0
   * [18]  sdp0_data software-controllable i/o pin SDP0
   * [19]  sdp1_data 
   * [20]  Wakeup capability advertisement enable
   * [21]  Phy power mgmt enable
   * [22]  sdp0 i/o direction, 0=input 1=output
   * [23]  sdp1 i/o direction, 0=input 1=output
   * [25:24] reserved
   * [26]  1=reset
   * [27]  1=receive flow control enable
   * [28]  1=transmit flow control enable
   * [29]  reserved, should be 0
   * [30]  1=VLAN mode enable
   * [31]  1=reset internal phy. See timing notes in chip sheet
   */
  u32 control;
  CLIB_PAD_FROM_TO (0x0004, 0x0008);
  /* 
   * [0]   link is full duplex
   * [1]   link is up
   * [3:2] (pci) function id: 0b00 LAN A, 0b01 LAN B
   * [4]   transmit paused
   * [5]   1=Serdes mode, 0=internal phy mode
   * [7:6] link speed 0b00=10mb, 0b01=100mb 0b10=1000mb 0b11=1000mb
   *       not valid in serdes mode
   * [9:8] (auto) detected speed
   * [10]  reserved
   * [11]  1=66mHz
   * [12]  1=64-bit PCI
   * [13]  1=pci-x
   * [15:14] pci-x bus speed 0b00=50-66MHz, 0b01=66-100MHz, 0b10=100-133
   MHz
  */
  u32 status;
  CLIB_PAD_FROM_TO (0x000C, 0x0010);
  /*
   * [0] clock input
   * [1] chip select
   * [2] data input
   * [3] data output
   * [5:4] flash write enable 0b00=not allowed, 0b01=not allowed,
   0b10=allowed 0b11=not allowed
   * [6] 1=request access
   * [7] 1=access granted
   * [8] 1=eeprom present
   * [9] eeprom size,non microwire, 1=4096 bit, 0=1024 bit
   * [10] eeprom size, microwire, 0=6 bit addressable, 1=8 bit
   addressable
   SPI eeprom, 0=8 bit addressable, 1=16 bit
   addressable
   * [12:11] reserved
   * [13] eeprom type 0=microwire 1=SPI
   */
  u32 eeprom_flash_control;
  /* 
   * [0] start
   * [3:1] reserved
   * [4] done
   * [7:5] reserved
   * [15:8] address
   * [31:16] read data. 
   */
  u32 eeprom_read;
  u32 extended_control;
  u32 flash_access;
  /* 
   * [15:0] data
   * [20:16] phy register address
   * [25:21] phy address (always 0b0001)
   * [27:26] opcode 0b01 = write, 0b10=read
   * [28] 1 => ready, 0 => busy
   * [29] interrupt enable
   * [30] error
   */
  u32 mdi_control;
  u32 serdes_control;
  /* 
   * [0] = program 0x00c28001
   * [1] = program 0x0100
   */
  u32 flow_control_addr[2];
  /*
   * [15:0] program 0x8808
   */
  u32 flow_control_type;

  u32 glci_control;

  /* 
   * [15:0] program 0x8100
   */
  u32 vlan_ether_type;

  struct {
    CLIB_PAD_FROM_TO (0x003c, 0x00c0);
    u32 status_clear_to_read;
    u32 throttle_rate;
    u32 status_write_1_to_set;

    CLIB_PAD_FROM_TO (0x00cc, 0x00d0);
    u32 enable_write_1_to_set;
    CLIB_PAD_FROM_TO (0x00d4, 0x00d8);
    u32 enable_write_1_to_clear;
    CLIB_PAD_FROM_TO (0xdc, 0xe0);
    u32 auto_mask;
    CLIB_PAD_FROM_TO (0xe4, 0x100);
  } interrupt;

  /* default all 0s
     [1] rx enable
     [2] store bad packets
     [3] accept all unicast
     [4] accept all multicast
     [5] accept packets > 1522 bytes
     [7:6] loopback mode
     [9:8] rx minimum buffer interrupt threshold
     [11:10] descriptor type
     [13:12] multicast filter 12 bit offset
     [15] accept all broadcast
     [17:16] rx buffer size
     [25] rx buffer size ext (multiples sizes by 16)
     [18] vlan filter enable
     [22] discard pause frames
     [23] accept mac control frames (not pause)
     [26] strip ethernet crc
     [30:27] flexible buffer size */
  u32 rx_control;

  /* 
   * [15:0] set to 0x0b prior to initializing pause frame
   */
  CLIB_PAD_FROM_TO (0x0104, 0x0170);
  u32 tx_flow_control_timer_value;

  CLIB_PAD_FROM_TO (0x0174, 0x0178);
  u32 tx_autoneg_config_word;

  CLIB_PAD_FROM_TO (0x017c, 0x0180);
  u32 rx_autoneg_config_word;

  CLIB_PAD_FROM_TO (0x0184, 0x0400);
  /* [1] tx enable
     [3] pad short packets
     [28] tx multiple descriptor request enable */
  u32 tx_control;

  CLIB_PAD_FROM_TO (0x0404, 0x0410);
  u32 tx_inter_packet_gap;

  CLIB_PAD_FROM_TO (0x0414, 0x0458);
  u32 adaptive_ifs_throttle;

  CLIB_PAD_FROM_TO (0x045c, 0x0e00);
  u32 led_control;

  CLIB_PAD_FROM_TO (0x0e04, 0x1000);
  u32 packet_buffer_allocation;
  CLIB_PAD_FROM_TO (0x1004, 0x1010);
  u32 management_eeprom_control;

  CLIB_PAD_FROM_TO (0x1014, 0x1100);
  u32 packet_buffer_ecc_control;
  CLIB_PAD_FROM_TO (0x1104, 0x2008);
  u32 early_rx_threshold;

  CLIB_PAD_FROM_TO (0x200c, 0x2160);
  u32 rx_flow_control_receive_threshold_low;

  CLIB_PAD_FROM_TO (0x2164, 0x2168);
  u32 rx_flow_control_receive_threshold_high;

  CLIB_PAD_FROM_TO (0x216c, 0x2170);
  u32 rx_packet_split_control;

  CLIB_PAD_FROM_TO (0x2174, 0x2800);
  ige_dma_regs_t rx_dma[2];
  CLIB_PAD_FROM_TO (0x2a00, 0x2c00);
    
  u32 rx_small_packet_detect_interrupt;
  CLIB_PAD_FROM_TO (0x2c04, 0x2c08);
  u32 rx_ack_interrupt_delay;
  CLIB_PAD_FROM_TO (0x2c0c, 0x2c10);
  u32 cpu_vector;
  CLIB_PAD_FROM_TO (0x2c14, 0x3800);

  ige_dma_regs_t tx_dma[2];

  CLIB_PAD_FROM_TO (0x3a00, 0x5000);

  /* [7:0] checksum starting byte offset
     [8] ip checksum enable
     [9] tcp/udp checksum enable. */
  u32 rx_checksum_control;
  CLIB_PAD_FROM_TO (0x5004, 0x5008);

  /* [0] iscsi disable
     [5:1] iscsi word count
     [6] nfs write disable
     [7] nfs read disable
     [9:8] nfs version
     [10] ip6 filter disable
     [11] ip6 checksum disable
     [12] ack accel disable
     [13] ack data disable
     [14] ip fragment split disable
     [15] extended status enable. */
  u32 rx_filter_control;

  CLIB_PAD_FROM_TO (0x500c, 0x5200);
  /* 12 bits determined by rx_filter_control
     lookup bits in this vector. */
  u32 multicast_enable[128];

  /* [0] ethernet address [31:0]
     [1] [15:0] ethernet address [47:32]
         [31] valid bit.
     Index 0 is read from eeprom after reset. */
  u32 rx_ethernet_address[8][2];
  CLIB_PAD_FROM_TO (0x5440, 0x5600);

  /* 4096 bits one for each possible vlan. */
  u32 vlan_filter[128];

  u32 wakeup_control;
  CLIB_PAD_FROM_TO (0x5804, 0x5808);
  u32 wakeup_filter_control;
  CLIB_PAD_FROM_TO (0x580c, 0x5810);
  u32 wakeup_status;
  CLIB_PAD_FROM_TO (0x5814, 0x5818);
  u32 rx_multiple_queues_config;
  CLIB_PAD_FROM_TO (0x581c, 0x5820);
  u32 management_control;
  CLIB_PAD_FROM_TO (0x5824, 0x5838);
  u32 wakeup_ip46_address_valid;
  CLIB_PAD_FROM_TO (0x583c, 0x5840);
  struct {
    u32 addr;
    u32 unused;
  } wakeup_ip4_address_table[4];
  CLIB_PAD_FROM_TO (0x5860, 0x5864);
  u32 rss_interrupt_enable;
  u32 rss_interrupt_request;
  CLIB_PAD_FROM_TO (0x586c, 0x5880);  
  u32 wakeup_ip6_address_table[4];
  CLIB_PAD_FROM_TO (0x5890, 0x5900);
  u32 wakeup_packet_length;
  CLIB_PAD_FROM_TO (0x5904, 0x5a00);
  u32 wakeup_packet_memory[32];
  CLIB_PAD_FROM_TO (0x5a80, 0x5b00);
  struct {
    u32 control;
    CLIB_PAD_FROM_TO (0x5b04, 0x5b10);
    u32 counter_control[4];
    u32 counters[4];
    u32 power_stuff;
    u32 serdes_control[7];
  } pcie;
  u32 software_semaphore;
  u32 firmware_semaphore;
  CLIB_PAD_FROM_TO (0x5b58, 0x5b5c);
  u32 software_firmware_sync;
  CLIB_PAD_FROM_TO (0x5b60, 0x5c00);
  u32 redirection_table[32];
  u32 rss_random_keys[10];
  CLIB_PAD_FROM_TO (0x5ca8, 0x5f00);
  u32 wakeup_flexible_filter_lengths[6];
  CLIB_PAD_FROM_TO (0x5f18, 0x9000);

  u32 wakeup_flexible_filter_masks[128][2];
  CLIB_PAD_FROM_TO (0x9400, 0x9800);
  u32 wakeup_flexible_filter_values[128][2];
} ige_regs_t;

#define foreach_ige_8254x_pci_device_id		\
  _ (82542, 0x1000)				\
  _ (82543gc_fiber, 0x1001)			\
  _ (82543gc_copper, 0x1004)			\
  _ (82544ei_copper, 0x1008)			\
  _ (82544ei_fiber, 0x1009)			\
  _ (82544gc_copper, 0x100c)			\
  _ (82544gc_lom, 0x100d)			\
  _ (82540em, 0x100e)				\
  _ (82540em_lom, 0x1015)			\
  _ (82540ep_lom, 0x1016)			\
  _ (82540ep, 0x1017)				\
  _ (82540ep_lp, 0x101e)			\
  _ (82545em_copper, 0x100f)			\
  _ (82545em_fiber, 0x1011)			\
  _ (82545gm_copper, 0x1026)			\
  _ (82545gm_fiber, 0x1027)			\
  _ (82545gm_serdes, 0x1028)			\
  _ (82546eb_copper, 0x1010)			\
  _ (82546eb_fiber, 0x1012)			\
  _ (82546eb_quad_copper, 0x101d)		\
  _ (82541ei, 0x1013)				\
  _ (82541ei_mobile, 0x1018)			\
  _ (82541er_lom, 0x1014)			\
  _ (82541er, 0x1078)				\
  _ (82547gi, 0x1075)				\
  _ (82541gi, 0x1076)				\
  _ (82541gi_mobile, 0x1077)			\
  _ (82541gi_lf, 0x107c)			\
  _ (82546gb_copper, 0x1079)			\
  _ (82546gb_fiber, 0x107a)			\
  _ (82546gb_serdes, 0x107b)			\
  _ (82546gb_pcie, 0x108a)			\
  _ (82546gb_quad_copper, 0x1099)		\
  _ (82547ei, 0x1019)				\
  _ (82547ei_mobile, 0x101a)			\
  _ (82546gb_quad_copper_ksp3, 0x10b5)

#define foreach_ige_8257x_pci_device_id		\
  _ (82571eb_copper, 0x105e)			\
  _ (82571eb_fiber, 0x105f)			\
  _ (82571eb_serdes, 0x1060)			\
  _ (82571eb_quad_copper, 0x10a4)		\
  _ (82571pt_quad_copper, 0x10d5)		\
  _ (82571eb_quad_fiber, 0x10a5)		\
  _ (82571eb_quad_copper_lp, 0x10bc)		\
  _ (82571eb_serdes_dual, 0x10d9)		\
  _ (82571eb_serdes_quad, 0x10da)		\
  _ (82572ei_copper, 0x107d)			\
  _ (82572ei_fiber, 0x107e)			\
  _ (82572ei_serdes, 0x107f)			\
  _ (82572ei, 0x10b9)				\
  _ (82573e, 0x108b)				\
  _ (82573e_iamt, 0x108c)			\
  _ (82573l, 0x109a)				\
  _ (82574l, 0x10d3)				\
  _ (82574la, 0x10f6)				\
  _ (82583v, 0x150c)				\
  _ (80003es2lan_copper_dpt, 0x1096)		\
  _ (80003es2lan_serdes_dpt, 0x1098)		\
  _ (80003es2lan_copper_spt, 0x10ba)		\
  _ (80003es2lan_serdes_spt, 0x10bb)		\
  _ (ich8_igp_m_amt, 0x1049)			\
  _ (ich8_igp_amt, 0x104a)			\
  _ (ich8_igp_c, 0x104b)			\
  _ (ich8_ife, 0x104c)				\
  _ (ich8_ife_gt, 0x10c4)			\
  _ (ich8_ife_g, 0x10c5)			\
  _ (ich8_igp_m, 0x104d)			\
  _ (ich9_igp_amt, 0x10bd)			\
  _ (ich9_bm, 0x10e5)				\
  _ (ich9_igp_m_amt, 0x10f5)			\
  _ (ich9_igp_m, 0x10bf)			\
  _ (ich9_igp_m_v, 0x10cb)			\
  _ (ich9_igp_c, 0x294c)			\
  _ (ich9_ife, 0x10c0)				\
  _ (ich9_ife_gt, 0x10c3)			\
  _ (ich9_ife_g, 0x10c2)			\
  _ (ich10_r_bm_lm, 0x10cc)			\
  _ (ich10_r_bm_lf, 0x10cd)			\
  _ (ich10_r_bm_v, 0x10ce)			\
  _ (ich10_d_bm_lm, 0x10de)			\
  _ (ich10_d_bm_lf, 0x10df)			\
  _ (pch_m_hv_lm, 0x10ea)			\
  _ (pch_m_hv_lc, 0x10eb)			\
  _ (pch_d_hv_dm, 0x10ef)			\
  _ (pch_d_hv_dc, 0x10f0)

typedef enum {
#define _(f,n) IGE_##f = n,
  foreach_ige_8257x_pci_device_id
  foreach_ige_8254x_pci_device_id
#undef _
} ige_pci_device_id_t;

#define foreach_ige_counter                     \
  _ (0x40d0, rx_total_packets)                  \
  _64 (0x40c0, rx_total_bytes)                  \
  _ (0x4074, rx_good_packets)                   \
  _64 (0x4088, rx_good_bytes)                   \
  _ (0x407c, rx_multicast_packets)              \
  _ (0x4078, rx_broadcast_packets)              \
  _ (0x405c, rx_64_byte_packets)                \
  _ (0x4060, rx_65_127_byte_packets)            \
  _ (0x4064, rx_128_255_byte_packets)           \
  _ (0x4068, rx_256_511_byte_packets)           \
  _ (0x406c, rx_512_1023_byte_packets)          \
  _ (0x4070, rx_gt_1023_byte_packets)           \
  _ (0x4000, rx_crc_errors)                     \
  _ (0x4004, rx_illegal_symbol_errors)          \
  _ (0x4008, rx_error_symbol_errors)            \
  _ (0x400c, rx_errors)                         \
  _ (0x4010, rx_misses)                         \
  _ (0x4014, rx_single_collisions)              \
  _ (0x401c, rx_multiple_collisions)            \
  _ (0x4018, rx_excessive_collisions)           \
  _ (0x4020, rx_late_collisions)                \
  _ (0x4028, rx_collisions)			\
  _ (0x4030, rx_deferred)			\
  _ (0x4034, tx_no_crs)                         \
  _ (0x4038, rx_sequence_errors)                \
  _ (0x4040, rx_length_errors)                  \
  _ (0x4048, rx_xons)                           \
  _ (0x404c, tx_xons)                           \
  _ (0x4050, rx_xoffs)                          \
  _ (0x4054, tx_xoffs)                          \
  _ (0x40a0, rx_no_buffers)			\
  _ (0x40a4, rx_undersize_packets)              \
  _ (0x40a8, rx_fragments)                      \
  _ (0x40ac, rx_oversize_packets)               \
  _ (0x40b0, rx_jabbers)                        \
  _ (0x40b4, rx_management_packets)             \
  _ (0x40b8, rx_management_drops)               \
  _ (0x40d4, tx_total_packets)                  \
  _64 (0x40c8, tx_total_bytes)                  \
  _ (0x4080, tx_good_packets)                   \
  _64 (0x4090, tx_good_bytes)                   \
  _ (0x40f0, tx_multicast_packets)              \
  _ (0x40f4, tx_broadcast_packets)              \
  _ (0x40d8, tx_64_byte_packets)                \
  _ (0x40dc, tx_65_127_byte_packets)            \
  _ (0x40e0, tx_128_255_byte_packets)           \
  _ (0x40e4, tx_256_511_byte_packets)           \
  _ (0x40e8, tx_512_1023_byte_packets)          \
  _ (0x40ec, tx_gt_1023_byte_packets)

typedef enum {
#define _(a,f) IGE_COUNTER_##f,
#define _64(a,f) _(a,f)
  foreach_ige_counter
#undef _
#undef _64
  IGE_N_COUNTER,
} ige_counter_type_t;

struct ige_main_t;

typedef struct ige_device_t {
  ige_regs_t * regs;

  /* PCI bus info. */
  pci_device_t pci_device;

  /* From PCI config space header. */
  ige_pci_device_id_t device_id;

  u16 device_index;

  /* 0 or 1. */
  u8 pci_function;

  /* True if for 8254x versus 8257x. */
  u8 is_8254x;

  uword (* rx_queue_no_wrap) (struct ige_main_t * xm,
			      struct ige_device_t * xd,
			      ige_dma_queue_t * dq,
			      u32 start_descriptor_index,
			      u32 n_descriptors);

  /* VLIB interface for this instance. */
  u32 vlib_hw_if_index, vlib_sw_if_index;

  ige_dma_queue_t * dma_queues[VLIB_N_RX_TX];

  /* Phy index (0 or 1) and address on MDI bus. */
  u32 phy_index;

  ethernet_phy_t phy;

  /* Counters. */
  u64 counters[IGE_N_COUNTER], counters_last_clear[IGE_N_COUNTER];
} ige_device_t;

typedef struct ige_main_t {
  vlib_main_t * vlib_main;

  /* Vector of devices. */
  ige_device_t * devices;

  /* Descriptor ring sizes. */
  u32 n_descriptors[VLIB_N_RX_TX];

  /* RX buffer size. */
  u32 n_bytes_in_rx_buffer;

  u32 n_descriptors_per_cache_line;

  u32 vlib_buffer_free_list_index;

  u32 process_node_index;

  /* Template and mask for initializing/validating TX descriptors. */
  ige_tx_descriptor_t tx_descriptor_template, tx_descriptor_template_mask;

  /* Vector of buffers for which TX is done and can be freed. */
  u32 * tx_buffers_pending_free;

  u32 * rx_buffers_to_add;

  f64 time_last_stats_update;
} ige_main_t;

ige_main_t ige_main;
vnet_device_class_t ige_device_class;

#endif /* included_ige_h */
