/*
 * ip/ip_lookup.c: ip4/6 adjacency and lookup table managment
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

#include <clib/math.h>		/* for fabs */
#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ppp/ppp.h>

always_inline void
ip_poison_adjacencies (ip_adjacency_t * adj, uword n_adj)
{
  if (DEBUG > 0)
    memset (adj, 0xfe, n_adj * sizeof (adj[0]));
}

/* Create new block of given number of contiguous adjacencies. */
ip_adjacency_t *
ip_add_adjacency (ip_lookup_main_t * lm,
		  ip_adjacency_t * copy_adj,
		  u32 n_adj,
		  u32 * adj_index_return)
{
  ip_adjacency_t * adj;
  u32 ai, i, handle;

  ai = heap_alloc (lm->adjacency_heap, n_adj, handle);
  adj = heap_elt_at_index (lm->adjacency_heap, ai);

  ip_poison_adjacencies (adj, n_adj);

  for (i = 0; i < n_adj; i++)
    {
      if (copy_adj)
	adj[i] = copy_adj[i];

      adj[i].heap_handle = handle;
      adj[i].n_adj = n_adj;

      /* Validate adjacency counters. */
      vlib_validate_counter (&lm->adjacency_counters, ai + i);

      /* Zero possibly stale counters for re-used adjacencies. */
      vlib_zero_combined_counter (&lm->adjacency_counters, ai + i);
    }

  *adj_index_return = ai;
  return adj;
}

void ip_del_adjacency (ip_lookup_main_t * lm, u32 adj_index)
{
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);
  uword handle = adj->heap_handle;

  ip_poison_adjacencies (adj, adj->n_adj);

  heap_dealloc (lm->adjacency_heap, handle);
}

static int
next_hop_sort_by_weight (ip_multipath_next_hop_t * n1,
			 ip_multipath_next_hop_t * n2)
{
  int cmp = (int) n1->weight - (int) n2->weight;
  return (cmp == 0
	  ? (int) n1->next_hop_adj_index - (int) n2->next_hop_adj_index
	  : (cmp > 0 ? +1 : -1));
}

/* Given next hop vector is over-written with normalized one with sorted weights and
   with weights corresponding to the number of adjacencies for each next hop.
   Returns number of adjacencies in block. */
static u32 ip_multipath_normalize_next_hops (ip_lookup_main_t * lm,
					     ip_multipath_next_hop_t * raw_next_hops,
					     ip_multipath_next_hop_t ** normalized_next_hops)
{
  ip_multipath_next_hop_t * nhs;
  uword n_nhs, n_adj, n_adj_left, i;
  f64 sum_weight, norm, error;

  n_nhs = vec_len (raw_next_hops);
  ASSERT (n_nhs > 0);
  if (n_nhs == 0)
    return 0;

  /* Allocate enough space for 2 copies; we'll use second copy to save original weights. */
  nhs = *normalized_next_hops;
  vec_validate (nhs, 2*n_nhs - 1);

  /* Fast path: 1 next hop in block. */
  n_adj = n_nhs;
  if (n_nhs == 1)
    {
      nhs[0] = raw_next_hops[0];
      nhs[0].weight = 1;
      goto done;
    }

  else if (n_nhs == 2)
    {
      int cmp = next_hop_sort_by_weight (&raw_next_hops[0], &raw_next_hops[1]) < 0;

      /* Fast sort. */
      nhs[0] = raw_next_hops[cmp];
      nhs[1] = raw_next_hops[cmp ^ 1];

      /* Fast path: equal cost multipath with 2 next hops. */
      if (nhs[0].weight == nhs[1].weight)
	goto done;
    }
  else
    {
      memcpy (nhs, raw_next_hops, n_nhs * sizeof (raw_next_hops[0]));
      qsort (nhs, n_nhs, sizeof (nhs[0]), (void *) next_hop_sort_by_weight);
    }

  /* Find total weight to normalize weights. */
  sum_weight = 0;
  for (i = 0; i < n_nhs; i++)
    sum_weight += nhs[i].weight;

  /* In the unlikely case that all weights are given as 0, set them all to 1. */
  if (sum_weight == 0)
    {
      for (i = 0; i < n_nhs; i++)
	nhs[i].weight = 1;
      sum_weight = n_nhs;
    }

  /* Save copies of all next hop weights to avoid being overwritten in loop below. */
  for (i = 0; i < n_nhs; i++)
    nhs[n_nhs + i].weight = nhs[i].weight;

  /* Try larger and larger power of 2 sized adjacency blocks until we
     find one where traffic flows to within 1% of specified weights. */
  for (n_adj = max_pow2 (n_nhs); ; n_adj *= 2)
    {
      error = 0;

      norm = n_adj / sum_weight;
      n_adj_left = n_adj;
      for (i = 0; i < n_nhs && n_adj_left > 0; i++)
	{
	  f64 nf = nhs[n_nhs + i].weight * norm; /* use saved weights */
	  word n = flt_round_nearest (nf);

	  n = n > n_adj_left ? n_adj_left : n;
	  n_adj_left -= n;
	  error += fabs (nf - n);
	  nhs[i].weight = n;
	}
	
      /* Less than 1% error with this size adjacency block? */
      if (error <= 1e-2)
	{
	  /* Truncate any next hops with zero weight. */
	  _vec_len (nhs) = i;
	  break;
	}
    }

 done:
  /* Save vector for next call. */
  *normalized_next_hops = nhs;
  return n_adj;
}

