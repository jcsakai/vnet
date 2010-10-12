/*
 * ip/ip4.h: ip4 main include file
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

#ifndef included_ip_ip4_h
#define included_ip_ip4_h

#include <vlib/mc.h>
#include <vnet/ip/ip4_packet.h>
#include <vnet/ip/lookup.h>

typedef struct {
  /* Hash table for each prefix length mapping. */
  uword * adj_index_by_dst_address[33];

  /* Temporary vectors for holding new/old values for hash_set. */
  uword * new_hash_values, * old_hash_values;

  /* Table ID (hash key) for this FIB. */
  u32 table_id;

  /* Index into FIB vector. */
  u32 index;
} ip4_fib_t;

struct ip4_main_t;

typedef void (ip4_add_del_route_function_t)
  (struct ip4_main_t * im,
   uword opaque,
   ip4_fib_t * fib,
   u32 flags,
   ip4_address_t * address,
   u32 address_length,
   void * old_result,
   void * new_result);

typedef struct {
  ip4_add_del_route_function_t * function;
  uword function_opaque;
} ip4_add_del_route_callback_t;

typedef void (ip4_set_interface_address_function_t)
  (struct ip4_main_t * im,
   uword opaque,
   u32 sw_if_index,
   ip4_address_t * address,
   u32 address_length);

typedef struct {
  ip4_set_interface_address_function_t * function;
  uword function_opaque;
} ip4_set_interface_address_callback_t;

typedef enum {
  /* First check access list to either permit or deny this
     packet based on classification. */
  IP4_RX_FEATURE_CHECK_ACCESS,

  /* RPF check: verify that source address is reachable via
     RX interface or via any interface. */
  IP4_RX_FEATURE_SOURCE_CHECK_REACHABLE_VIA_RX,
  IP4_RX_FEATURE_SOURCE_CHECK_REACHABLE_VIA_ANY,

  /* Must be last: perform forwarding lookup. */
  IP4_RX_FEATURE_LOOKUP,

  IP4_N_RX_FEATURE,
} ip4_rx_feature_type_t;

typedef struct ip4_main_t {
  ip_lookup_main_t lookup_main;

  /* Vector of FIBs. */
  ip4_fib_t * fibs;

  u32 fib_masks[33];

  /* Table index indexed by software interface. */
  u32 * fib_index_by_sw_if_index;

  /* Hash table mapping table id to fib index.
     ID space is not necessarily dense; index space is dense. */
  uword * fib_index_by_table_id;

  /* Vector of functions to call when routes are added/deleted. */
  ip4_add_del_route_callback_t * add_del_route_callbacks;

  /* IP4 address and length (e.g. netmask) for interfaces.
     Address of ~0 means interface has no IP4 address. */
  ip4_address_t * ip4_address_by_sw_if_index;
  u8 * ip4_address_length_by_sw_if_index;

  /* Hash table mapping interface rewrite adjacency index by sw if index. */
  uword * interface_adj_index_by_sw_if_index;

  /* Functions to call when interface address changes. */
  ip4_set_interface_address_callback_t * set_interface_address_callbacks;

  /* Template used to generate IP4 ARP packets. */
  vlib_packet_template_t ip4_arp_request_packet_template;

  /* Seed for Jenkins hash used to compute ip4 flow hash. */
  u32 flow_hash_seed;

  struct {
    /* TTL to use for host generated packets. */
    u8 ttl;

    u8 pad[3];
  } host_config;
} ip4_main_t;

/* Global ip4 main structure. */
extern ip4_main_t ip4_main;

/* Global ip4 input node.  Errors get attached to ip4 input node. */
extern vlib_node_registration_t ip4_input_node;
extern vlib_node_registration_t ip4_rewrite_node;
extern vlib_node_registration_t ip4_arp_node;

u32 ip4_fib_lookup_with_table (ip4_main_t * im, u32 fib_index, ip4_address_t * dst,
			       u32 disable_default_route);

