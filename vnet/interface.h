/*
 * interface.h: VNET interfaces/sub-interfaces
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

#ifndef included_vnet_interface_h
#define included_vnet_interface_h

#include <vnet/l3_types.h>

struct vnet_main_t;

/* Interface up/down callback. */
typedef clib_error_t * (vnet_interface_function_t)
  (struct vnet_main_t * vm, u32 if_index, u32 flags);

#define _VNET_INTERFACE_FUNCTION_DECL(f,tag)				\
  vnet_interface_function_t * _##tag##_##f CLIB_ELF_SECTION ("vnet_" #tag "_functions")

#define VNET_HW_INTERFACE_ADD_DEL_FUNCTION(f)			\
  _VNET_INTERFACE_FUNCTION_DECL(f,hw_interface_add_del) = f
#define VNET_HW_INTERFACE_LINK_UP_DOWN_FUNCTION(f)		\
  _VNET_INTERFACE_FUNCTION_DECL(f,hw_interface_link_up_down) = f
#define VNET_SW_INTERFACE_ADD_DEL_FUNCTION(f)			\
  _VNET_INTERFACE_FUNCTION_DECL(f,sw_interface_add_del) = f
#define VNET_SW_INTERFACE_ADMIN_UP_DOWN_FUNCTION(f)		\
  _VNET_INTERFACE_FUNCTION_DECL(f,sw_interface_admin_up_down) = f

/* A class of hardware interface devices. */
typedef struct {
  /* Index into main vector. */
  u32 index;

  /* Device name (e.g. "FOOBAR 1234a"). */
  char * name;

  /* Function to call when hardware interface is added/deleted. */
  vnet_interface_function_t * interface_add_del_function;

  /* Function to bring device administratively up/down. */
  vnet_interface_function_t * admin_up_down_function;

  /* Redistribute flag changes/existence of this interface class. */
  u32 redistribute;

  /* Transmit function. */
  vlib_node_function_t * tx_function;

  /* Error strings indexed by error code for this node. */
  char ** tx_function_error_strings;

  /* Number of error codes used by this node. */
  u32 tx_function_n_errors;

  /* Format device instance as name. */
  format_function_t * format_device_name;

  /* Parse function for device name. */
  unformat_function_t * unformat_device_name;

  /* Format device verbosely for this class. */
  format_function_t * format_device;

  /* Trace buffer format for TX function. */
  format_function_t * format_tx_trace;

  /* Function to clear hardware counters for device. */
  void (* clear_counters) (u32 dev_class_instance);

  uword (* is_valid_class_for_interface) (struct vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index);

  /* Called when hardware class of an interface changes. */
  void ( * hw_class_change) (struct vnet_main_t * vm,
			     u32 hw_if_index,
			     u32 new_hw_class_index);
} vnet_device_class_t;

#define VNET_DEVICE_CLASS(x) vnet_device_class_t x CLIB_ELF_SECTION ("vnet_device_class")

/* Layer-2 (e.g. Ethernet) interface class. */
typedef struct {
  /* Index into main vector. */
  u32 index;

  /* Class name (e.g. "Ethernet"). */
  char * name;

  /* Function to call when hardware interface is added/deleted. */
  vnet_interface_function_t * interface_add_del_function;

  /* Function to bring interface administratively up/down. */
  vnet_interface_function_t * admin_up_down_function;

  /* Function to call when link state changes. */
  vnet_interface_function_t * link_up_down_function;

  /* Format function to display interface name. */
  format_function_t * format_interface_name;

  /* Format function to display interface address. */
  format_function_t * format_address;

  /* Format packet header for this interface class. */
  format_function_t * format_header;

  /* Format device verbosely for this class. */
  format_function_t * format_device;

  /* Parser for hardware (e.g. ethernet) address. */
  unformat_function_t * unformat_hw_address;

  /* Parser for packet header for e.g. rewrite string. */
  unformat_function_t * unformat_header;

  /* Node to fixup rewrite strings before output. */
  char * rewrite_fixup_node;

  /* To be filled in when class is registered. */
  u32 rewrite_fixup_node_index;

  /* Forms adjacency for given l3 packet type and destination address.
     Returns number of bytes in adjacency. */
  uword (* rewrite_for_sw_interface)
    (struct vnet_main_t * vm,
     u32 sw_if_index,
     vnet_l3_packet_type_t l3_packet_type,
     void * dst_address,
     void * rewrite,
     uword max_rewrite_bytes);

  /* Set up rewrite string for hardware interface.  This is used by
     rewrite fixup routines to stash away per-rewrite data for fixup node. */
  void (* rewrite_for_hw_interface)
    (struct vnet_main_t * vm,
     u32 hw_if_index,
     void * rewrite);

  uword (* is_valid_class_for_interface) (struct vnet_main_t * vm, u32 hw_if_index, u32 hw_class_index);

  /* Called when hw interface class is changed and old hardware instance
     may want to be deleted. */
  void (* hw_class_change) (struct vnet_main_t * vm, u32 hw_if_index, u32 old_class_index, u32 new_class_index);
} vnet_hw_interface_class_t;