ip_multipath_adjacency_t *
ip_multipath_adjacency_get (ip_lookup_main_t * lm,
			    ip_multipath_next_hop_t * raw_next_hops,
			    uword create_if_non_existent)
{
  uword * p;
  u32 i, j, n_adj, adj_index, adj_heap_handle;
  ip_adjacency_t * adj, * copy_adj;
  ip_multipath_next_hop_t * nh, * nhs;
  ip_multipath_adjacency_t * madj;

  n_adj = ip_multipath_normalize_next_hops (lm, raw_next_hops, &lm->next_hop_hash_lookup_key_normalized);
  nhs = lm->next_hop_hash_lookup_key_normalized;

  /* Basic sanity. */
  ASSERT (n_adj >= vec_len (raw_next_hops));

  /* Use normalized next hops to see if we've seen a block equivalent to this one before. */
  p = hash_get_mem (lm->multipath_adjacency_by_next_hops, nhs);
  if (p)
    return vec_elt_at_index (lm->multipath_adjacencies, p[0]);

  if (! create_if_non_existent)
    return 0;

  adj = ip_add_adjacency (lm, /* copy_adj */ 0, n_adj, &adj_index);
  adj_heap_handle = adj[0].heap_handle;
  
  /* Fill in adjacencies in block based on corresponding next hop adjacencies. */
  i = 0;
  vec_foreach (nh, nhs)
    {
      copy_adj = ip_get_adjacency (lm, nh->next_hop_adj_index);
      for (j = 0; j < nh->weight; j++)
	{
	  adj[i] = copy_adj[0];
	  adj[i].heap_handle = adj_heap_handle;
	  adj[i].n_adj = n_adj;
	  i++;
	}
    }

  /* All adjacencies should have been initialized. */
  ASSERT (i == n_adj);

  vec_validate (lm->multipath_adjacencies, adj_heap_handle);
  madj = vec_elt_at_index (lm->multipath_adjacencies, adj_heap_handle);
  
  madj->adj_index = adj_index;
  madj->n_adj_in_block = n_adj;
  madj->reference_count = 0;	/* caller will set to one. */

  madj->normalized_next_hops.count = vec_len (nhs);
  madj->normalized_next_hops.heap_offset
    = heap_alloc (lm->next_hop_heap, vec_len (nhs),
		  madj->normalized_next_hops.heap_handle);
  memcpy (lm->next_hop_heap + madj->normalized_next_hops.heap_offset,
	  nhs, vec_bytes (nhs));

  madj->unnormalized_next_hops.count = vec_len (raw_next_hops);
  madj->unnormalized_next_hops.heap_offset
    = heap_alloc (lm->next_hop_heap, vec_len (raw_next_hops),
		  madj->unnormalized_next_hops.heap_handle);
  memcpy (lm->next_hop_heap + madj->unnormalized_next_hops.heap_offset,
	  raw_next_hops, vec_bytes (raw_next_hops));

  return madj;
}

void
ip_multipath_adjacency_free (ip_lookup_main_t * lm,
			     ip_multipath_adjacency_t * a)
{
  ASSERT (0);
}

always_inline ip_multipath_next_hop_t *
ip_next_hop_hash_key_get_next_hops (ip_lookup_main_t * lm, uword k,
				    uword * n_next_hops)
{
  ip_multipath_next_hop_t * nhs;
  uword n_nhs;
  if (k & 1)
    {
      uword handle = k / 2;
      nhs = heap_elt_with_handle (lm->next_hop_heap, handle);
      n_nhs = heap_len (lm->next_hop_heap, handle);
    }
  else
    {
      nhs = uword_to_pointer (k, ip_multipath_next_hop_t *);
      n_nhs = vec_len (nhs);
    }
  *n_next_hops = n_nhs;
  return nhs;
}

