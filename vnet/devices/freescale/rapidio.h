/*
 * device/freescale/rapidio.h: Freescale 8xxx rapidio defines
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

#ifndef included_freescale_rapidio_h
#define included_freescale_rapidio_h

#include <clib/types.h>

/* (name, ftype, ttype) */
#define foreach_rapidio_transaction		\
  _ (read, 0x2, 0x4)				\
  _ (read_home, 0x2, 0x2)			\
  _ (read_and_post_increment, 0x2, 0xc)		\
  _ (read_and_post_decrement, 0x2, 0xd)		\
  _ (read_and_set_to_ones, 0x2, 0xe)		\
  _ (read_and_set_to_zero, 0x2, 0xf)		\
  _ (flush, 0x5, 0x1)				\
  _ (write, 0x5, 0x4)				\
  _ (write_with_response, 0x5, 0x5)		\
  _ (write_streaming, 0x6, 0x0)			\
  _ (maintenance_read, 0x8, 0x0)		\
  _ (maintenance_write, 0x8, 0x1)		\
  _ (maintenance_read_response, 0x8, 0x2)	\
  _ (maintenance_write_response, 0x8, 0x3)	\
  _ (maintenance_port_write, 0x8, 0x4)		\
  _ (doorbell, 0xa, 0x0)			\
  _ (message, 0xb, 0x0)				\
  _ (message_response_without_data, 0xd, 0x0)	\
  _ (message_response_with_data, 0xd, 0x8)

typedef struct {
  u8 ack_id;
  u8 priority : 2;
  u8 transaction_type : 2;
  u8 ftype : 4;
  u8 dst_id;
  u8 src_id;
} rapidio_packet_header_t;

typedef struct {
  union {
    u32 address : 29;

    struct {
      u32 hop_count : 8;
      u32 config_offset : 21;
    };
  };

  u32 word_pointer : 1;

  u32 extended_address_msbs : 2;
} rapidio_address_t;

typedef struct {
  rapidio_packet_header_t header;

  u8 ttype : 4;
  u8 size : 4;

  u8 transaction_id;
} rapidio_generic_packet_t;

/* TX buffer descriptors. */
typedef struct {
  /* [35:3] address hi bits first.
     [2] snoop enable. */
  u32 src_buffer_address[2];

  /* [31:16] destination id of target
     [5:2] extended mailbox
     [1:0] mailbox. */
  u32 dst_port;

  /* [31] multicast enable
     [29] end of message/doorbell interrupt enable
     [27:26] priority
     [23:20] rapidio port.
     [15:0] doorbell info field. */
  u32 dst_attributes;

  /* Must be of the form 2^i for i >= 3 and i <= 12. */
  u32 n_transfer_bytes;

  u32 multicast_group;

  /* Bitmap of device IDs of recepients. */
  u32 multicast_list;

  u32 reserved;
} freescale_rapidio_tx_descriptor_t;

/* Doorbell queue is a ring of this structure. */
typedef struct {
  u16 reserved;

  /* Source and destination IDs for this message. */
  u16 dst_id;
  u16 src_id;

  /* 16 bits of user data that goes along with doorbell. */
  u16 user_data;
} freescale_rapidio_doorbell_descriptor_t;

