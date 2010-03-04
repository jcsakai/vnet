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

  /* This packet matches a multipath adjacency (e.g. n_adj > 1).
     The (src,dst) (address,layer 4 ports) will be hashed to figure
     which adjacency of the batch to take. */
  IP_LOOKUP_NEXT_MULTIPATH,

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
  ip_lookup_next_t lookup_next_index : 16;

  union {
    /* IP_LOOKUP_NEXT_{REWRITE,MULTIPATH} adjacencies. */
    vnet_declare_rewrite (64 - 2*sizeof(u32));

    /* IP_LOOKUP_NEXT_LOCAL */
    u32 local_index;

  };
} ip_adjacency_t;

/* Index into adjacency table. */
typedef u32 ip_adjacency_index_t;

typedef struct {
  u32 buffer;
  u32 adj_index;
} ip_buffer_and_adjacency_t;

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

  /* Adjacency index for routing table misses. */
  u32 miss_adj_index;

  /* Number of bytes in a fib result.  Must be at least
     sizeof (uword).  First word is always adjacency index. */
  u32 fib_result_n_bytes;

  format_function_t * format_fib_result;

  /* Adjacency packet/byte counters indexed by adjacency index. */
  vlib_combined_counter_main_t adjacency_counters;

  /* Table mapping ip protocol to ip[46]-local node next index. */
  u8 local_next_by_ip_protocol[256];
} ip_lookup_main_t;

static always_inline ip_adjacency_t *
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

void ip_adjacency_set_arp (vlib_main_t * vm, ip_adjacency_t * adj, u32 sw_if_index);

extern vlib_cli_command_t vlib_cli_ip_command, vlib_cli_show_ip_command;

#endif /* included_ip_lookup_h */
