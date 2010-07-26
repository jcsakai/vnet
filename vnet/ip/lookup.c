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

static void
ip_multipath_del_adjacency (ip_lookup_main_t * lm, u32 del_adj_index);

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

static void ip_del_adjacency2 (ip_lookup_main_t * lm, u32 adj_index, u32 delete_multipath_adjacency)
{
  ip_adjacency_t * adj = ip_get_adjacency (lm, adj_index);
  uword handle = adj->heap_handle;

  if (delete_multipath_adjacency)
    ip_multipath_del_adjacency (lm, adj_index);

  ip_poison_adjacencies (adj, adj->n_adj);

  heap_dealloc (lm->adjacency_heap, handle);
}

void ip_del_adjacency (ip_lookup_main_t * lm, u32 adj_index)
{ ip_del_adjacency2 (lm, adj_index, /* delete_multipath_adjacency */ 1); }

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
      _vec_len (nhs) = 1;
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
	{
	  nhs[0].weight = nhs[1].weight = 1;
	  _vec_len (nhs) = 2;
	  goto done;
	}
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
      for (i = 0; i < n_nhs; i++)
	{
	  f64 nf = nhs[n_nhs + i].weight * norm; /* use saved weights */
	  word n = flt_round_nearest (nf);

	  n = n > n_adj_left ? n_adj_left : n;
	  n_adj_left -= n;
	  error += fabs (nf - n);
	  nhs[i].weight = n;
	}
	
      nhs[0].weight += n_adj_left;

      /* Less than 5% average error per adjacency with this size adjacency block? */
      if (error <= lm->multipath_next_hop_error_tolerance*n_adj)
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

static u32
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
    return p[0];

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

  return adj_heap_handle;
}

/* Returns 0 for next hop not found. */
u32
ip_multipath_adjacency_add_del_next_hop (ip_lookup_main_t * lm,
					 u32 is_del,
					 u32 old_mp_adj_index,
					 u32 next_hop_adj_index,
					 u32 next_hop_weight,
					 u32 * new_mp_adj_index)
{
  ip_multipath_adjacency_t * mp_old, * mp_new;
  ip_multipath_next_hop_t * nh, * nhs, * hash_nhs;
  u32 n_nhs, i_nh;

  mp_old = 0;
  n_nhs = 0;
  if (old_mp_adj_index != ~0)
    {
      mp_old = vec_elt_at_index (lm->multipath_adjacencies, old_mp_adj_index);
	
      nhs = vec_elt_at_index (lm->next_hop_heap, mp_old->unnormalized_next_hops.heap_offset);
      n_nhs = mp_old->unnormalized_next_hops.count;

      /* Linear search: ok since n_next_hops is small. */
      for (i_nh = 0; i_nh < n_nhs; i_nh++)
	if (nhs[i_nh].next_hop_adj_index == next_hop_adj_index)
	  break;

      /* Given next hop not found. */
      if (i_nh >= n_nhs && is_del)
	return 0;
    }

  hash_nhs = lm->next_hop_hash_lookup_key;
  if (hash_nhs)
    _vec_len (hash_nhs) = 0;

  if (is_del)
    {
      if (n_nhs > 1)
	{
	  /* Prepare lookup key for multipath with target next hop deleted. */
	  if (i_nh > 0)
	    vec_add (hash_nhs, nhs + 0, i_nh);
	  if (i_nh + 1 < n_nhs)
	    vec_add (hash_nhs, nhs + i_nh + 1, n_nhs - (i_nh + 1));
	}
    }
  else /* it's an add. */
    {
      /* If next hop is already there with the same weight, we have nothing to do. */
      if (i_nh < n_nhs && nhs[i_nh].weight == next_hop_weight)
	goto done;

      /* Copy old next hops to lookup key vector. */
      if (n_nhs > 0)
	vec_add (hash_nhs, nhs, n_nhs);

      if (i_nh < n_nhs)
	{
	  /* Change weight of existing next hop. */
	  nh = vec_elt_at_index (hash_nhs, i_nh);
	}
      else
	{
	  /* Add a new next hop. */
	  vec_add2 (hash_nhs, nh, 1);
	  nh->next_hop_adj_index = next_hop_adj_index;
	}

      /* Set weight for added or old next hop. */
      nh->weight = next_hop_weight;
    }

  if (vec_len (hash_nhs) > 0)
    {
      u32 tmp = ip_multipath_adjacency_get (lm, hash_nhs,
					    /* create_if_non_existent */ 1);
      if (tmp != ~0)
	mp_new = vec_elt_at_index (lm->multipath_adjacencies, tmp);

      /* Fetch again since pool may have moved. */
      if (mp_old)
	mp_old = vec_elt_at_index (lm->multipath_adjacencies, old_mp_adj_index);
    }

  new_mp_adj_index[0] = mp_new ? mp_new - lm->multipath_adjacencies : ~0;

  if (mp_new != mp_old)
    {
      if (mp_old)
	{
	  ASSERT (mp_old->reference_count > 0);
	  mp_old->reference_count -= 1;
	}
      if (mp_new)
	mp_new->reference_count += 1;
    }

  if (mp_old && mp_old->reference_count == 0)
    ip_multipath_adjacency_free (lm, mp_old);

 done:
  /* Save key vector next call. */
  lm->next_hop_hash_lookup_key = hash_nhs;

  return 1;
}