static uword
ip_next_hop_hash_key_sum (hash_t * h, uword key0)
{
  ip_lookup_main_t * lm = uword_to_pointer (h->user, ip_lookup_main_t *);  
  ip_multipath_next_hop_t * k0;
  uword n0;

  k0 = ip_next_hop_hash_key_get_next_hops (lm, key0, &n0);
  return hash_memory (k0, n0 * sizeof (k0[0]), /* seed */ n0);
}

static uword
ip_next_hop_hash_key_equal (hash_t * h, uword key0, uword key1)
{
  ip_lookup_main_t * lm = uword_to_pointer (h->user, ip_lookup_main_t *);  
  ip_multipath_next_hop_t * k0, * k1;
  uword n0, n1;

  k0 = ip_next_hop_hash_key_get_next_hops (lm, key0, &n0);
  k1 = ip_next_hop_hash_key_get_next_hops (lm, key1, &n1);

  return n0 == n1 && ! memcmp (k0, k1, n0 * sizeof (k0[0]));
}

void ip_lookup_init (ip_lookup_main_t * lm, u32 ip_lookup_node_index)
{
  u32 ai;
  ip_adjacency_t * adj;

  /* Hand-craft special miss adjacency to use when nothing matches in the
     routing table. */
  adj = ip_add_adjacency (lm, /* template */ 0, /* n-adj */ 1, &ai);

  adj->lookup_next_index = IP_LOOKUP_NEXT_MISS;

  lm->miss_adj_index = ai;

  if (! lm->fib_result_n_bytes)
    lm->fib_result_n_bytes = sizeof (uword);

  lm->multipath_adjacency_by_next_hops
    = hash_create2 (/* elts */ 0,
		    /* user */ pointer_to_uword (lm),
		    /* value_bytes */ sizeof (uword),
		    ip_next_hop_hash_key_sum,
		    ip_next_hop_hash_key_equal,
		    /* format pair/arg */
		    0, 0);
}

u8 * format_ip_lookup_next (u8 * s, va_list * args)
{
  ip_lookup_next_t n = va_arg (*args, ip_lookup_next_t);
  char * t = 0;

  switch (n)
    {
    default:
      s = format (s, "unknown %d", n);
      return s;

    case IP_LOOKUP_NEXT_MISS: t = "miss"; break;
    case IP_LOOKUP_NEXT_DROP: t = "drop"; break;
    case IP_LOOKUP_NEXT_PUNT: t = "punt"; break;
    case IP_LOOKUP_NEXT_LOCAL: t = "local"; break;
    case IP_LOOKUP_NEXT_ARP: t = "arp"; break;

    case IP_LOOKUP_NEXT_REWRITE:
    case IP_LOOKUP_NEXT_MULTIPATH:
      break;
    }

  if (t)
    vec_add (s, t, strlen (t));

  return s;
}

u8 * format_ip_adjacency (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  ip_lookup_main_t * lm = va_arg (*args, ip_lookup_main_t *);
  u32 adj_index = va_arg (*args, u32);
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);

  s = format (s, "%d: ", adj_index);

  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_REWRITE:
    case IP_LOOKUP_NEXT_MULTIPATH:
      s = format (s, "%U",
		  format_vnet_rewrite,
		  vm, &adj->rewrite_header, sizeof (adj->rewrite_data));
      break;

    default:
      s = format (s, "%U", format_ip_lookup_next, adj->lookup_next_index);
      if (adj->lookup_next_index == IP_LOOKUP_NEXT_ARP)
	s = format (s, " %U",
		    format_vlib_sw_interface_name,
		      vm,
		      vlib_get_sw_interface (vm, adj->rewrite_header.sw_if_index));
      break;
    }

  return s;
}

u8 * format_ip_adjacency_packet_data (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  ip_lookup_main_t * lm = va_arg (*args, ip_lookup_main_t *);
  u32 adj_index = va_arg (*args, u32);
  u8 * packet_data = va_arg (*args, u8 *);
  u32 n_packet_data_bytes = va_arg (*args, u32);
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);

  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_REWRITE:
    case IP_LOOKUP_NEXT_MULTIPATH:
      s = format (s, "%U",
		  format_vnet_rewrite_header,
		  vm, &adj->rewrite_header, packet_data, n_packet_data_bytes);
      break;

    default:
      break;
    }

  return s;
}

