/*
 * ethernet.h: types/functions for ethernet.
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

#ifndef included_ethernet_h
#define included_ethernet_h

#include <ethernet/phy.h>

typedef enum {
#define ethernet_type(n,s) ETHERNET_TYPE_##s = n,
#include "ethernet/types.def"
#undef ethernet_type
} ethernet_type_t;

typedef struct {
  /* Source/destination address. */
  u8 dst_address[6];
  u8 src_address[6];

  /* Ethernet type. */
  u16 type;
} ethernet_header_t;

static inline u64
ethernet_mac_address_u64 (u8 * a)
{ return (((u64) a[0] << (u64) (5*8))
	  | ((u64) a[1] << (u64) (4*8))
	  | ((u64) a[2] << (u64) (3*8))
	  | ((u64) a[3] << (u64) (2*8))
	  | ((u64) a[4] << (u64) (1*8))
	  | ((u64) a[5] << (u64) (0*8))); }

/* For VLAN ethernet type. */
typedef struct {
  /* 3 bit priority, 1 bit CFI and 12 bit vlan id. */
  u16 priority_cfi_and_id;

#define ETHERNET_N_VLAN (1 << 12)

  /* Inner ethernet type. */
  u16 type;
} ethernet_vlan_header_t;

/* Max. sized ethernet/vlan header for parsing. */
typedef struct {
  ethernet_header_t ethernet;

  /* Allow up to 2 stacked vlan headers. */
  ethernet_vlan_header_t vlan[2];
} ethernet_max_header_t;

/* Ethernet interface instance. */
typedef struct {
  u32 flags;
  
  /* Accept all packets (promiscuous mode). */
#define ETHERNET_INTERFACE_FLAG_ACCEPT_ALL (1 << 0)

  u32 driver_instance;

  ethernet_phy_t phy;
} ethernet_interface_t;

extern vlib_hw_interface_class_t ethernet_hw_interface_class;

typedef struct {
  /* Name (a c string). */
  char * name;

  /* Ethernet type in host byte order. */
  ethernet_type_t type;

  /* Node which handles this type. */
  u32 node_index;

  /* Next index for this type. */
  u32 next_index;

  /* Format function for this ether type. */
  format_function_t * format_header;

  /* Packet generator header parser for this ether type. */
  unformat_function_t * unformat_pg_edit;
} ethernet_type_info_t;

typedef enum {
#define ethernet_error(n,c,s) ETHERNET_ERROR_##n,
#include "error.def"
#undef ethernet_error
  ETHERNET_N_ERROR,
} ethernet_error_t;

typedef struct {
  i32 * vlan_to_sw_if_index;
} ethernet_vlan_mapping_t;

typedef struct {
  vlib_main_t * vlib_main;

  /* Pool of ethernet interface instances. */
  ethernet_interface_t * interfaces;

  ethernet_type_info_t * type_infos;

  /* Hash tables mapping name/type to type info index. */
  uword * type_info_by_name, * type_info_by_type;

  ethernet_vlan_mapping_t * vlan_mapping_by_sw_if_index;
} ethernet_main_t;

static inline ethernet_type_info_t *
ethernet_get_type_info (ethernet_main_t * em, ethernet_type_t type)
{
  uword * p = hash_get (em->type_info_by_type, type);
  return p ? vec_elt_at_index (em->type_infos, p[0]) : 0;
}

static inline ethernet_interface_t *
ethernet_get_interface (ethernet_main_t * em, u32 hw_if_index)
{
  vlib_hw_interface_t * i = vlib_get_hw_interface (em->vlib_main, hw_if_index);
  return pool_elt_at_index (em->interfaces, i->hw_instance);
}

static always_inline u32
ethernet_vlan_to_sw_if_index (ethernet_vlan_mapping_t * m,
			      u32 vlan,
			      u32 is_vlan)
{
  u32 i = is_vlan ? vlan : ETHERNET_N_VLAN;
  ASSERT (i < vec_len (m->vlan_to_sw_if_index));
  return m->vlan_to_sw_if_index[i];
}

extern ethernet_main_t ethernet_main;

/* Fetch ethernet main structure possibly calling init function. */
ethernet_main_t * ethernet_get_main (vlib_main_t * vm);

clib_error_t *
ethernet_register_interface (vlib_main_t * vm,
			     vlib_device_class_t * dev_class,
			     u32 dev_instance,
			     ethernet_phy_t * phy,
			     u32 * hw_if_index_return);

/* Register given node index to take input for given ethernet type. */
void
ethernet_register_input_type (vlib_main_t * vm,
			      ethernet_type_t type,
			      u32 node_index);

/* Formats ethernet address X:X:X:X:X:X */
u8 * format_ethernet_address (u8 * s, va_list * args);
u8 * format_ethernet_type (u8 * s, va_list * args);
u8 * format_ethernet_header (u8 * s, va_list * args);
u8 * format_ethernet_header_with_length (u8 * s, va_list * args);

/* Parse ethernet address in either X:X:X:X:X:X unix or X.X.X cisco format. */
uword
unformat_ethernet_address (unformat_input_t * input, va_list * args);

/* Parse ethernet type as 0xXXXX or type name from ethernet/types.def.
   In either host or network byte order. */
uword
unformat_ethernet_type_host_byte_order (unformat_input_t * input,
					va_list * args);
uword
unformat_ethernet_type_net_byte_order (unformat_input_t * input,
				       va_list * args);

/* Parse ethernet header. */
uword
unformat_ethernet_header (unformat_input_t * input, va_list * args);

/* Parse ethernet interface name; return hw_if_index. */
uword unformat_ethernet_interface (unformat_input_t * input, va_list * args);

uword unformat_pg_ethernet_header (unformat_input_t * input, va_list * args);

#endif /* included_ethernet_h */
