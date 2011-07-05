/*
 * ip/ip_lookup.h: ip (4 or 6) lookup structures, adjacencies, ...
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

#ifndef included_ip_lookup_h
#define included_ip_lookup_h

#include <vnet/vnet/config.h>
#include <vnet/vnet/rewrite.h>
#include <vnet/vnet/vnet.h>

/* Next index stored in adjacency. */
typedef enum {
  /* Packet does not match any route in table. */
  IP_LOOKUP_NEXT_MISS,

  /* Adjacency says to drop or punt this packet. */
  IP_LOOKUP_NEXT_DROP,
  IP_LOOKUP_NEXT_PUNT,

  /* This packet is for one of our own IP addresses. */
  IP_LOOKUP_NEXT_LOCAL,

  /* This packet matches an "interface route" and packets
     need to be passed to ARP to find rewrite string for
     this destination. */
  IP_LOOKUP_NEXT_ARP,

  /* This packet is to be rewritten and forwarded to the next
     processing node.  This is typically the output interface but
     might be another node for further output processing. */
  IP_LOOKUP_NEXT_REWRITE,

  IP_LOOKUP_N_NEXT,
} ip_lookup_next_t;

/* IP unicast adjacency. */
typedef struct {
  /* Handle for this adjacency in adjacency heap. */
  u32 heap_handle;

  /* Interface address index for this local/arp adjacency. */
  u32 if_address_index;

  /* Number of adjecencies in block.  Greater than 1 means multipath;
     otherwise equal to 1. */
  u16 n_adj;

  /* Next hop after ip4-lookup. */
  union {
    ip_lookup_next_t lookup_next_index : 16;
    u16 lookup_next_index_as_int;
  };

  vnet_declare_rewrite (128 - 3*sizeof(u32));
} ip_adjacency_t;

/* Index into adjacency table. */
typedef u32 ip_adjacency_index_t;

typedef struct {
  /* Directly connected next-hop adjacency index. */
  u32 next_hop_adj_index;

  /* Path weight for this adjacency. */
  u32 weight;
} ip_multipath_next_hop_t;

typedef struct {
  /* Adjacency index of first index in block. */
  u32 adj_index;
  
  /* Power of 2 size of adjacency block. */
  u32 n_adj_in_block;

  /* Number of prefixes that point to this adjacency. */
  u32 reference_count;

  /* Normalized next hops are used as hash keys: they are sorted by weight
     and weights are chosen so they add up to 1 << log2_n_adj_in_block (with
     zero-weighted next hops being deleted).
     Unnormalized next hops are saved so that control plane has a record of exactly
     what the RIB told it. */
  struct {
    /* Number of hops in the multipath. */
    u32 count;

    /* Offset into next hop heap for this block. */
    u32 heap_offset;

    /* Heap handle used to for example free block when we're done with it. */
    u32 heap_handle;
  } normalized_next_hops, unnormalized_next_hops;
} ip_multipath_adjacency_t;

/* IP multicast adjacency. */
typedef struct {
  /* Handle for this adjacency in adjacency heap. */
  u32 heap_handle;

  /* Number of adjecencies in block. */
  u32 n_adj;

  /* Rewrite string. */
  vnet_declare_rewrite (64 - 2*sizeof(u32));
} ip_multicast_rewrite_t;

typedef struct {
  /* ip4-multicast-rewrite next index. */
  u32 next_index;

  u8 n_rewrite_bytes;

  u8 rewrite_string[64 - 1*sizeof(u32) - 1*sizeof(u8)];
} ip_multicast_rewrite_string_t;

typedef struct {
  ip_multicast_rewrite_t * rewrite_heap;

  ip_multicast_rewrite_string_t * rewrite_strings;

  /* Negative rewrite string index; >= 0 sw_if_index.
     Sorted.  Used to hash. */
  i32 ** adjacency_id_vector;

  uword * adjacency_by_id_vector;
} ip_multicast_lookup_main_t;