static uword unformat_ip_lookup_next (unformat_input_t * input, va_list * args)
{
  ip_lookup_next_t * result = va_arg (*args, ip_lookup_next_t *);
  ip_lookup_next_t n;

  if (unformat (input, "drop"))
    n = IP_LOOKUP_NEXT_DROP;

  else if (unformat (input, "punt"))
    n = IP_LOOKUP_NEXT_PUNT;

  else if (unformat (input, "local"))
    n = IP_LOOKUP_NEXT_LOCAL;

  else if (unformat (input, "arp"))
    n = IP_LOOKUP_NEXT_ARP;

  else
    return 0;
    
  *result = n;
  return 1;
}

void ip_adjacency_set_arp (vlib_main_t * vm, ip_adjacency_t * adj, u32 sw_if_index)
{
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  ip_lookup_next_t n;
  u32 node_index;

  if (is_ethernet_interface (hw->hw_if_index))
    {
      n = IP_LOOKUP_NEXT_ARP;
      node_index = ip4_arp_node.index;
    }
  else
    {
      n = IP_LOOKUP_NEXT_REWRITE;
      node_index = ip4_rewrite_node.index;
    }

  adj->lookup_next_index = n;
  adj->rewrite_header.sw_if_index = sw_if_index;
  adj->rewrite_header.node_index = node_index;
  adj->rewrite_header.next_index = vlib_node_add_next (vm, node_index, hw->output_node_index);

  if (n == IP_LOOKUP_NEXT_REWRITE)
    {
      if (is_ppp_interface (vm, hw->hw_if_index))
	ppp_set_adjacency (&adj->rewrite_header, sizeof (adj->rewrite_data),
			   PPP_PROTOCOL_ip4);
      else
	ASSERT (0);
    }
}

uword unformat_ip_adjacency (unformat_input_t * input, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  ip_adjacency_t * adj = va_arg (*args, ip_adjacency_t *);
  u32 node_index = va_arg (*args, u32);
  u32 sw_if_index;
  ip_lookup_next_t next;

  adj->rewrite_header.node_index = node_index;

  if (unformat (input, "arp %U",
		unformat_vlib_sw_interface, vm, &sw_if_index))
    {
      ip_adjacency_set_arp (vm, adj, sw_if_index);
    }

  else if (unformat_user (input, unformat_ip_lookup_next, &next))
    {
      adj->lookup_next_index = next;

      adj->local_index = 0;
      if (next == IP_LOOKUP_NEXT_LOCAL)
	unformat (input, "%d", &adj->local_index);
    }

  else if (unformat_user (input,
			  unformat_vnet_rewrite,
			  vm, &adj->rewrite_header, sizeof (adj->rewrite_data)))
    adj->lookup_next_index = IP_LOOKUP_NEXT_REWRITE;

  else
    return 0;

  return 1;
}

static clib_error_t *
ip_route (vlib_main_t * vm, unformat_input_t * input, vlib_cli_command_t * cmd)
{
  ip4_main_t * im4 = &ip4_main;
  ip_lookup_main_t * lm = &im4->lookup_main;
  clib_error_t * error = 0;
  u32 address_len, is_ip4, table_id, is_del;
  ip4_address_t address;
  ip_adjacency_t * add_adj = 0;

  is_ip4 = 0;
  is_del = 0;
  table_id = 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "table %d", &table_id))
	;
      else if (unformat (input, "del"))
	is_del = 1;
      else if (unformat (input, "add"))
	is_del = 0;
      else if (unformat (input, "%U/%d",
			 unformat_ip4_address, &address,
			 &address_len))
	{
	  is_ip4 = 1;
	  break;
	}
      else
	return unformat_parse_error (input);
    }
    
  if (! is_del)
    {
      while (1)
	{
	  ip_adjacency_t parse_adj;

	  if (! unformat_user (input, unformat_ip_adjacency, vm, &parse_adj,
			       is_ip4 ? ip4_rewrite_node.index : ~0))
	    break;

	  vec_add1 (add_adj, parse_adj);
	}

      if (vec_len (add_adj) == 0)
	{
	  error = clib_error_return (0, "expected adjacencies");
	  goto done;
	}
    }

  ASSERT (is_ip4);

  if (is_del)
    {
      u32 adj_index = 0;

      if (is_ip4)
	{
	  adj_index = ip4_add_del_route (im4, table_id,
					 IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_TABLE_ID,
					 &address, address_len,
					 /* adj index */ ~0);
	  if (adj_index == ~0)
	    {
	      error = clib_error_return
		(0, "no such route %U",
		 format_ip4_address_and_length, &address, address_len);
	      goto done;
	    }
	}

      ip_del_adjacency (lm, adj_index);
    }
  else
    {
      u32 ai;
      ip_adjacency_t * adj;

      adj = ip_add_adjacency (lm, add_adj, vec_len (add_adj), &ai);

      if (is_ip4)
	ip4_add_del_route (im4, table_id,
			   IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_TABLE_ID,
			   &address, address_len,
			   ai);
    }

 done:
  vec_free (add_adj);
  return error;
}