static void
ip_multipath_del_adjacency (ip_lookup_main_t * lm, u32 del_adj_index)
{
  ip_adjacency_t * adj = ip_get_adjacency (lm, del_adj_index);
  ip_multipath_adjacency_t * madj, * new_madj;
  ip_multipath_next_hop_t * nhs, * hash_nhs;
  u32 i, n_nhs, madj_index, new_madj_index;

  /* It is illegal to directly delete a multipath adjacency. */
  if (DEBUG > 0)
    {
      madj = vec_elt_at_index (lm->multipath_adjacencies, adj->heap_handle);
      ASSERT (madj->reference_count == 0);
    }

  vec_validate (lm->adjacency_remap_table, vec_len (lm->adjacency_heap) - 1);

  for (madj_index = 0; madj_index < vec_len (lm->multipath_adjacencies); madj_index++)
    {
      madj = vec_elt_at_index (lm->multipath_adjacencies, madj_index);
      if (madj->n_adj_in_block == 0)
	continue;

      nhs = heap_elt_at_index (lm->next_hop_heap, madj->unnormalized_next_hops.heap_offset);
      n_nhs = madj->unnormalized_next_hops.count;
      for (i = 0; i < n_nhs; i++)
	if (nhs[i].next_hop_adj_index == del_adj_index)
	  break;

      /* del_adj_index not found in unnormalized_next_hops?  We're done. */
      if (i >= n_nhs)
	continue;

      new_madj = 0;
      if (n_nhs > 1)
	{
	  hash_nhs = lm->next_hop_hash_lookup_key;
	  if (hash_nhs)
	    _vec_len (hash_nhs) = 0;
	  if (i > 0)
	    vec_add (hash_nhs, nhs + 0, i);
	  if (i + 1 < n_nhs)
	    vec_add (hash_nhs, nhs + i + 1, n_nhs - (i + 1));

	  new_madj_index = ip_multipath_adjacency_get (lm, hash_nhs, /* create_if_non_existent */ 1);

	  lm->next_hop_hash_lookup_key = hash_nhs;

	  if (new_madj_index == madj_index)
	    continue;

	  new_madj = vec_elt_at_index (lm->multipath_adjacencies, new_madj_index);
	}

      lm->adjacency_remap_table[madj->adj_index] = new_madj ? 1 + new_madj->adj_index : ~0;
      lm->n_adjacency_remaps += 1;
      ip_multipath_adjacency_free (lm, madj);
    }
}

