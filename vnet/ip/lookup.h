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

#include <vnet/vnet/rewrite.h>

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

/* IP adjacency. */
typedef struct {
  /* Handle for this adjacency in adjacency heap. */
  u32 heap_handle;

  /* Number of adjecencies in block.  Greater than 1 means multipath;
     otherwise equal to 1. */
  u16 n_adj;

  /* Next hop after ip4-lookup. */
  u16 lookup_next_index;

  union {
    /* IP_LOOKUP_NEXT_REWRITE adjacencies. */
    vnet_declare_rewrite (64 - 2*sizeof(u32));

    /* IP_LOOKUP_NEXT_LOCAL */
    u32 local_index;

  };
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

/* Stored in 32 byte VLIB buffer opaque by ip lookup for benefit of
   next nodes. */
typedef struct {
  /* Adjacency from destination/source IP address lookup. */
  u32 dst_adj_index;

  /* This gets set to ~0 until source lookup is performed. */
  u32 src_adj_index;

  /* Flow hash value for this packet computed from IP src/dst address
     protocol and ports. */
  u32 flow_hash;
} ip_buffer_opaque_t;

typedef struct {
  /* Must be first. */
  ip_buffer_opaque_t non_local;
} ip_local_buffer_opaque_t;

typedef enum {
  IP_LOCAL_NEXT_DROP,
  IP_LOCAL_NEXT_PUNT,
  IP_LOCAL_NEXT_TCP_LOOKUP,
  IP_LOCAL_NEXT_UDP_LOOKUP,
  IP_LOCAL_N_NEXT,
} ip_local_next_t;

typedef struct {
  /* Adjacency heap. */
  ip_adjacency_t * adjacency_heap;

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

  /* Number of bytes in a fib result.  Must be at least
     sizeof (uword).  First word is always adjacency index. */
  u32 fib_result_n_bytes, fib_result_n_words;

  format_function_t * format_fib_result;

  /* Adjacency packet/byte counters indexed by adjacency index. */
  vlib_combined_counter_main_t adjacency_counters;

  /* Table mapping ip protocol to ip[46]-local node next index. */
  u8 local_next_by_ip_protocol[256];
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

void ip_lookup_init (ip_lookup_main_t * lm, u32 ip_lookup_node_index);

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

extern vlib_cli_command_t vlib_cli_ip_command, vlib_cli_show_ip_command;

serialize_function_t serialize_ip_lookup_main, unserialize_ip_lookup_main;

#endif /* included_ip_lookup_h */
