/*
 * ip/ip4_fib.h: ip4 mtrie fib
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

#ifndef included_ip_ip4_fib_h
#define included_ip_ip4_fib_h

#include <clib/cache.h>
#include <clib/vector.h>
#include <vnet/ip/lookup.h>
#include <vnet/ip/ip4_packet.h>	/* for ip4_address_t */

/* ip4 fib leafs: 4 ply 8-8-8-8 mtrie.
   1 + 2*adj_index for terminal leaves.
   0 + 2*next_ply_index for non-terminals.
   1 => empty (adjacency index of zero is special miss adjacency). */
typedef u32 ip4_fib_mtrie_leaf_t;

#define IP4_FIB_MTRIE_LEAF_EMPTY (1 + 2*IP_LOOKUP_MISS_ADJ_INDEX)

always_inline u32 ip4_fib_mtrie_leaf_is_empty (ip4_fib_mtrie_leaf_t n)
{ return n == IP4_FIB_MTRIE_LEAF_EMPTY; }

always_inline u32 ip4_fib_mtrie_leaf_is_non_empty (ip4_fib_mtrie_leaf_t n)
{ return n != IP4_FIB_MTRIE_LEAF_EMPTY; }

always_inline u32 ip4_fib_mtrie_leaf_is_terminal (ip4_fib_mtrie_leaf_t n)
{ return n & 1; }

always_inline u32 ip4_fib_mtrie_leaf_get_adj_index (ip4_fib_mtrie_leaf_t n)
{
  ASSERT (ip4_fib_mtrie_leaf_is_terminal (n));
  return n >> 1;
}

always_inline ip4_fib_mtrie_leaf_t ip4_fib_mtrie_leaf_set_adj_index (u32 adj_index)
{
  ip4_fib_mtrie_leaf_t l;
  l = 1 + 2*adj_index;
  ASSERT (ip4_fib_mtrie_leaf_get_adj_index (l) == adj_index);
  return l;
}

always_inline u32 ip4_fib_mtrie_leaf_is_next_ply (ip4_fib_mtrie_leaf_t n)
{ return (n & 1) == 0; }

always_inline u32 ip4_fib_mtrie_leaf_get_next_ply_index (ip4_fib_mtrie_leaf_t n)
{
  ASSERT (ip4_fib_mtrie_leaf_is_next_ply (n));
  return n >> 1;
}

always_inline ip4_fib_mtrie_leaf_t ip4_fib_mtrie_leaf_set_next_ply_index (u32 i)
{
  ip4_fib_mtrie_leaf_t l;
  l = 0 + 2*i;
  ASSERT (ip4_fib_mtrie_leaf_get_next_ply_index (l) == i);
  return l;
}

/* One ply of the 4 ply mtrie fib. */
typedef struct {
  union {
    ip4_fib_mtrie_leaf_t leaves[256];

#ifdef CLIB_HAVE_VEC128
    u32x4 leaves_as_u32x4[256 / 4];
#endif
  };

  /* Prefix length for terminal leaves. */
  u8 dst_address_bits_of_leaves[256];

  /* Number of non-empty leafs (whether terminal or not). */
  i32 n_non_empty_leafs;

  /* Pad to cache line boundary. */
  u8 pad[CLIB_CACHE_LINE_BYTES
	 - 1 * sizeof (i32)];
} ip4_fib_mtrie_ply_t;

typedef struct {
  /* Pool of plies.  Index zero is root ply. */
  ip4_fib_mtrie_ply_t * ply_pool;

  /* Special case leaf for default route 0.0.0.0/0. */
  ip4_fib_mtrie_leaf_t default_leaf;
} ip4_fib_mtrie_t;

void ip4_fib_mtrie_init (ip4_fib_mtrie_t * m);

struct ip4_fib_t;

void ip4_fib_mtrie_add_del_route (struct ip4_fib_t * f,
				  ip4_address_t dst_address,
				  u32 dst_address_length,
				  u32 adj_index,
				  u32 is_del);

/* Returns adjacency index. */
u32 ip4_mtrie_lookup_address (ip4_fib_mtrie_t * m, ip4_address_t dst);

format_function_t format_ip4_fib_mtrie;

#endif /* included_ip_ip4_fib_h */