#define VNET_HW_INTERFACE_CLASS(x) vnet_hw_interface_class_t x CLIB_ELF_SECTION("vnet_hw_interface_class")

/* Hardware-interface.  This corresponds to a physical wire
   that packets flow over. */
typedef struct vnet_hw_interface_t {
  /* Interface name. */
  u8 * name;

  u32 flags;
  /* Hardware link state is up. */
#define VNET_HW_INTERFACE_FLAG_LINK_UP (1 << 0)

  /* Hardware address as vector.  Zero (e.g. zero-length vector) if no
     address for this class (e.g. PPP). */
  u8 * hw_address;

  /* Interface is up as far as software is concerned. */
  /* NAME.{output,tx} nodes for this interface. */
  u32 output_node_index, tx_node_index;

  /* (dev_class, dev_instance) uniquely identifies hw interface. */
  u32 dev_class_index;
  u32 dev_instance;

  /* (hw_class, hw_instance) uniquely identifies hw interface. */
  u32 hw_class_index;
  u32 hw_instance;

  /* Hardware index for this hardware interface. */
  u32 hw_if_index;

  /* Software index for this hardware interface. */
  u32 sw_if_index;

  /* Maximum transmit rate for this interface in bits/sec. */
  f64 max_rate_bits_per_sec;

  /* Smallest packet size for this interface. */
  u32 min_packet_bytes;

  /* Number of extra bytes that go on the wire.
     Packet length on wire
     = max (length + per_packet_overhead_bytes, min_packet_bytes). */
  u32 per_packet_overhead_bytes;

  /* Receive and transmit layer 3 packet size limits (MRU/MTU). */
  u32 max_l3_packet_bytes[VLIB_N_RX_TX];

  /* Hash table mapping sub interface id to sw_if_index. */
  uword * sub_interface_sw_if_index_by_id;
} vnet_hw_interface_t;

typedef enum {
  /* A hw interface. */
  VNET_SW_INTERFACE_TYPE_HARDWARE,

  /* A sub-interface. */
  VNET_SW_INTERFACE_TYPE_SUB,
} vnet_sw_interface_type_t;

typedef struct {
  /* VLAN id, ATM vc, etc. */
  u32 id;
} vnet_sub_interface_t;

/* Software-interface.  This corresponds to a Ethernet VLAN, ATM vc, a
   tunnel, etc.  Configuration (e.g. IP address) gets attached to
   software interface. */
typedef struct {
  vnet_sw_interface_type_t type : 16;

  u16 flags;
  /* Interface is "up" meaning adminstratively up.
     Up in the sense of link state being up is maintained by hardware interface. */
#define VNET_SW_INTERFACE_FLAG_ADMIN_UP (1 << 0)

  /* Interface is disabled for forwarding: punt all traffic to slow-path. */
#define VNET_SW_INTERFACE_FLAG_PUNT (1 << 1)

  /* Index for this interface. */
  u32 sw_if_index;

  /* Software interface index of super-interface;
     equal to sw_if_index if this interface is not a
     sub-interface. */
  u32 sup_sw_if_index;

  union {
    /* VNET_SW_INTERFACE_TYPE_HARDWARE. */
    u32 hw_if_index;

    /* VNET_SW_INTERFACE_TYPE_SUB. */
    vnet_sub_interface_t sub;

    /* SW interfaces are sorted by type and key. */
    u32 sort_key;
  };
} vnet_sw_interface_t;

typedef enum {
  /* Simple counters. */
  VNET_INTERFACE_COUNTER_DROP = 0,
  VNET_INTERFACE_COUNTER_PUNT = 1,
  VNET_N_SIMPLE_INTERFACE_COUNTER = 2,
  /* Combined counters. */
  VNET_INTERFACE_COUNTER_RX = 0,
  VNET_INTERFACE_COUNTER_TX = 1,
  VNET_N_COMBINED_INTERFACE_COUNTER = 2,
} vnet_interface_counter_type_t;

typedef struct {
  u32 output_node_index;
  u32 tx_node_index;
} vnet_hw_interface_nodes_t;

typedef struct {
  /* Hardware interfaces. */
  vnet_hw_interface_t * hw_interfaces;

  /* Hash table mapping HW interface name to index. */
  uword * hw_interface_by_name;

  /* Vectors if hardware interface classes and device classes. */
  vnet_hw_interface_class_t * hw_interface_classes;
  vnet_device_class_t * device_classes;

  /* Hash table mapping name to hw interface/device class. */
  uword * hw_interface_class_by_name;
  uword * device_class_by_name;

  /* Software interfaces. */
  vnet_sw_interface_t * sw_interfaces;

  /* Software interface counters both simple and combined
     packet and byte counters. */
  vlib_simple_counter_main_t * sw_if_counters;
  vlib_combined_counter_main_t * combined_sw_if_counters;

  vnet_hw_interface_nodes_t * deleted_hw_interface_nodes;
} vnet_interface_main_t;

#endif /* included_vnet_interface_h */
