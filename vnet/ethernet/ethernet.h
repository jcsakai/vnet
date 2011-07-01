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

#include <vnet/ethernet/packet.h>
#include <vnet/ethernet/phy.h>
#include <vnet/pg/pg.h>

always_inline u64
ethernet_mac_address_u64 (u8 * a)
{ return (((u64) a[0] << (u64) (5*8))
	  | ((u64) a[1] << (u64) (4*8))
	  | ((u64) a[2] << (u64) (3*8))
	  | ((u64) a[3] << (u64) (2*8))
	  | ((u64) a[4] << (u64) (1*8))
	  | ((u64) a[5] << (u64) (0*8))); }

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

  /* Ethernet (MAC) address for this interface. */
  u8 address[6];

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
} ethernet_type_info_t;

typedef enum {
#define ethernet_error(n,c,s) ETHERNET_ERROR_##n,
#include <vnet/ethernet/error.def>
#undef ethernet_error
  ETHERNET_N_ERROR,
} ethernet_error_t;

typedef struct {
  u32 * vlan_to_sw_if_index;
} ethernet_vlan_mapping_t;

/* Per VLAN state. */
typedef struct {
  /* ARP table. */
} ethernet_vlan_t;

typedef struct {
  vlib_main_t * vlib_main;

  /* Sparse vector mapping ethernet type in network byte order
     to next index. */
  u16 * input_next_by_type;

  u32 * sparse_index_by_input_next_index;

  /* Pool of ethernet interface instances. */
  ethernet_interface_t * interfaces;

  ethernet_type_info_t * type_infos;

  /* Hash tables mapping name/type to type info index. */
  uword * type_info_by_name, * type_info_by_type;

  /* Each software interface gets a VLAN mapping table which maps VLAN
     id to sw_if_index.  Normally only sw ifs corresponding to hw ifs
     get tables.  But, when 2 VLAN tags are present it is valid to
     have 2 levels of tables. */
  ethernet_vlan_mapping_t * vlan_mapping_by_sw_if_index;

  /* Per VLAN state. */
  ethernet_vlan_t * vlans;

  /* Hash indexed by VLAN ID. */
  uword * vlan_index_by_1_vlan_id;

  /* Hash indexed by 24 bits of (inner << 12) | outer VLAN IDs. */
  uword * vlan_index_by_2_vlan_id;

  /* Set to one to use AB.CD.EF instead of A:B:C:D:E:F as ethernet format. */
  int format_ethernet_address_16bit;
} ethernet_main_t;

always_inline ethernet_type_info_t *
ethernet_get_type_info (ethernet_main_t * em, ethernet_type_t type)
{
  uword * p = hash_get (em->type_info_by_type, type);
  return p ? vec_elt_at_index (em->type_infos, p[0]) : 0;
}

extern ethernet_main_t ethernet_main;

/* Fetch ethernet main structure possibly calling init function. */
ethernet_main_t * ethernet_get_main (vlib_main_t * vm);

always_inline uword
is_ethernet_interface (u32 hw_if_index)
{
  ethernet_main_t * em = &ethernet_main;
  vlib_hw_interface_t * i = vlib_get_hw_interface (em->vlib_main, hw_if_index);
  vlib_hw_interface_class_t * c = vlib_get_hw_interface_class (em->vlib_main, i->hw_class_index);
  return ! strcmp (c->name, ethernet_hw_interface_class.name);
}

always_inline ethernet_interface_t *
ethernet_get_interface (ethernet_main_t * em, u32 hw_if_index)
{
  vlib_hw_interface_t * i = vlib_get_hw_interface (em->vlib_main, hw_if_index);
  return (is_ethernet_interface (hw_if_index)
	  ? pool_elt_at_index (em->interfaces, i->hw_instance)
	  : 0);
}

always_inline u32
ethernet_vlan_to_sw_if_index (ethernet_vlan_mapping_t * m,
			      u32 vlan,
			      u32 is_vlan)
{
  u32 i = is_vlan ? vlan : ETHERNET_N_VLAN;
  ASSERT (i < vec_len (m->vlan_to_sw_if_index));
  return m->vlan_to_sw_if_index[i];
}

clib_error_t *
ethernet_register_interface (vlib_main_t * vm,
			     u32 dev_class_index,
			     u32 dev_instance,
			     u8 * address,
			     ethernet_phy_t * phy,
			     u32 * hw_if_index_return);

void ethernet_delete_interface (vlib_main_t * vm, u32 hw_if_index);

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

always_inline void
ethernet_setup_node (vlib_main_t * vm, u32 node_index)
{
  vlib_node_t * n = vlib_get_node (vm, node_index);
  pg_node_t * pn = pg_get_node (node_index);

  n->format_buffer = format_ethernet_header_with_length;
  n->unformat_buffer = unformat_ethernet_header;
  pn->unformat_edit = unformat_pg_ethernet_header;
}

typedef struct {
  /* Saved value of current header by ethernet-input. */
  u32 start_of_ethernet_header;
} ethernet_buffer_opaque_t;

always_inline ethernet_header_t *
ethernet_buffer_get_header (vlib_buffer_t * b)
{
  ethernet_buffer_opaque_t * o = vlib_get_buffer_opaque (b);
  return (void *) (b->data + o->start_of_ethernet_header);
}

#endif /* included_ethernet_h */
