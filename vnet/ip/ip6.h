/*
 * ip/ip6.h: ip6 main include file
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

#ifndef included_ip_ip6_h
#define included_ip_ip6_h

#include <vlib/mc.h>
#include <vnet/ip/ip6_packet.h>
#include <vnet/ip/lookup.h>
#include <clib/mhash.h>

/* Hash table for each prefix length mapping. */
typedef struct {
  mhash_t adj_index_by_dst_address;

  u32 dst_address_length;
} ip6_fib_mhash_t;

typedef struct {
  ip6_fib_mhash_t * non_empty_dst_address_length_mhash;

  u8 mhash_index_by_dst_address_length[129];

  /* Temporary vectors for holding new/old values for hash_set. */
  uword * new_hash_values, * old_hash_values;

  /* Table ID (hash key) for this FIB. */
  u32 table_id;

  /* Index into FIB vector. */
  u32 index;
} ip6_fib_t;

always_inline ip6_fib_mhash_t *
ip6_fib_get_dst_address_length (ip6_fib_t * f, u32 dst_address_length)
{
  ip6_fib_mhash_t * mh;
  ASSERT (dst_address_length < ARRAY_LEN (f->mhash_index_by_dst_address_length));
  mh = vec_elt_at_index (f->non_empty_dst_address_length_mhash,
			 f->mhash_index_by_dst_address_length[dst_address_length]);
  ASSERT (mh->dst_address_length == dst_address_length);
  return mh;
}

struct ip6_main_t;

typedef void (ip6_add_del_route_function_t)
  (struct ip6_main_t * im,
   uword opaque,
   ip6_fib_t * fib,
   u32 flags,
   ip6_address_t * address,
   u32 address_length,
   void * old_result,
   void * new_result);

typedef struct {
  ip6_add_del_route_function_t * function;
  uword function_opaque;
} ip6_add_del_route_callback_t;

typedef void (ip6_set_interface_address_function_t)
  (struct ip6_main_t * im,
   uword opaque,
   u32 sw_if_index,
   ip6_address_t * address,
   u32 address_length);

typedef struct {
  ip6_set_interface_address_function_t * function;
  uword function_opaque;
} ip6_set_interface_address_callback_t;

typedef struct ip6_main_t {
  ip_lookup_main_t lookup_main;

  /* Vector of FIBs. */
  ip6_fib_t * fibs;

  ip6_address_t fib_masks[129];

  /* Table index indexed by software interface. */
  u32 * fib_index_by_sw_if_index;

  /* Hash table mapping table id to fib index.
     ID space is not necessarily dense; index space is dense. */
  uword * fib_index_by_table_id;

  /* Vector of functions to call when routes are added/deleted. */
  ip6_add_del_route_callback_t * add_del_route_callbacks;

  /* IP6 address and length (e.g. netmask) for interfaces.
     Address of ~0 means interface has no IP6 address. */
  ip6_address_t * ip6_address_by_sw_if_index;
  u8 * ip6_address_length_by_sw_if_index;

  /* Hash table mapping interface rewrite adjacency index by sw if index. */
  uword * interface_adj_index_by_sw_if_index;

  /* Functions to call when interface address changes. */
  ip6_set_interface_address_callback_t * set_interface_address_callbacks;

  /* Template used to generate IP6 neighbor solicitation packets. */
  vlib_packet_template_t ip6_neighbor_solicitation_packet_template;

  /* Seed for Jenkins hash used to compute ip6 flow hash. */
  u32 flow_hash_seed;

  struct {
    /* TTL to use for host generated packets. */
    u8 ttl;

    u8 pad[3];
  } host_config;
} ip6_main_t;

/* Global ip6 main structure. */
extern ip6_main_t ip6_main;

/* Global ip6 input node.  Errors get attached to ip6 input node. */
extern vlib_node_registration_t ip6_input_node;
extern vlib_node_registration_t ip6_rewrite_node;
extern vlib_node_registration_t ip6_arp_node;

always_inline ip6_address_t *
ip6_get_interface_address (ip6_main_t * im, u32 sw_if_index)
{ return vec_elt_at_index (im->ip6_address_by_sw_if_index, sw_if_index); }

always_inline uword
ip6_get_interface_address_length (ip6_main_t * im, u32 sw_if_index)
{ return vec_elt (im->ip6_address_length_by_sw_if_index, sw_if_index); }