typedef volatile struct {
  u32 id;
  u32 info;
  u32 assembly_id;
  u32 assembly_info;
  u32 features;
  CLIB_PAD_FROM_TO (0x14, 0x18);
  u32 src_features;
  u32 dst_features;
  CLIB_PAD_FROM_TO (0x20, 0x40);

  /* [0]
     [31] msg unit 0 is_ready
     [30] msg unit 0 is_full
     [29] msg unit 0 is_empty
     [28] msg unit 0 is_busy
     [27] msg unit 0 is_error
     [23] msg unit 1 as above.
    [1]
     [31] doorbell unit ready
     [30] doorbell unit full
     [30] doorbell unit empty
     [29] doorbell unit busy processing message
     [28] doorbell unit internal error
     [7] port-write ready
     [6] port-write full
     [5] port-write empty
     [4] port-write busy processing message */
  u32 status[2];
  CLIB_PAD_FROM_TO (0x48, 0x4c);

  /* [2:0] extended addressing control (read only). */
  u32 logical_layer_status;

  /* [30:17] hi 14 bits of 34 bit rapidio address.  Takes precedence
     over translation unit. */
  u32 local_config_space_base_address;
  CLIB_PAD_FROM_TO (0x50, 0x5c);

  /* [15:0] our large system device id
     [23:16] our small system device id. */
  u32 base_device_id;
  CLIB_PAD_FROM_TO (0x64, 0x68);

  /* [15:0] device id of lock holder (write once). */
  u32 base_device_id_lock;

  /* unused */
  u32 component_tag_command;
  CLIB_PAD_FROM_TO (0x70, 0x100);

  struct {
    u32 block_header;
    CLIB_PAD_FROM_TO (0x104, 0x120);

    /* Units of 168/platform-frequencey. */
    u32 link_timeout;
    u32 response_timeout;
    CLIB_PAD_FROM_TO (0x128, 0x13c);

    /* [31] 1 => host, 0 => agent
       [30] 1 => master, 0 => slave
       [29] discovered. */
    u32 control;

    struct {
      /* 3 bit maintenance request.
	 (3 => reset device, 4 => input status). */
      u32 link_maintenance_request;

      /* [31] response valid
	 [9:5] ack status
	 [4:0] link status from response. */
      u32 link_maintenance_response;

      u32 local_ack_id_status;
      CLIB_PAD_FROM_TO (0x14c, 0x158);

      /* [26] output packet dropped
	 [25] output failed encountered
	 [24] output degraded
	 [20] output retry
	 [19] output retry symbol received
	 [18] output port stopped due to retry
	 [17] output port tx error encountered
	 [16] output port stopped due to tx error
	 [10] input port stopped due to retry
	 [9] input port tx error encountered
	 [8] input port stopped due to tx error
	 etc. */
      u32 error_status;

      /* [31:30] 0 => single lane, 1 => 4 lanes
	 [29:27] 0 => single lane port, lane 0, 1 => single lane port, lane 2, 2 => 4 lane port
	 [26:24] force port
	 [23] input error state machine forced to normal (port disable)
	 [22] output port tx enable
	 [21] input port rx enable
	 [20] error checking disable
	 [19] multicast event participant
	 [3] stop on port failed
	 [2] drop packet enable when error rate exceeds threshold
	 [1] port lockout
	 [0] port type (0 => reserved, 1 => serial port). */
      u32 control;
    } per_port[2];
  } port;
  CLIB_PAD_FROM_TO (0x160, 0x600);

  struct {
    u32 block_header;
    CLIB_PAD_FROM_TO (0x604, 0x608);

    u32 error_detect;
    u32 error_enable;
    CLIB_PAD_FROM_TO (0x610, 0x614);
    u32 address_capture;
    u32 device_id_capture;
    u32 control_capture;
    CLIB_PAD_FROM_TO (0x620, 0x640);

    struct {
      u32 error_detect;
      u32 error_rate_enable;
      struct {
	u32 attributes;
	u32 symbol;
	u32 data[3];
      } capture;
      CLIB_PAD_FROM_TO (0x65c, 0x668);
      u32 rate_command;
      u32 rate_threshold;
      CLIB_PAD_FROM_TO (0x670, 0x680);
    } per_port[2];
  } error_reporting;
  CLIB_PAD_FROM_TO (0xc06c0, 0xd0000);

  struct {
    CLIB_PAD_FROM_TO (0x0, 0x4);
    /* [30] 0 => message unit assigned letters 0-3
            1 => message unit letters 0-2, dma letter 3
       [29] external reg access block. */
    u32 logical_layer_config;
    CLIB_PAD_FROM_TO (0x8, 0x10);

    /* [0] port write
       [1] message unit
       [31] port 0 error
       [30] port 1 error. */
    u32 interrupt_status;
    CLIB_PAD_FROM_TO (0x14, 0x20);

    u32 logical_retry_error_threshold;
    CLIB_PAD_FROM_TO (0x24, 0x80);

    u32 physical_retry_error_threshold;
    CLIB_PAD_FROM_TO (0x84, 0x100);

    struct {
      /* [31] enable
	 [23:16] id for small system
	 [15:0] id for large system. */
      u32 alternate_device_id;
      CLIB_PAD_FROM_TO (0x104, 0x120);

      u32 accept_all_packets_without_checking_target_id;

      /* [31:8] value. */
      u32 tx_packet_time_to_live;
      CLIB_PAD_FROM_TO (0x128, 0x130);

      /* [31] retry error. */
      u32 error_write_1_to_clear;
      CLIB_PAD_FROM_TO (0x134, 0x140);

      /* [2] output drain enable
	 [4] packet crc check enable
	 [15] control symbol crc check enable. */
      u32 config;
      CLIB_PAD_FROM_TO (0x144, 0x158);

      /* [31:24] lane i sync bit (31 - i)
	 [23] lanes aligned. */
      u32 serial_link_write_1_to_clear;
      CLIB_PAD_FROM_TO (0x15c, 0x160);

      u32 link_error_inject;
      CLIB_PAD_FROM_TO (0x164, 0x180);
   } per_port[2];
  } port_regs;
  CLIB_PAD_FROM_TO (0xd0200, 0xd0bf8);

  u32 ip_block_revision[2];

  /* 2 ports x (9 tx [1-8 plus default], 2 unused, 5 rx [1-4 plus default]) */
  struct {
    /* lo/hi bits of rapidio translation for OCN address. */
    u32 rapidio_address[2];

    /* [0:23] upper 24 bits of OCN address (low 12 bits zero). */
    u32 cpu_address_35_13;
    CLIB_PAD_FROM_TO (0xc, 0x10);

    /* [31] enable
       [30] rx protected
       [27:26] priority (lo 0, next 1, hi 2, reserved 3)
       [25] pci ordering enable

       for tx:
       [23:22] log2 n segments for this window
       [21:20] log2 n device ids for each segment

       for rx:
       [23:20] target interface (0x[012] => pcie [321],
         0xc/0xd => rapidio 1/2,
         0xf local memory (ddr, local bus, mem mapped regs).

       [19:16] rapidio ttype for reads (segment 0)
       [15:12] rapidio ttype for writes (segment 0)
       [5:0] log2 window number of bytes - 1 (e.g. 4k => 11). */
    u32 attributes;

    /* [27:26] priority (as above)
       [23:20] rapidio ttype for reads
       [19:16] rapidio ttype for writes
       [7:0] low bits of target id for this segment. */
    u32 tx_segments[3];
  } address_translation_windows[2][16];
  CLIB_PAD_FROM_TO (0xd1000, 0xd3000);

  /* 2 message units, 2 reserved, 1 doorbell + port_write = 5 message units. */
  struct {
    struct {
      /* [31:28] log2 number of rx frames before message in queue interrupt
	 [27:25] log2 n + 1 tx descriptors to process before moving to next queue
	        0 => fixed priority
	 [20] descriptor snoop enable
	 [19:16] rx frame buffer size (log2 size - 1) from 8 to 4k bytes
	 [15:12] descriptor circular buffer size (log2 size - 1)
	 [9] queue overflow interrupt enable (increment when head == tail)
	 [8] queue full interrupt enable (head == tail)
	 [6] tx/rx queue empty/non-empty interrupt enable
	 [5] error interrupt enable
	 [2] direct mode enable (versus descriptor chaining mode)
	     in direct mode the current_descriptor[01] registers are used as the descriptor.
	 [1] increment enqueue address
	 [0] start. */
      u32 mode;

      /* [20] queue full (read only)
	 [16] rx message in queue (read only)
	 [12] message error response
	 [11] retry threshold exceeded
	 [10] timeout
	 [7] transaction error
	 [5] queue overflow
	 [4] queue full
	 [3] port write discarded
	 [2] busy (read only)
	 [1] tx end of message, rx queue empty (read only)
	 [0] tx queue empty, rx message in queue. */
      u32 status_write_1_to_clear;

      /* 32 byte aligned.  36 bit address hi/lo.
	 Hardware dequeues descriptors here and increments address. */
      u32 descriptor_dequeue_address[2];

      /* Fields defining descriptor (0). */
      struct {
	/* [35:3] address
	   [2] snoop enable. */
	u32 src_buffer_address[2];

	/* [31:16] destination id of target
	   [5:2] extended mailbox
	   [1:0] mailbox. */
	u32 dst_port;

	/* [31] multicast mode
	   [29] end of message/doorbell interrupt enable
	   [27:26] priority
	   [23:20] rapidio port.
	   [15:0] doorbell info field. */
	u32 dst_attributes;

	/* Must be of the form 2^i for i >= 3 and i <= 12. */
	u32 n_transfer_bytes;
      } current_descriptor0;

      /* CPU writes descriptors here. */
      u32 descriptor_enqueue_address[2];

      u32 retry_error_threshold;

      /* Continuation of fields that define current descriptor. */
      struct {
	u32 multicast_group;
	u32 multicast_list;
      } current_descriptor1;

      CLIB_PAD_FROM_TO (0x38, 0x60);
    } tx;

    struct {
      /* See tx mode/status above. */
      u32 mode;
      u32 status_write_1_to_clear;

      /* Hardware dequeues descriptors here and increments address. */
      u32 frame_dequeue_address[2];
      u32 frame_enqueue_address[2];

      /* [31:8] time in units of 168/bus-frequency (e.g. 400MHz). */
      u32 max_interrupt_report;
      CLIB_PAD_FROM_TO (0x7c, 0xe0);
    } rx;

    struct {
      /* See tx mode/status above. */
      u32 mode;
      u32 status_write_1_to_clear;

      /* Only one 64 byte buffer in queue. */
      u32 queue_base_address[2];
    } rx_port_write;
    CLIB_PAD_FROM_TO (0xf0, 0x100);
  } message_units[5];
} freescale_rapidio_regs_t;

#endif /* included_freescale_rapidio_h */