typedef struct {
  /* Key for mhash; in fact, just a byte offset into mhash key vector. */
  u32 address_key;

  /* Interface which has this address. */
  u32 sw_if_index;

  /* Adjacency for neighbor probe (ARP) for this interface address. */
  u32 neighbor_probe_adj_index;

  /* Address (prefix) length for this interface. */
  u16 address_length;

  /* Will be used for something eventually.  Primary vs. secondary? */
  u16 flags;

  /* Next and previous pointers for doubly linked list of
     addresses per software interface. */
  u32 next_this_sw_interface;
  u32 prev_this_sw_interface;
} ip_interface_address_t;

/* Stored in 32 byte VLIB buffer opaque by ip lookup for benefit of
   next nodes. */
typedef struct {
  /* Adjacency from destination IP address lookup. */
  u32 dst_adj_index;

  /* Adjacency from source IP address lookup.
     This gets set to ~0 until source lookup is performed. */
  u32 src_adj_index;

  /* Flow hash value for this packet computed from IP src/dst address
     protocol and ports. */
  u32 flow_hash;

  /* Current configuration index. */
  u32 current_config_index;
} ip_buffer_opaque_t;

typedef enum {
  IP_LOCAL_NEXT_DROP,
  IP_LOCAL_NEXT_PUNT,
  IP_LOCAL_NEXT_TCP_LOOKUP,
  IP_LOCAL_NEXT_UDP_LOOKUP,
  IP_LOCAL_NEXT_ICMP,
  IP_LOCAL_N_NEXT,
} ip_local_next_t;

struct ip_lookup_main_t;

typedef void (* ip_add_del_adjacency_callback_t) (struct ip_lookup_main_t * lm,
						  u32 adj_index,
						  ip_adjacency_t * adj,
						  u32 is_del);

typedef struct {
  vnet_config_main_t config_main;

  u32 * config_index_by_sw_if_index;
} ip_config_main_t;

typedef struct ip_lookup_main_t {
  /* Adjacency heap. */
  ip_adjacency_t * adjacency_heap;

  /* Adjacency packet/byte counters indexed by adjacency index. */
  vlib_combined_counter_main_t adjacency_counters;

  /* Heap of (next hop, weight) blocks.  Sorted by next hop. */
  ip_multipath_next_hop_t * next_hop_heap;

  /* Indexed by heap_handle from ip_adjacency_t. */
  ip_multipath_adjacency_t * multipath_adjacencies;

  /* Temporary vectors for looking up next hops in hash. */
  ip_multipath_next_hop_t * next_hop_hash_lookup_key;
  ip_multipath_next_hop_t * next_hop_hash_lookup_key_normalized;

  /* Hash table mapping normalized next hops and weights
     to multipath adjacency index. */
  uword * multipath_adjacency_by_next_hops;

  u32 * adjacency_remap_table;
  u32 n_adjacency_remaps;

  /* If average error per adjacency is less than this threshold adjacency block
     size is accepted. */
  f64 multipath_next_hop_error_tolerance;

  /* Adjacency index for routing table misses and drops. */
  u32 miss_adj_index, drop_adj_index;

  ip_add_del_adjacency_callback_t * add_del_adjacency_callbacks;

  /* Pool of addresses that are assigned to interfaces. */
  ip_interface_address_t * if_address_pool;

  /* Hash table mapping address to index in interface address pool. */
  mhash_t address_to_if_address_index;

  /* Head of doubly linked list of interface addresses for each software interface.
     ~0 means this interface has no address. */
  u32 * if_address_pool_index_by_sw_if_index;

  /* rx/tx interface/feature configuration. */
  ip_config_main_t rx_config_mains[VNET_N_CAST], tx_config_main;

  /* Number of bytes in a fib result.  Must be at least
     sizeof (uword).  First word is always adjacency index. */
  u32 fib_result_n_bytes, fib_result_n_words;

  format_function_t * format_fib_result;

  /* 1 for ip6; 0 for ip4. */
  u32 is_ip6;

  /* Either format_ip4_address_and_length or format_ip6_address_and_length. */
  format_function_t * format_address_and_length;

  /* Table mapping ip protocol to ip[46]-local node next index. */
  u8 local_next_by_ip_protocol[256];

  /* IP_BUILTIN_PROTOCOL_{TCP,UDP,ICMP,OTHER} by protocol in IP header. */
  u8 builtin_protocol_by_ip_protocol[256];
} ip_lookup_main_t;