always_inline uword
ip6_interface_address_is_valid (ip6_address_t * a)
{
  int i;
  for (i = 0; i < ARRAY_LEN (a->data_u32); i++)
    if (a->data_u32[i] != ~0)
      return 1;
  return 0;
}

always_inline void
ip6_interface_address_set_invalid (ip6_address_t * a)
{
  int i;
  for (i = 0; i < ARRAY_LEN (a->data_u32); i++)
    a->data_u32[i] = ~0;
}

always_inline uword
ip6_destination_matches_route (ip6_main_t * im,
			       ip6_address_t * key,
			       ip6_address_t * dest,
			       uword dest_length)
{
  int i;
  for (i = 0; i < ARRAY_LEN (key->data_u32); i++)
    {
      if ((key->data_u32[i] ^ dest->data_u32[i]) & im->fib_masks[dest_length].data_u32[i])
	return 0;
    }
  return 1;
}

/* As above but allows for unaligned destinations (e.g. works right from IP header of packet). */
always_inline uword
ip6_unaligned_destination_matches_route (ip6_main_t * im,
					 ip6_address_t * key,
					 ip6_address_t * dest,
					 uword dest_length)
{
  int i;
  for (i = 0; i < ARRAY_LEN (key->data_u32); i++)
    {
      if ((clib_mem_unaligned (&key->data_u32[i], u32) ^ dest->data_u32[i]) & im->fib_masks[dest_length].data_u32[i])
	return 0;
    }
  return 1;
}

void
ip6_set_interface_address (vlib_main_t * vm, u32 sw_if_index,
			   ip6_address_t * to_set, u32 to_set_length);

int ip6_address_compare (ip6_address_t * a1, ip6_address_t * a2);

/* Add/del a route to the FIB. */

#define IP6_ROUTE_FLAG_ADD (0 << 0)
#define IP6_ROUTE_FLAG_DEL (1 << 0)
#define IP6_ROUTE_FLAG_TABLE_ID  (0 << 1)
#define IP6_ROUTE_FLAG_FIB_INDEX (1 << 1)
#define IP6_ROUTE_FLAG_NO_REDISTRIBUTE (1 << 2)
#define IP6_ROUTE_FLAG_KEEP_OLD_ADJACENCY (1 << 3)

typedef struct {
  /* IP6_ROUTE_FLAG_* */
  u32 flags;

  /* Either index of fib or table_id to hash and get fib.
     IP6_ROUTE_FLAG_FIB_INDEX specifies index; otherwise table_id is assumed. */
  u32 table_index_or_table_id;

  /* Destination address (prefix) and length. */
  ip6_address_t dst_address;
  u32 dst_address_length;

  /* Adjacency to use for this destination. */
  u32 adj_index;

  /* If specified adjacencies to add and then
     use for this destination.  add_adj/n_add_adj
     are override adj_index if specified. */
  ip_adjacency_t * add_adj;
  u32 n_add_adj;
} ip6_add_del_route_args_t;

void ip6_add_del_route (ip6_main_t * im, ip6_add_del_route_args_t * args);

void ip6_add_del_route_next_hop (ip6_main_t * im,
				 u32 flags,
				 ip6_address_t * dst_address,
				 u32 dst_address_length,
				 ip6_address_t * next_hop,
				 u32 next_hop_sw_if_index,
				 u32 next_hop_weight);

void *
ip6_get_route (ip6_main_t * im,
	       u32 fib_index_or_table_id,
	       u32 flags,
	       u8 * address,
	       u32 address_length);

void
ip6_foreach_matching_route (ip6_main_t * im,
			    u32 table_index_or_table_id,
			    u32 flags,
			    u8 * address,
			    u32 address_length,
			    ip6_address_t ** results,
			    u8 ** result_length);

void ip6_delete_matching_routes (ip6_main_t * im,
				 u32 table_index_or_table_id,
				 u32 flags,
				 u8 * address,
				 u32 address_length);

void ip6_maybe_remap_adjacencies (ip6_main_t * im,
				  u32 table_index_or_table_id,
				  u32 flags);

void ip6_adjacency_set_interface_route (vlib_main_t * vm, ip_adjacency_t * adj, u32 sw_if_index);

uword
ip6_tcp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index);
uword
ip6_udp_register_listener (vlib_main_t * vm,
			   u16 dst_port,
			   u32 next_node_index);

serialize_function_t serialize_vnet_ip6_main, unserialize_vnet_ip6_main;

#endif /* included_ip_ip6_h */