VLIB_CLI_COMMAND (vlib_cli_ip_command) = {
  .name = "ip",
  .short_help = "Internet protocol (IP) commands",
};

VLIB_CLI_COMMAND (vlib_cli_show_ip_command) = {
  .name = "ip",
  .short_help = "Internet protocol (IP) show commands",
  .parent = &vlib_cli_show_command,
};

static VLIB_CLI_COMMAND (ip_route_command) = {
  .name = "route",
  .short_help = "Add/delete IP routes",
  .function = ip_route,
  .parent = &vlib_cli_ip_command,
};

typedef struct {
  ip4_address_t address;

  PACKED (u32 address_length : 6);

  PACKED (u32 index : 26);
} ip4_route_t;

static clib_error_t *
ip4_show_fib (vlib_main_t * vm, unformat_input_t * input, vlib_cli_command_t * cmd)
{
  ip4_main_t * im4 = &ip4_main;
  ip4_route_t * routes, * r;
  ip4_fib_t * fib;
  ip_lookup_main_t * lm = &im4->lookup_main;
  uword * results, n_words_per_result, i;

  routes = 0;
  results = 0;
  ASSERT (lm->fib_result_n_bytes % sizeof (uword) == 0);
  n_words_per_result = lm->fib_result_n_bytes / sizeof (uword);

  vec_foreach (fib, im4->fibs)
    {
      vlib_cli_output (vm, "Table %d", fib->table_id);

      if (routes)
	_vec_len (routes) = 0;
      if (results)
	_vec_len (results) = 0;
      for (i = 0; i < ARRAY_LEN (fib->adj_index_by_dst_address); i++)
	{
	  uword * hash = fib->adj_index_by_dst_address[i];
	  hash_pair_t * p;
	  hash_foreach_pair (p, hash, ({
	    ip4_route_t x;
	    x.address.data_u32 = p->key;
	    x.address_length = i;
	    if (n_words_per_result > 1)
	      {
		x.index = vec_len (results);
		vec_add (results, p->value, n_words_per_result);
	      }
	    else
	      x.index = p->value[0];

	    vec_add1 (routes, x);
	  }));
	}

      vec_sort (routes, r1, r2,
		({ int cmp = ip4_address_compare (&r1->address, &r2->address);
		  cmp ? cmp : ((int) r1->address_length - (int) r2->address_length); }));

      vlib_cli_output (vm, "%=20s%=16s%=16s%=16s",
		       "Destination", "Packets", "Bytes", "Adjacency");
      vec_foreach (r, routes)
	{
	  vlib_counter_t c;
	  uword adj_index, * result = 0;
	  ip_adjacency_t * adj;

	  adj_index = r->index;
	  if (n_words_per_result > 1)
	    {
	      result = vec_elt_at_index (results, adj_index);
	      adj_index = result[0];
	    }

	  vlib_get_combined_counter (&lm->adjacency_counters, adj_index, &c);
	  vlib_cli_output (vm, "%-20U%16Ld%16Ld %U",
			   format_ip4_address_and_length,
			   r->address.data, r->address_length,
			   c.packets, c.bytes,
			   format_ip_adjacency,
			   vm, lm, adj_index);

	  if (result && lm->format_fib_result)
	    vlib_cli_output (vm, "%20s%U", "", lm->format_fib_result, vm, lm, result, 0);

	  adj = ip_get_adjacency (lm, adj_index);
	  if (adj->n_adj > 1)
	    ASSERT (0);
	}
    }

  vec_free (routes);
  vec_free (results);

  return 0;
}

static VLIB_CLI_COMMAND (ip4_show_fib_command) = {
  .name = "fib",
  .short_help = "Show IP4 routing table",
  .function = ip4_show_fib,
  .parent = &vlib_cli_show_ip_command,
};