always_inline ip4_address_t *
ip4_get_interface_address (ip4_main_t * im, u32 sw_if_index)
{ return vec_elt_at_index (im->ip4_address_by_sw_if_index, sw_if_index); }

always_inline uword
ip4_get_interface_address_length (ip4_main_t * im, u32 sw_if_index)
{ return vec_elt (im->ip4_address_length_by_sw_if_index, sw_if_index); }

always_inline uword
ip4_interface_address_is_valid (ip4_address_t * a)
{ return a->data_u32 != ~0; }

always_inline uword
ip4_destination_matches_route (ip4_main_t * im,
			       ip4_address_t * key,
			       ip4_address_t * dest,
			       uword dest_length)
{ return 0 == ((key->data_u32 ^ dest->data_u32) & im->fib_masks[dest_length]); }

/* As above but allows for unaligned destinations (e.g. works right from IP header of packet). */
always_inline uword
ip4_unaligned_destination_matches_route (ip4_main_t * im,
					 ip4_address_t * key,
					 ip4_address_t * dest,
					 uword dest_length)
{ return 0 == ((clib_mem_unaligned (&key->data_u32, u32) ^ dest->data_u32) & im->fib_masks[dest_length]); }

void
ip4_set_interface_address (vlib_main_t * vm, u32 sw_if_index,
			   ip4_address_t * to_set, u32 to_set_length);

int ip4_address_compare (ip4_address_t * a1, ip4_address_t * a2);

/* Add/del a route to the FIB. */

#define IP4_ROUTE_FLAG_ADD (0 << 0)
#define IP4_ROUTE_FLAG_DEL (1 << 0)
#define IP4_ROUTE_FLAG_TABLE_ID  (0 << 1)
#define IP4_ROUTE_FLAG_FIB_INDEX (1 << 1)
#define IP4_ROUTE_FLAG_NO_REDISTRIBUTE (1 << 2)
#define IP4_ROUTE_FLAG_KEEP_OLD_ADJACENCY (1 << 3)

typedef struct {
  /* IP4_ROUTE_FLAG_* */
  u32 flags;

  /* Either index of fib or table_id to hash and get fib.
     IP4_ROUTE_FLAG_FIB_INDEX specifies index; otherwise table_id is assumed. */
  u32 table_index_or_table_id;

  /* Destination address (prefix) and length. */
  ip4_address_t dst_address;
  u32 dst_address_length;

  /* Adjacency to use for this destination. */
  u32 adj_index;

  /* If specified adjacencies to add and then
     use for this destination.  add_adj/n_add_adj
     are override adj_index if specified. */
  ip_adjacency_t * add_adj;
  u32 n_add_adj;
} ip4_add_del_route_args_t;

void ip4_add_del_route (ip4_main_t * im, ip4_add_del_route_args_t * args);

void ip4_add_del_route_next_hop (ip4_main_t * im,
				 u32 flags,
				 ip4_address_t * dst_address,
				 u32 dst_address_length,
				 ip4_address_t * next_hop,
				 u32 next_hop_sw_if_index,
				 u32 next_hop_weight);

void *
ip4_get_route (ip4_main_t * im,
	       u32 fib_index_or_table_id,
	       u32 flags,
	       u8 * address,
	       u32 address_length);

void
ip4_foreach_matching_route (ip4_main_t * im,
			    u32 table_index_or_table_id,
			    u32 flags,
			    u8 * address,
			    u32 address_length,
			    ip4_address_t ** results,
			    u8 ** result_lengths);

void ip4_delete_matching_routes (ip4_main_t * im,
				 u32 table_index_or_table_id,
				 u32 flags,
				 u8 * address,
				 u32 address_length);

void ip4_maybe_remap_adjacencies (ip4_main_t * im,
				  u32 table_index_or_table_id,
				  u32 flags);

void ip4_adjacency_set_interface_route (vlib_main_t * vm, ip_adjacency_t * adj, u32 sw_if_index);

uword
ip4_tcp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index);
uword
ip4_udp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index);

serialize_function_t serialize_vnet_ip4_main, unserialize_vnet_ip4_main;

#endif /* included_ip_ip4_h */