always_inline ip_adjacency_t *
ip_get_adjacency (ip_lookup_main_t * lm,
		  u32 adj_index)
{
  ip_adjacency_t * adj;

  adj = heap_elt_at_index (lm->adjacency_heap, adj_index);

  ASSERT (! heap_is_free_handle (lm->adjacency_heap, adj->heap_handle));

  return adj;
}

#define ip_prefetch_adjacency(lm,adj_index,type)		\
do {								\
  ip_adjacency_t * _adj = (lm)->adjacency_heap + (adj_index);	\
  CLIB_PREFETCH (_adj, sizeof (_adj[0]), type);			\
} while (0)

always_inline void
ip_call_add_del_adjacency_callbacks (ip_lookup_main_t * lm, u32 adj_index, u32 is_del)
{
  ip_adjacency_t * adj;
  uword i;
  adj = ip_get_adjacency (lm, adj_index);
  for (i = 0; i < vec_len (lm->add_del_adjacency_callbacks); i++)
    lm->add_del_adjacency_callbacks[i] (lm, adj_index, adj, is_del);
}

/* Create new block of given number of contiguous adjacencies. */
ip_adjacency_t *
ip_add_adjacency (ip_lookup_main_t * lm,
		  ip_adjacency_t * adj,
		  u32 n_adj,
		  u32 * adj_index_result);

void ip_del_adjacency (ip_lookup_main_t * lm, u32 adj_index);

void
ip_multipath_adjacency_free (ip_lookup_main_t * lm,
			     ip_multipath_adjacency_t * a);

u32
ip_multipath_adjacency_add_del_next_hop (ip_lookup_main_t * lm,
					 u32 is_del,
					 u32 old_mp_adj_index,
					 u32 next_hop_adj_index,
					 u32 next_hop_weight,
					 u32 * new_mp_adj_index);

clib_error_t *
ip_interface_address_add_del (ip_lookup_main_t * lm,
			      u32 sw_if_index,
			      void * address,
			      u32 address_length,
			      u32 is_del,
			      u32 * result_index);

always_inline ip_interface_address_t *
ip_get_interface_address (ip_lookup_main_t * lm, void * address)
{
  uword * p = mhash_get (&lm->address_to_if_address_index, address);
  return p ? pool_elt_at_index (lm->if_address_pool, p[0]) : 0;
}

always_inline void *
ip_interface_address_get_address (ip_lookup_main_t * lm, ip_interface_address_t * a)
{ return mhash_key_to_mem (&lm->address_to_if_address_index, a->address_key); }

always_inline ip_interface_address_t *
ip_interface_address_for_packet (ip_lookup_main_t * lm, vlib_buffer_t * b, u32 sw_if_index)
{
  ip_buffer_opaque_t * o;
  ip_adjacency_t * adj;
  u32 if_address_index;

  o = vlib_get_buffer_opaque (b);
  adj = ip_get_adjacency (lm, o->dst_adj_index);

  ASSERT (adj->lookup_next_index == IP_LOOKUP_NEXT_ARP
	  || adj->lookup_next_index == IP_LOOKUP_NEXT_LOCAL);
  if_address_index = adj->if_address_index;
  if_address_index = (if_address_index == ~0 ?
		      vec_elt (lm->if_address_pool_index_by_sw_if_index, sw_if_index)
		      : if_address_index);

  return pool_elt_at_index (lm->if_address_pool, if_address_index);
}

#define foreach_ip_interface_address(lm,a,sw_if_index,body)		\
do {									\
  u32 _ia = vec_elt ((lm)->if_address_pool_index_by_sw_if_index,	\
		     (sw_if_index));					\
  ip_interface_address_t * _a;						\
  while (_ia != ~0)							\
    {									\
      _a = pool_elt_at_index ((lm)->if_address_pool, _ia);		\
      _ia = _a->next_this_sw_interface;					\
      (a) = _a;								\
      body;								\
    }									\
} while (0)

void ip_lookup_init (ip_lookup_main_t * lm, u32 ip_lookup_node_index);

serialize_function_t serialize_ip_lookup_main, unserialize_ip_lookup_main;
serialize_function_t serialize_vec_ip_adjacency, unserialize_vec_ip_adjacency;

#endif /* included_ip_lookup_h */