void
ip_multipath_adjacency_free (ip_lookup_main_t * lm,
			     ip_multipath_adjacency_t * a)
{
  heap_dealloc (lm->next_hop_heap, a->normalized_next_hops.heap_handle);
  heap_dealloc (lm->next_hop_heap, a->unnormalized_next_hops.heap_handle);
  ip_del_adjacency2 (lm, a->adj_index, a->reference_count == 0);
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

  /* 5% max error tolerance for multipath. */
  lm->multipath_next_hop_error_tolerance = .05;
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

  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_REWRITE:
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
  u32 address_valid, dst_address_len, is_ip4, table_id, is_del;
  u32 weight, * weights = 0;
  u32 sw_if_index, * sw_if_indices = 0;
  ip4_address_t ip4_addr, ip4_dst_address, * ip4_via_next_hops = 0;
  ip_adjacency_t parse_adj, * add_adj = 0;

  is_ip4 = 0;
  is_del = 0;
  table_id = 0;
  address_valid = 0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "table %d", &table_id))
	;
      else if (unformat (input, "del"))
	is_del = 1;
      else if (unformat (input, "add"))
	is_del = 0;

      else if (unformat (input, "%U/%d",
			 unformat_ip4_address, &ip4_addr,
			 &dst_address_len))
	{
	  is_ip4 = 1;
	  ip4_dst_address = ip4_addr;
	  address_valid = 1;
	}

      else if (unformat (input, "via %U %U weight %u",
			 unformat_ip4_address, &ip4_addr,
			 unformat_vlib_sw_interface, vm, &sw_if_index,
			 &weight))
	{
	  vec_add1 (ip4_via_next_hops, ip4_addr);
	  vec_add1 (sw_if_indices, sw_if_index);
	  vec_add1 (weights, weight);
	}

      else if (unformat (input, "via %U %U",
			 unformat_ip4_address, &ip4_addr,
			 unformat_vlib_sw_interface, vm, &sw_if_index))
	{
	  vec_add1 (ip4_via_next_hops, ip4_addr);
	  vec_add1 (sw_if_indices, sw_if_index);
	  vec_add1 (weights, 1);
	}
			 
      else if (unformat (input, "via %U",
			 unformat_ip_adjacency, vm, &parse_adj, ip4_rewrite_node.index))
	vec_add1 (add_adj, parse_adj);

      else
	{
	  error = unformat_parse_error (input);
	  goto done;
	}
    }
    
  if (! address_valid)
    {
      error = clib_error_return (0, "expected ip4/ip6 destination address/length.");
      goto done;
    }

  if (! is_del && vec_len (add_adj) + vec_len (weights) == 0)
    {
      error = clib_error_return (0, "no next hops or adjacencies to add.");
      goto done;
    }

  if (is_ip4)
    {
      if (is_del)
	{
	  if (vec_len (ip4_via_next_hops) == 0)
	    {
	      u32 adj_index;

	      adj_index = ip4_add_del_route (im4, table_id,
					     IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_TABLE_ID,
					     &ip4_dst_address, dst_address_len,
					     /* adj index */ ~0);
	      if (adj_index != ~0)
		ip_del_adjacency (lm, adj_index);
	      else
		{
		  error = clib_error_return
		    (0, "no such route %U",
		     format_ip4_address_and_length, &ip4_dst_address, dst_address_len);
		  goto done;
		}

	      ip4_maybe_remap_adjacencies (im4, table_id, IP4_ROUTE_FLAG_TABLE_ID);
	    }
	  else
	    {
	      u32 i;
	      for (i = 0; i < vec_len (ip4_via_next_hops); i++)
		{
		  error = ip4_add_del_route_next_hop (im4,
						      IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_TABLE_ID,
						      &ip4_dst_address, dst_address_len,
						      &ip4_via_next_hops[i],
						      sw_if_indices[i],
						      weights[i]);
		  if (error)
		    goto done;
		}
	    }
	}

      else
	{
	  if (vec_len (add_adj) > 0)
	    {
	      u32 new_ai, old_ai;
	      ip_adjacency_t * adj;

	      adj = ip_add_adjacency (lm, add_adj, vec_len (add_adj), &new_ai);

	      old_ai = ip4_add_del_route (im4, table_id,
					  IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_TABLE_ID,
					  &ip4_dst_address, dst_address_len,
					  new_ai);

	      /* Delete old adjacency index if present. */
	      if (old_ai != new_ai && old_ai != ~0)
		ip_del_adjacency (lm, old_ai);
	    }
	  else if (vec_len (ip4_via_next_hops) > 0)
	    {
	      u32 i;
	      for (i = 0; i < vec_len (ip4_via_next_hops); i++)
		{
		  error = ip4_add_del_route_next_hop (im4,
						      IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_TABLE_ID,
						      &ip4_dst_address, dst_address_len,
						      &ip4_via_next_hops[i],
						      sw_if_indices[i],
						      weights[i]);
		  if (error)
		    goto done;
		}
	    }
	}
    }

 done:
  vec_free (add_adj);
  vec_free (weights);
  vec_free (ip4_via_next_hops);
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
	  vlib_counter_t c, sum;
	  uword i, j, n_left, n_nhs, adj_index, * result = 0;
	  ip_adjacency_t * adj;
	  ip_multipath_next_hop_t * nhs, tmp_nhs[1];

	  adj_index = r->index;
	  if (n_words_per_result > 1)
	    {
	      result = vec_elt_at_index (results, adj_index);
	      adj_index = result[0];
	    }

	  adj = ip_get_adjacency (lm, adj_index);
	  if (adj->n_adj == 1)
	    {
	      nhs = &tmp_nhs[0];
	      nhs[0].next_hop_adj_index = ~0; /* not used */
	      nhs[0].weight = 1;
	      n_nhs = 1;
	    }
	  else
	    {
	      ip_multipath_adjacency_t * madj;
	      madj = vec_elt_at_index (lm->multipath_adjacencies, adj->heap_handle);
	      nhs = heap_elt_at_index (lm->next_hop_heap, madj->normalized_next_hops.heap_offset);
	      n_nhs = madj->normalized_next_hops.count;
	    }

	  n_left = nhs[0].weight;
	  vlib_counter_zero (&sum);
	  for (i = j = 0; i < adj->n_adj; i++)
	    {
	      n_left -= 1;
	      vlib_get_combined_counter (&lm->adjacency_counters, adj_index + i, &c);
	      vlib_counter_add (&sum, &c);
	      if (n_left == 0)
		{
		  u8 * msg = 0;
		  uword indent;

		  if (j == 0)
		    msg = format (msg, "%-20U",
				  format_ip4_address_and_length,
				  r->address.data, r->address_length);
		  else
		    msg = format (msg, "%U", format_white_space, 20);

		  msg = format (msg, "%16Ld%16Ld ", sum.packets, sum.bytes);

		  indent = vec_len (msg);
		  msg = format (msg, "weight %d, index %d\n%U%U",
				nhs[j].weight, adj_index + i,
				format_white_space, indent,
				format_ip_adjacency,
				vm, lm, adj_index + i);

		  vlib_cli_output (vm, "%v", msg);
		  vec_free (msg);

		  j++;
		  if (j < n_nhs)
		    {
		      n_left = nhs[j].weight;
		      vlib_counter_zero (&sum);
		    }
		}
	    }

	  if (result && lm->format_fib_result)
	    vlib_cli_output (vm, "%20s%U", "", lm->format_fib_result, vm, lm, result, 0);
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
