/*
 * ip/ip4_forward.c: IP v4 forwarding
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

#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>	/* for ethernet_header_t */
#include <vnet/ethernet/arp_packet.h>	/* for ethernet_arp_header_t */

/* This is really, really simple but stupid fib. */
ip_lookup_next_t
ip4_fib_lookup (ip4_main_t * im, u32 sw_if_index, ip4_address_t * dst, u32 * adj_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index = vec_elt (im->fib_index_by_sw_if_index, sw_if_index);
  ip4_fib_t * fib = vec_elt_at_index (im->fibs, fib_index);
  ip_adjacency_t * adj;
  uword * p, * hash, key;
  i32 i, dst_address, ai;

  dst_address = clib_mem_unaligned (&dst->data_u32, u32);
  for (i = ARRAY_LEN (fib->adj_index_by_dst_address) - 1; i >= 0; i--)
    {
      hash = fib->adj_index_by_dst_address[i];
      if (! hash)
	continue;

      key = dst_address & im->fib_masks[i];
      if ((p = hash_get (hash, key)) != 0)
	{
	  ai = p[0];
	  goto done;
	}
    }
    
  /* Nothing matches in table. */
  ai = lm->miss_adj_index;

 done:
  *adj_index = ai;
  adj = ip_get_adjacency (lm, ai);
  return adj->lookup_next_index;
}

static ip4_fib_t *
create_fib_with_table_id (ip4_main_t * im, u32 table_id)
{
  ip4_fib_t * fib;
  hash_set (im->fib_index_by_table_id, table_id, vec_len (im->fibs));
  vec_add2 (im->fibs, fib, 1);
  fib->table_id = table_id;
  fib->index = fib - im->fibs;
  return fib;
}

static ip4_fib_t *
find_fib_by_table_index_or_id (ip4_main_t * im, u32 table_index_or_id, u32 flags)
{
  uword * p, fib_index;

  fib_index = table_index_or_id;
  if (! (flags & IP4_ROUTE_FLAG_FIB_INDEX))
    {
      p = hash_get (im->fib_index_by_table_id, table_index_or_id);
      if (! p)
	return create_fib_with_table_id (im, table_index_or_id);
      fib_index = p[0];
    }
  return vec_elt_at_index (im->fibs, fib_index);
}

u32 ip4_add_del_route (ip4_main_t * im,
		       u32 fib_index_or_table_id,
		       u32 flags,
		       ip4_address_t * address,
		       u32 address_length,
		       u32 adj_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, fib_index_or_table_id, flags);
  u32 dst_address = address->data_u32;
  uword * hash, * p, old_adj_index, is_del;
  ip4_add_del_route_callback_t * cb;

  ASSERT (address_length < ARRAY_LEN (im->fib_masks));
  dst_address &= im->fib_masks[address_length];

  if (! fib->adj_index_by_dst_address[address_length])
    {
      ASSERT (lm->fib_result_n_bytes >= sizeof (uword));
      fib->adj_index_by_dst_address[address_length] =
	hash_create (32 /* elts */,
		     /* value size */ round_pow2 (lm->fib_result_n_bytes, sizeof (uword)));
    }

  hash = fib->adj_index_by_dst_address[address_length];

  is_del = (flags & IP4_ROUTE_FLAG_DEL) != 0;

  old_adj_index = ~0;

  /* For deletes callbacks are done before route is inserted. */
  if (is_del)
    {
      if (vec_len (im->add_del_route_callbacks) > 0)
	{
	  p = hash_get (hash, dst_address);
	  if (p != 0)
	    vec_foreach (cb, im->add_del_route_callbacks)
	      cb->function (im, cb->function_opaque,
			    fib, flags,
			    address, address_length,
			    p);
	}

      hash_unset3 (hash, dst_address, &old_adj_index);
      fib->adj_index_by_dst_address[address_length] = hash;
    }
  else
    {
      hash_set3 (hash, dst_address, adj_index, &old_adj_index);
      fib->adj_index_by_dst_address[address_length] = hash;
      /* For adds callbacks are done after route is inserted. */
      if (vec_len (im->add_del_route_callbacks) > 0)
	{
	  p = hash_get (hash, dst_address);
	  vec_foreach (cb, im->add_del_route_callbacks)
	    cb->function (im, cb->function_opaque,
			  fib, flags,
			  address, address_length,
			  p);
	}
    }

  return old_adj_index;
}

clib_error_t *
ip4_add_del_route_next_hop (ip4_main_t * im,
			    u32 flags,
			    ip4_address_t * dst_address,
			    u32 dst_address_length,
			    ip4_address_t * next_hop,
			    u32 next_hop_sw_if_index,
			    u32 next_hop_weight)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index = vec_elt (im->fib_index_by_sw_if_index, next_hop_sw_if_index);
  ip4_fib_t * fib = vec_elt_at_index (im->fibs, fib_index);
  u32 dst_address_u32, old_mp_adj_index, new_mp_adj_index;
  uword * dst_hash, * dst_result, dst_adj_index;
  ip_adjacency_t * dst_adj;
  uword * nh_hash, * nh_result, nh_adj_index;
  ip_multipath_adjacency_t * old_mp, * new_mp;
  int is_del = (flags & IP4_ROUTE_FLAG_DEL) != 0;

  /* Lookup next hop to be added or deleted. */
  nh_hash = fib->adj_index_by_dst_address[32];
  nh_result = hash_get (nh_hash, next_hop->data_u32);

  /* Next hop must be known. */
  if (! nh_result)
    return clib_error_return (0, "next-hop %U/32 not in FIB",
			      format_ip4_address, next_hop);

  nh_adj_index = *nh_result;

  ASSERT (dst_address_length < ARRAY_LEN (im->fib_masks));
  dst_address_u32 = dst_address->data_u32 & im->fib_masks[dst_address_length];

  dst_hash = fib->adj_index_by_dst_address[dst_address_length];
  dst_result = hash_get (dst_hash, dst_address_u32);
  if (dst_result)
    {
      dst_adj_index = dst_result[0];
      dst_adj = ip_get_adjacency (lm, dst_adj_index);
    }
  else
    {
      /* For deletes destination must be known. */
      if (is_del)
	return clib_error_return (0, "unknown destination %U/%d",
				  format_ip4_address, dst_address,
				  dst_address_length);

      dst_adj_index = ~0;
      dst_adj = 0;
    }

  old_mp_adj_index = dst_adj ? dst_adj->heap_handle : ~0;

  if (! ip_multipath_adjacency_add_del_next_hop
      (lm, is_del,
       dst_adj ? dst_adj->heap_handle : ~0,
       nh_adj_index,
       next_hop_weight,
       &new_mp_adj_index))
    return clib_error_return (0, "requested deleting next-hop %U not found in multi-path",
			      format_ip4_address, next_hop);
  
  old_mp = new_mp = 0;
  if (old_mp_adj_index != ~0)
    old_mp = vec_elt_at_index (lm->multipath_adjacencies, old_mp_adj_index);
  if (new_mp_adj_index != ~0)
    new_mp = vec_elt_at_index (lm->multipath_adjacencies, new_mp_adj_index);

  if (old_mp != new_mp)
    ip4_add_del_route (im, fib_index,
		       ((is_del ? IP4_ROUTE_FLAG_DEL : IP4_ROUTE_FLAG_ADD)
			| IP4_ROUTE_FLAG_FIB_INDEX),
		       dst_address,
		       dst_address_length,
		       new_mp ? new_mp->adj_index : dst_adj_index);

  return /* no error */ 0;
}

void *
ip4_get_route (ip4_main_t * im,
	       u32 table_index_or_table_id,
	       u32 flags,
	       u8 * address,
	       u32 address_length)
{
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  u32 dst_address = * (u32 *) address;
  uword * hash, * p;

  ASSERT (address_length < ARRAY_LEN (im->fib_masks));
  dst_address &= im->fib_masks[address_length];

  hash = fib->adj_index_by_dst_address[address_length];
  p = hash_get (hash, dst_address);
  return (void *) p;
}

ip4_address_t *
ip4_foreach_matching_route (ip4_main_t * im,
			    u32 table_index_or_table_id,
			    u32 flags,
			    u8 * address,
			    u32 address_length,
			    u32 * result_length,
			    ip4_address_t * results)
{
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  u32 dst_address = * (u32 *) address;
  u32 this_length = *result_length;
  
  if (results)
    _vec_len (results) = 0;
  while (this_length <= 32 && vec_len (results) == 0)
    {
      uword k, v;
      hash_foreach (k, v, fib->adj_index_by_dst_address[this_length], ({
	    if (0 == ((k ^ dst_address) & im->fib_masks[address_length]))
	      {
		ip4_address_t a;
		a.data_u32 = k;
		vec_add1 (results, a);
	      }
	  }));

      this_length++;
    }
  *result_length = this_length;
  return results;
}

void ip4_maybe_remap_adjacencies (ip4_main_t * im,
				  u32 table_index_or_table_id,
				  u32 flags)
{
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 i, l;
  static u32 * to_delete;

  if (lm->n_adjacency_remaps == 0)
    return;

  for (l = 0; l <= 32; l++)
    {
      hash_pair_t * p;
      uword * hash = fib->adj_index_by_dst_address[l];

      if (hash_elts (hash) == 0)
	continue;

      if (to_delete)
	_vec_len (to_delete) = 0;

      hash_foreach_pair (p, hash, ({
	    u32 adj_index = p->value[0];
	    u32 m = vec_elt (lm->adjacency_remap_table, adj_index);
	    if (m != 0)
	      {
		/* Reset mapping table. */
		lm->adjacency_remap_table[adj_index] = 0;

		/* New adjacency points to nothing: so delete prefix. */
		if (m == ~0)
		  vec_add1 (to_delete, p->key);
		else
		  {
		    ip4_add_del_route_callback_t * cb;
		    ip4_address_t a;

		    /* Remap to new adjacency. */
		    a.data_u32 = p->key;
		    p->value[0] = m - 1;
		    vec_foreach (cb, im->add_del_route_callbacks)
		      cb->function (im, cb->function_opaque,
				    fib, flags | IP4_ROUTE_FLAG_DEL,
				    &a, l,
				    p->value);
		  }
	      }
      }));

      for (i = 0; i < vec_len (to_delete); i++)
	hash_unset (hash, to_delete[i]);
    }

  /* All remaps have been performed. */
  lm->n_adjacency_remaps = 0;
}

void ip4_delete_matching_routes (ip4_main_t * im,
				 u32 table_index_or_table_id,
				 u32 flags,
				 u8 * address,
				 u32 address_length)
{
  static ip4_address_t * matching_addresses;
  u32 l, i, adj_index;

  for (l = address_length + 1; l <= 32; )
    {
      matching_addresses
	= ip4_foreach_matching_route (im, table_index_or_table_id, flags,
				      address,
				      l,
				      &l,
				      matching_addresses);
      for (i = 0; i < vec_len (matching_addresses); i++)
	{
	  adj_index =
	    ip4_add_del_route
	    (im,
	     table_index_or_table_id,
	     IP4_ROUTE_FLAG_DEL | flags,
	     &matching_addresses[i],
	     l - 1,
	     /* adj_index */ ~0);

	  ip_del_adjacency (&im->lookup_main, adj_index);
	}
    }

  ip4_maybe_remap_adjacencies (im, table_index_or_table_id, flags);
}

static uword
ip4_lookup (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
  vlib_combined_counter_main_t * cm = &im->lookup_main.adjacency_counters;
  u32 n_left_from, n_left_to_next, * from, * to_next;
  ip_lookup_next_t next;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next = node->cached_next_index;

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  u32 pi0, pi1, adj_index0, adj_index1, wrong_next;
	  ip_lookup_next_t next0, next1;
	  ip_buffer_opaque_t * i0, * i1;
	  ip4_header_t * ip0, * ip1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);
	  }

	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  next0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, &adj_index0);
	  next1 = ip4_fib_lookup (im, p1->sw_if_index[VLIB_RX], &ip1->dst_address, &adj_index1);

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  i0->dst_adj_index = adj_index0;
	  i1->dst_adj_index = adj_index1;

	  vlib_buffer_increment_two_counters (vm, cm,
					      adj_index0, adj_index1,
					      p0, p1);
	  from += 2;
	  to_next += 2;
	  n_left_to_next -= 2;
	  n_left_from -= 2;

	  wrong_next = (next0 != next) + 2*(next1 != next);
	  if (PREDICT_FALSE (wrong_next != 0))
	    {
	      switch (wrong_next)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = pi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next0, pi0);
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  vlib_set_next_frame_buffer (vm, node, next1, pi1);
		  break;

		case 3:
		  /* A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  vlib_set_next_frame_buffer (vm, node, next0, pi0);
		  vlib_set_next_frame_buffer (vm, node, next1, pi1);
		  if (next0 == next1)
		    {
		      /* A B B */
		      vlib_put_next_frame (vm, node, next, n_left_to_next);
		      next = next1;
		      vlib_get_next_frame (vm, node, next, to_next, n_left_to_next);
		    }
		}
	    }
	}
    
      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  u32 pi0, adj_index0;
	  ip_lookup_next_t next0;

	  pi0 = from[0];

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);
	  next0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, &adj_index0);

	  to_next[0] = pi0;

	  i0 = vlib_get_buffer_opaque (p0);

	  i0->dst_adj_index = adj_index0;

	  vlib_buffer_increment_counter (vm, cm, adj_index0, p0);
	  from += 1;
	  to_next += 1;
	  n_left_to_next -= 1;
	  n_left_from -= 1;

	  if (PREDICT_FALSE (next0 != next))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next, n_left_to_next);
	      next = next0;
	      vlib_get_next_frame (vm, node, next,
				   to_next, n_left_to_next);
	      to_next[0] = pi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}

      vlib_put_next_frame (vm, node, next, n_left_to_next);
    }

  return frame->n_vectors;
}

static void
ip4_add_interface_routes (vlib_main_t * vm, u32 sw_if_index,
			  ip4_main_t * im, u32 fib_index,
			  ip4_address_t * address, u32 address_length)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_adjacency_t * adj;
  u32 adj_index;

  if (address_length < 32)
    {
      adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			      &adj_index);
      ip_adjacency_set_arp (vm, adj, sw_if_index);

      ip4_add_del_route (im, fib_index,
			 IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_FIB_INDEX,
			 address,
			 address_length,
			 adj_index);
    }

  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &adj_index);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;

  ip4_add_del_route (im, fib_index,
		     IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_FIB_INDEX,
		     address,
		     32,
		     adj_index);
}

static void
ip4_del_interface_routes (ip4_main_t * im, u32 fib_index,
			  ip4_address_t * address, u32 address_length)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 adj_index;

  ASSERT (ip4_interface_address_is_valid (address));

  if (address_length < 32)
    {
      adj_index = ip4_add_del_route (im, fib_index,
				     IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_FIB_INDEX,
				     address,
				     address_length,
				     /* adj_index */ ~0);

      ip_del_adjacency (lm, adj_index);
    }

  adj_index
    = ip4_add_del_route (im, fib_index,
			 IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_FIB_INDEX,
			 address,
			 32,
			 /* adj_index */ ~0);

  ip_del_adjacency (lm, adj_index);

  ip4_delete_matching_routes (im, fib_index, IP4_ROUTE_FLAG_FIB_INDEX,
			      address->data,
			      address_length);
}

void
ip4_set_interface_address (vlib_main_t * vm, u32 sw_if_index,
			   ip4_address_t * new_address, uword new_length)
{
  ip4_main_t * im = &ip4_main;
  ip4_address_t old_address;
  uword old_length;

  ASSERT (sw_if_index < vec_len (im->ip4_address_by_sw_if_index));
  ASSERT (sw_if_index < vec_len (im->ip4_address_length_by_sw_if_index));

  old_address = im->ip4_address_by_sw_if_index[sw_if_index];
  old_length = im->ip4_address_length_by_sw_if_index[sw_if_index];

  if (new_address->data_u32 == old_address.data_u32
      && old_length == new_length)
    return;

  im->ip4_address_by_sw_if_index[sw_if_index] = new_address[0];
  im->ip4_address_length_by_sw_if_index[sw_if_index] = new_length;

  if (vlib_sw_interface_is_admin_up (vm, sw_if_index))
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];

      if (ip4_interface_address_is_valid (&old_address))
	ip4_del_interface_routes (im, fib_index, &old_address, old_length);

      if (new_address->data_u32 != ~0)
	ip4_add_interface_routes (vm, sw_if_index,
				  im, fib_index, new_address, new_length);
    }

  {
    ip4_set_interface_address_callback_t * cb;
    vec_foreach (cb, im->set_interface_address_callbacks)
      cb->function (im, cb->function_opaque, sw_if_index,
		    new_address, new_length);
  }
}

static clib_error_t *
ip4_sw_interface_admin_up_down (vlib_main_t * vm,
				u32 sw_if_index,
				u32 flags)
{
  ip4_main_t * im = &ip4_main;
  ip4_address_t * a;
  uword is_admin_up;

  /* Fill in lookup tables with default table (0). */
  vec_validate (im->fib_index_by_sw_if_index, sw_if_index);

  /* Validate interface address/length table. */
  {
    ip4_address_t null;

    null.data_u32 = ~0;
    ASSERT (! ip4_interface_address_is_valid (&null));
    vec_validate_init_empty (im->ip4_address_by_sw_if_index, sw_if_index, null);
    vec_validate_init_empty (im->ip4_address_length_by_sw_if_index, sw_if_index, ~0);
  }

  is_admin_up = (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) != 0;
  a = ip4_get_interface_address (im, sw_if_index);
  if (ip4_interface_address_is_valid (a))
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];
      u32 length = ip4_get_interface_address_length (im, sw_if_index);

      if (is_admin_up)
	ip4_add_interface_routes (vm, sw_if_index,
				  im, fib_index,
				  a, length);
      else
	ip4_del_interface_routes (im, fib_index,
				  a, length);
    }

  return 0;
}

static VLIB_REGISTER_NODE (ip4_lookup_node) = {
  .function = ip4_lookup,
  .name = "ip4-lookup",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP_LOOKUP_N_NEXT,
  .next_nodes = {
    [IP_LOOKUP_NEXT_MISS] = "ip4-miss",
    [IP_LOOKUP_NEXT_DROP] = "ip4-drop",
    [IP_LOOKUP_NEXT_PUNT] = "ip4-punt",
    [IP_LOOKUP_NEXT_LOCAL] = "ip4-local",
    [IP_LOOKUP_NEXT_ARP] = "ip4-arp",
    [IP_LOOKUP_NEXT_REWRITE] = "ip4-rewrite",
    [IP_LOOKUP_NEXT_MULTIPATH] = "ip4-multipath",
  },

  .sw_interface_admin_up_down_function = ip4_sw_interface_admin_up_down,
};

/* Global IP4 main. */
ip4_main_t ip4_main;

static clib_error_t *
ip4_lookup_init (vlib_main_t * vm)
{
  ip4_main_t * im = &ip4_main;
  uword i;

  for (i = 0; i < ARRAY_LEN (im->fib_masks); i++)
    {
      u32 m;

      if (i < 32)
	m = pow2_mask (i) << (32 - i);
      else 
	m = ~0;
      im->fib_masks[i] = clib_host_to_net_u32 (m);
    }

  /* Create FIB with index 0 and table id of 0. */
  find_fib_by_table_index_or_id (im, /* table id */ 0, IP4_ROUTE_FLAG_TABLE_ID);

  ip_lookup_init (&im->lookup_main, ip4_lookup_node.index);

  {
    ethernet_and_arp_header_t h[2];

    memset (&h, 0, sizeof (h));

    /* Send to broadcast address ffff.ffff.ffff */
    memset (h[0].ethernet.dst_address, ~0, sizeof (h[0].ethernet.dst_address));
    memset (h[1].ethernet.dst_address, ~0, sizeof (h[1].ethernet.dst_address));

    /* Set target ethernet address to all zeros. */
    memset (h[0].arp.ip4_over_ethernet[1].ethernet, 0, sizeof (h[0].arp.ip4_over_ethernet[1].ethernet));
    memset (h[1].arp.ip4_over_ethernet[1].ethernet, ~0, sizeof (h[1].arp.ip4_over_ethernet[1].ethernet));

#define _16(f,v) h[0].f = clib_host_to_net_u16 (v); h[1].f = ~0
#define _8(f,v) h[0].f = v; h[1].f = ~0
    _16 (ethernet.type, ETHERNET_TYPE_ARP);
    _16 (arp.l2_type, ETHERNET_ARP_HARDWARE_TYPE_ethernet);
    _16 (arp.l3_type, ETHERNET_TYPE_IP);
    _8 (arp.n_l2_address_bytes, 6);
    _8 (arp.n_l3_address_bytes, 4);
    _16 (arp.opcode, ETHERNET_ARP_OPCODE_request);
#undef _16
#undef _8

    vlib_packet_template_init (vm,
			       &im->ip4_arp_request_packet_template,
			       /* data */ &h[0],
			       /* mask */ &h[1],
			       sizeof (h[0]),
			       /* alloc chunk size */ 8);
  }

  return 0;
}

VLIB_INIT_FUNCTION (ip4_lookup_init);

typedef struct {
  /* Adjacency taken. */
  u32 adj_index;

  /* Packet data, possibly *after* rewrite. */
  u8 packet_data[64 - 1*sizeof(u32)];
} ip4_forward_next_trace_t;

static u8 * format_ip4_forward_next_trace (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  ip4_forward_next_trace_t * t = va_arg (*args, ip4_forward_next_trace_t *);
  ip4_main_t * im = &ip4_main;
  ip_adjacency_t * adj;
  uword indent = format_get_indent (s);

  adj = ip_get_adjacency (&im->lookup_main, t->adj_index);
  s = format (s, "adjacency: %U",
	      format_ip_adjacency,
	      vm, &im->lookup_main, t->adj_index);
  switch (adj->lookup_next_index)
    {
    case IP_LOOKUP_NEXT_MULTIPATH:
    case IP_LOOKUP_NEXT_REWRITE:
      s = format (s, "\n%U%U",
		  format_white_space, indent,
		  format_ip_adjacency_packet_data,
		  vm, &im->lookup_main, t->adj_index,
		  t->packet_data, sizeof (t->packet_data));
      break;

    default:
      break;
    }

  return s;
}

/* Common trace function for all ip4-forward next nodes. */
static void
ip4_forward_next_trace (vlib_main_t * vm,
			vlib_node_runtime_t * node,
			vlib_frame_t * frame)
{
  u32 * from, n_left;

  n_left = frame->n_vectors;
  from = vlib_frame_vector_args (frame);
  
  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      ip_buffer_opaque_t * i0, * i1;
      ip4_forward_next_trace_t * t0, * t1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, from[2], LOAD);
      vlib_prefetch_buffer_with_index (vm, from[3], LOAD);

      bi0 = from[0];
      bi1 = from[1];

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      i0 = vlib_get_buffer_opaque (b0);
      i1 = vlib_get_buffer_opaque (b1);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = i0->dst_adj_index;
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      if (b1->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));
	  t1->adj_index = i1->dst_adj_index;
	  memcpy (t1->packet_data,
		  vlib_buffer_get_current (b1),
		  sizeof (t1->packet_data));
	}
      from += 2;
      n_left -= 2;
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      ip_buffer_opaque_t * i0;
      ip4_forward_next_trace_t * t0;

      bi0 = from[0];

      b0 = vlib_get_buffer (vm, bi0);
      i0 = vlib_get_buffer_opaque (b0);

      if (b0->flags & VLIB_BUFFER_IS_TRACED)
	{
	  t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
	  t0->adj_index = i0->dst_adj_index;
	  memcpy (t0->packet_data,
		  vlib_buffer_get_current (b0),
		  sizeof (t0->packet_data));
	}
      from += 1;
      n_left -= 1;
    }
}

static uword
ip4_drop_or_punt (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame,
		  ip4_error_t error_code)
{
  u32 * buffers = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   buffers,
			   /* stride */ 1,
			   n_packets,
			   /* next */ 0,
			   ip4_input_node.index,
			   error_code);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return n_packets;
}

static uword
ip4_drop (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_ADJACENCY_DROP); }

static uword
ip4_punt (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_ADJACENCY_PUNT); }

static uword
ip4_miss (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip4_drop_or_punt (vm, node, frame, IP4_ERROR_DST_LOOKUP_MISS); }

static VLIB_REGISTER_NODE (ip4_drop_node) = {
  .function = ip4_drop,
  .name = "ip4-drop",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (ip4_punt_node) = {
  .function = ip4_punt,
  .name = "ip4-punt",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-punt",
  },
};

static VLIB_REGISTER_NODE (ip4_miss_node) = {
  .function = ip4_miss,
  .name = "ip4-miss",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

/* Compute TCP/UDP checksum in software. */
u32 ip4_tcp_udp_checksum (vlib_buffer_t * p0)
{
  ip4_header_t * ip0;
  ip_csum_t sum0;
  u16 sum16;

  ip0 = vlib_buffer_get_current (p0);

  ASSERT (ip0->protocol == IP_PROTOCOL_TCP
	  || ip0->protocol == IP_PROTOCOL_UDP);

  if (ip0->protocol == IP_PROTOCOL_TCP)
    {
      tcp_header_t * tcp0 = ip4_next_header (ip0);
      u32 tcp_len0;

      tcp_len0 = clib_net_to_host_u16 (ip0->length) - ip4_header_bytes (ip0);

      /* Initialize checksum with header. */
      sum0 = clib_mem_unaligned (&ip0->src_address, u64);

      sum0 = ip_csum_with_carry
	(sum0, clib_host_to_net_u32 (tcp_len0 + (ip0->protocol << 16)));

      sum0 = ip_incremental_checksum (sum0, tcp0, tcp_len0);

      sum16 = ~ ip_csum_fold (sum0);
    }
  else
    {
      udp_header_t * udp0 = ip4_next_header (ip0);
      u32 udp_len0;

      if (udp0->checksum == 0)
	{
	  sum16 = 0;
	  goto done;
	}

      sum0 = clib_mem_unaligned (&ip0->src_address, u64);

      udp_len0 = clib_net_to_host_u16 (udp0->length);
      sum0 = ip_csum_with_carry
	(sum0, clib_host_to_net_u32 (udp_len0 + (ip0->protocol << 16)));

      sum0 = ip_incremental_checksum (sum0, udp0, udp_len0);

      sum16 = ~ ip_csum_fold (sum0);
    }

 done:
  p0->flags |= IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED;
  if (sum16 == 0)
    p0->flags |= IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT;

  return p0->flags;
}

static uword
ip4_local (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_local_next_t next_index;
  u32 * from, * to_next, n_left_from, n_left_to_next;
  vlib_error_t unknown_protocol, tcp_checksum, udp_checksum, udp_length, src_lookup_miss;
  vlib_error_t * to_next_error, to_next_error_dummy[2];

  unknown_protocol = vlib_error_set (ip4_input_node.index, IP4_ERROR_UNKNOWN_PROTOCOL);
  src_lookup_miss = vlib_error_set (ip4_input_node.index, IP4_ERROR_SRC_LOOKUP_MISS);
  tcp_checksum = vlib_error_set (ip4_input_node.index, IP4_ERROR_TCP_CHECKSUM);
  udp_checksum = vlib_error_set (ip4_input_node.index, IP4_ERROR_UDP_CHECKSUM);
  udp_length = vlib_error_set (ip4_input_node.index, IP4_ERROR_UDP_LENGTH);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  while (n_left_from > 0)
    {
      vlib_get_next_frame_transpose (vm, node, next_index,
				     to_next, n_left_to_next);
      to_next_error = (next_index < IP_LOCAL_N_NEXT
		       ? vlib_error_for_transpose_buffer_pointer (to_next)
		       : &to_next_error_dummy[0]);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip_local_buffer_opaque_t * i0, * i1;
	  ip4_header_t * ip0, * ip1;
	  udp_header_t * udp0, * udp1;
	  vlib_error_t error0, error1;
	  u32 pi0, ip_len0, udp_len0, flags0, adj_index0, next0;
	  u32 pi1, ip_len1, udp_len1, flags1, adj_index1, next1;
	  i32 len_diff0, len_diff1;
	  u8 is_error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
	  u8 is_error1, is_udp1, is_tcp_udp1, good_tcp_udp1, proto1;
	  u8 enqueue_code;
      
	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];
	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  adj_index0 = i0->non_local.dst_adj_index;
	  adj_index1 = i1->non_local.dst_adj_index;

	  proto0 = ip0->protocol;
	  proto1 = ip1->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_udp1 = proto1 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;
	  is_tcp_udp1 = is_udp1 || proto1 == IP_PROTOCOL_TCP;

	  flags0 = p0->flags;
	  flags1 = p1->flags;

	  good_tcp_udp0 = (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
	  good_tcp_udp1 = (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  udp0 = ip4_next_header (ip0);
	  udp1 = ip4_next_header (ip1);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);
	  ip_len1 = clib_net_to_host_u16 (ip1->length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);
	  udp_len1 = clib_net_to_host_u16 (udp1->length);

	  len_diff0 = ip_len0 - udp_len0;
	  len_diff1 = ip_len1 - udp_len1;

	  len_diff0 = is_udp0 ? len_diff0 : 0;
	  len_diff1 = is_udp1 ? len_diff1 : 0;

	  if (PREDICT_FALSE (! (is_tcp_udp0 & is_tcp_udp1
				& good_tcp_udp0 & good_tcp_udp1)))
	    {
	      if (is_tcp_udp0)
		{
		  if (! (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags0 = ip4_tcp_udp_checksum (p0);
		  good_tcp_udp0 =
		    (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
		}
	      if (is_tcp_udp1)
		{
		  if (! (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags1 = ip4_tcp_udp_checksum (p1);
		  good_tcp_udp1 =
		    (flags1 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;
		}
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;
	  good_tcp_udp1 &= len_diff1 >= 0;

	  error0 = error1 = unknown_protocol;

	  error0 = len_diff0 < 0 ? udp_length : error0;
	  error1 = len_diff1 < 0 ? udp_length : error1;

	  ASSERT (tcp_checksum + 1 == udp_checksum);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? tcp_checksum + is_udp0
		    : error0);
	  error1 = (is_tcp_udp1 && ! good_tcp_udp1
		    ? tcp_checksum + is_udp1
		    : error1);

	  if (error0 == unknown_protocol
	      && i0->non_local.src_adj_index == ~0)
	    {
	      ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX],
			      &ip0->src_address,
			      &i0->non_local.src_adj_index);
	      error0 = (lm->miss_adj_index == i0->non_local.src_adj_index
			? src_lookup_miss
			: error0);
	    }
	  if (error1 == unknown_protocol
	      && i1->non_local.src_adj_index == ~0)
	    {
	      ip4_fib_lookup (im, p1->sw_if_index[VLIB_RX],
			      &ip1->src_address,
			      &i1->non_local.src_adj_index);
	      error1 = (lm->miss_adj_index == i1->non_local.src_adj_index
			? src_lookup_miss
			: error1);
	    }

	  is_error0 = error0 != unknown_protocol;
	  is_error1 = error1 != unknown_protocol;

	  next0 = lm->local_next_by_ip_protocol[proto0];
	  next1 = lm->local_next_by_ip_protocol[proto1];

	  next0 = is_error0 ? IP_LOCAL_NEXT_DROP : next0;
	  next1 = is_error1 ? IP_LOCAL_NEXT_DROP : next1;

	  to_next_error[0] = error0;
	  to_next_error[is_error0] = error1;
	  to_next_error += is_error0 + is_error1;

	  enqueue_code = (next0 != next_index) + 2*(next1 != next_index);

	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      u32 * b;

	      switch (enqueue_code)
		{
		case 1:
		  /* A B A */
		  to_next[-2] = pi1;
		  to_next -= 1;
		  n_left_to_next += 1;
		  b = vlib_set_next_frame (vm, node, next0);
		  b[0] = pi0;
		  if (is_error0)
		    *vlib_error_for_transpose_buffer_pointer (b) = error0;
		  break;

		case 2:
		  /* A A B */
		  to_next -= 1;
		  n_left_to_next += 1;
		  b = vlib_set_next_frame (vm, node, next1);
		  b[0] = pi1;
		  if (is_error1)
		    *vlib_error_for_transpose_buffer_pointer (b) = error1;
		  break;

		case 3:
		  /* A B B or A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  b = vlib_set_next_frame (vm, node, next0);
		  b[0] = pi0;
		  if (is_error0)
		    *vlib_error_for_transpose_buffer_pointer (b) = error0;

		  b = vlib_set_next_frame (vm, node, next1);
		  b[0] = pi1;
		  if (is_error1)
		    *vlib_error_for_transpose_buffer_pointer (b) = error1;

		  if (next0 == next1)
		    {
		      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		      next_index = next1;
		      vlib_get_next_frame_transpose (vm, node, next_index, to_next, n_left_to_next);
		      to_next_error = (next_index < IP_LOCAL_N_NEXT
				       ? vlib_error_for_transpose_buffer_pointer (to_next)
				       : &to_next_error_dummy[0]);
		    }
		  break;
		}
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  ip_local_buffer_opaque_t * i0;
	  udp_header_t * udp0;
	  vlib_error_t error0;
	  u32 pi0, next0, ip_len0, udp_len0, flags0, adj_index0;
	  i32 len_diff0;
	  u8 is_error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
      
	  pi0 = to_next[0] = from[0];
	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->non_local.dst_adj_index;

	  ip0 = vlib_buffer_get_current (p0);

	  proto0 = ip0->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;
	  next0 = lm->local_next_by_ip_protocol[proto0];

	  flags0 = p0->flags;

	  good_tcp_udp0 = (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  udp0 = ip4_next_header (ip0);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 |= (is_udp0 && udp0->checksum == 0);

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);

	  len_diff0 = ip_len0 - udp_len0;

	  len_diff0 = is_udp0 ? len_diff0 : 0;

	  if (PREDICT_FALSE (! (is_tcp_udp0 & good_tcp_udp0)))
	    {
	      if (! (flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		flags0 = ip4_tcp_udp_checksum (p0);
	      good_tcp_udp0 =
		(flags0 & IP4_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
	      good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;

	  error0 = unknown_protocol;

	  error0 = len_diff0 < 0 ? udp_length : error0;

	  ASSERT (tcp_checksum + 1 == udp_checksum);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? tcp_checksum + is_udp0
		    : error0);

	  next0 = lm->local_next_by_ip_protocol[proto0];

	  if (error0 == unknown_protocol
	      && i0->non_local.src_adj_index == ~0)
	    {
	      ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX],
			      &ip0->src_address,
			      &i0->non_local.src_adj_index);
	      error0 = (lm->miss_adj_index == i0->non_local.src_adj_index
			? src_lookup_miss
			: error0);
	    }

	  is_error0 = error0 != unknown_protocol;
	  next0 = is_error0 ? IP_LOCAL_NEXT_DROP : next0;

	  to_next_error[0] = error0;
	  to_next_error += is_error0;

	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);

	      next_index = next0;
	      vlib_get_next_frame_transpose (vm, node, next_index,
					     to_next, n_left_to_next);
	      to_next_error = (next_index < IP_LOCAL_N_NEXT
			       ? vlib_error_for_transpose_buffer_pointer (to_next)
			       : &to_next_error_dummy[0]);

	      to_next[0] = pi0;
	      to_next_error[0] = error0;
	      to_next += 1;
	      to_next_error += is_error0;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

static VLIB_REGISTER_NODE (ip4_local_node) = {
  .function = ip4_local,
  .name = "ip4-local",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = IP_LOCAL_N_NEXT,
  .next_nodes = {
    [IP_LOCAL_NEXT_DROP] = "error-drop-transpose",
    [IP_LOCAL_NEXT_PUNT] = "error-punt-transpose",
    [IP_LOCAL_NEXT_TCP_LOOKUP] = "tcp4-lookup",
    [IP_LOCAL_NEXT_UDP_LOOKUP] = "udp4-lookup",
  },
};

typedef enum {
  IP4_ARP_NEXT_DROP,
  IP4_ARP_N_NEXT,
} ip4_arp_next_t;

typedef enum {
  IP4_ARP_ERROR_DROP,
  IP4_ARP_ERROR_REQUEST_SENT,
} ip4_arp_error_t;

static uword
ip4_arp (vlib_main_t * vm,
	 vlib_node_runtime_t * node,
	 vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 * from, * to_next_drop;
  uword n_left_from, n_left_to_next_drop, next_index;
  static f64 time_last_seed_change = -1e100;
  static u32 hash_seeds[3];
  static uword hash_bitmap[256 / BITS (uword)]; 
  f64 time_now;
  vlib_error_t * to_next_drop_error;
  vlib_error_t arp_drop_error_no_code = vlib_error_set (node->node_index, 0);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  time_now = vlib_time_now (vm);
  if (time_now - time_last_seed_change > 1e-3)
    {
      uword i;
      u32 * r = clib_random_buffer_get_data (&vm->random_buffer,
					     sizeof (hash_seeds));
      for (i = 0; i < ARRAY_LEN (hash_seeds); i++)
	hash_seeds[i] = r[i];

      /* Mark all hash keys as been not-seen before. */
      for (i = 0; i < ARRAY_LEN (hash_bitmap); i++)
	hash_bitmap[i] = 0;

      time_last_seed_change = time_now;
    }

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  if (next_index == IP4_ARP_NEXT_DROP)
    next_index = IP4_ARP_N_NEXT; /* point to first interface */

  while (n_left_from > 0)
    {
      vlib_get_next_frame_transpose (vm, node, IP4_ARP_NEXT_DROP,
				     to_next_drop, n_left_to_next_drop);
      to_next_drop_error = vlib_error_for_transpose_buffer_pointer (to_next_drop);

      while (n_left_from > 0 && n_left_to_next_drop > 0)
	{
	  vlib_buffer_t * p0;
	  ip_buffer_opaque_t * i0;
	  ip4_header_t * ip0;
	  u32 pi0, adj_index0, a0, b0, c0, m0, sw_if_index0, drop0;
	  uword bm0;
	  ip_adjacency_t * adj0;
	  u32 next0;

	  pi0 = from[0];

	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->dst_adj_index;

	  ip0 = vlib_buffer_get_current (p0);

	  adj0 = ip_get_adjacency (lm, adj_index0);

	  a0 = hash_seeds[0];
	  b0 = hash_seeds[1];
	  c0 = hash_seeds[2];

	  sw_if_index0 = adj0->rewrite_header.sw_if_index;
	  p0->sw_if_index[VLIB_TX] = sw_if_index0;

	  a0 ^= ip0->dst_address.data_u32;
	  b0 ^= sw_if_index0;

	  hash_v3_finalize32 (a0, b0, c0);

	  c0 &= BITS (hash_bitmap) - 1;
	  c0 = c0 / BITS (uword);
	  m0 = (uword) 1 << (c0 % BITS (uword));

	  bm0 = hash_bitmap[c0];
	  drop0 = (bm0 & m0) != 0;
	  next0 = drop0 ? IP4_ARP_NEXT_DROP : adj0->rewrite_header.next_index;

	  /* Mark it as seen. */
	  hash_bitmap[c0] = bm0 | m0;

	  from += 1;
	  n_left_from -= 1;
	  to_next_drop[0] = pi0;
	  to_next_drop_error[0]
	    = vlib_error_set_code (arp_drop_error_no_code,
				   drop0 ? IP4_ARP_ERROR_DROP : IP4_ARP_ERROR_REQUEST_SENT);
	  to_next_drop += 1;
	  to_next_drop_error += 1;
	  n_left_to_next_drop -= 1;

	  if (drop0)
	    continue;

	  {
	    u32 bi0, * to_next_request;
	    ethernet_and_arp_header_t * h0;
	    vlib_sw_interface_t * swif0;
	    ethernet_interface_t * eif0;
	    ip4_address_t * swif_ip0;
	    u8 * eth_addr0, dummy[6] = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, };

	    h0 = vlib_packet_template_get_packet (vm, &im->ip4_arp_request_packet_template, &bi0);

	    swif0 = vlib_get_sup_sw_interface (vm, sw_if_index0);
	    ASSERT (swif0->type == VLIB_SW_INTERFACE_TYPE_HARDWARE);
	    eif0 = ethernet_get_interface (&ethernet_main, swif0->hw_if_index);
	    eth_addr0 = eif0 ? eif0->address : dummy;
	    memcpy (h0->ethernet.src_address, eth_addr0, sizeof (h0->ethernet.src_address));
	    memcpy (h0->arp.ip4_over_ethernet[0].ethernet, eth_addr0, sizeof (h0->arp.ip4_over_ethernet[0].ethernet));

	    swif_ip0 = ip4_get_interface_address (im, sw_if_index0);
	    h0->arp.ip4_over_ethernet[0].ip4.data_u32 = swif_ip0->data_u32;

	    /* Copy in destination address we are requesting. */
	    h0->arp.ip4_over_ethernet[1].ip4.data_u32 = ip0->dst_address.data_u32;

	    vlib_buffer_copy_trace_flag (vm, p0, bi0);

	    to_next_request = vlib_set_next_frame (vm, node, next0);
	    to_next_request[0] = bi0;
	  }
	}

      vlib_put_next_frame (vm, node, IP4_ARP_NEXT_DROP, n_left_to_next_drop);
    }

  return frame->n_vectors;
}

static char * ip4_arp_error_strings[] = {
  [IP4_ARP_ERROR_DROP] = "address overflow drops",
  [IP4_ARP_ERROR_REQUEST_SENT] = "ARP requests sent",
};

VLIB_REGISTER_NODE (ip4_arp_node) = {
  .function = ip4_arp,
  .name = "ip4-arp",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_errors = ARRAY_LEN (ip4_arp_error_strings),
  .error_strings = ip4_arp_error_strings,

  .n_next_nodes = IP4_ARP_N_NEXT,
  .next_nodes = {
    [IP4_ARP_NEXT_DROP] = "error-drop-transpose",
  },
};

typedef enum {
  IP4_REWRITE_NEXT_DROP,
} ip4_rewrite_next_t;

static uword
ip4_rewrite_slow_path (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       u32 buffer_index,
		       u32 adj_index)
{
  ip_adjacency_t * adj0;
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  u32 * to_next;
  vlib_buffer_t * p0;
  ip4_header_t * ip0;
  u32 pi0, next0, rw_len0, error_code0, adj_index0;

  adj_index0 = adj_index;
  adj0 = ip_get_adjacency (lm, adj_index0);
      
  ASSERT (adj0[0].n_adj == 1);

  pi0 = buffer_index;
  p0 = vlib_get_buffer (vm, pi0);

  rw_len0 = adj0[0].rewrite_header.data_bytes;

  ip0 = vlib_buffer_get_current (p0) + rw_len0;
  next0 = adj0[0].rewrite_header.next_index;
  error_code0 = ~0;
  
  if (ip0->ttl == 0 || ip0->ttl == 255)
    {
      error_code0 = IP4_ERROR_TIME_EXPIRED;

      /* FIXME send an ICMP. */
    }

  else if (p0->current_length > adj0[0].rewrite_header.max_packet_bytes)
    {
      /* MTU check failed. */
      error_code0 = IP4_ERROR_MTU_EXCEEDED;

      /* FIXME fragment packet. */
    }

  /* Now put the packet on the appropriate next frame. */
  to_next = vlib_set_next_frame (vm, node, next0);
  to_next[0] = pi0;
  if (error_code0 != ~0)
    {
      next0 = IP4_REWRITE_NEXT_DROP;
      to_next[1] = vlib_error_set (ip4_input_node.index, error_code0);
    }

  return 0;
}

static uword
ip4_rewrite (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  u32 * from = vlib_frame_vector_args (frame);
  u32 n_left_from, n_left_to_next, * to_next, next_index;

  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  ip_adjacency_t * adj0, * adj1;
	  ip_buffer_opaque_t * i0, * i1;
	  vlib_buffer_t * p0, * p1;
	  ip4_header_t * ip0, * ip1;
	  u32 pi0, rw_len0, len0, next0, checksum0, adj_index0;
	  u32 pi1, rw_len1, len1, next1, checksum1, adj_index1;
	  u8 is_slow_path;
      
	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->pre_data, 32, STORE);
	    CLIB_PREFETCH (p3->pre_data, 32, STORE);

	    CLIB_PREFETCH (p2->data, sizeof (ip0[0]), STORE);
	    CLIB_PREFETCH (p3->data, sizeof (ip0[0]), STORE);
	  }

	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];

	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  adj_index0 = i0->dst_adj_index;
	  adj_index1 = i1->dst_adj_index;

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  /* Decrement TTL & update checksum.
	     Works either endian, so no need for byte swap. */
	  {
	    i32 ttl0 = ip0->ttl, ttl1 = ip1->ttl;

	    /* Input node should have reject packets with ttl 0. */
	    ASSERT (ip0->ttl > 0);
	    ASSERT (ip1->ttl > 0);

	    checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);
	    checksum1 = ip1->checksum + clib_host_to_net_u16 (0x0100);

	    ip0->checksum = checksum0 + (checksum0 >= 0xffff);
	    ip1->checksum = checksum1 + (checksum1 >= 0xffff);

	    ttl0 -= 1;
	    ttl1 -= 1;

	    ip0->ttl = ttl0;
	    ip1->ttl = ttl1;

	    is_slow_path = ttl0 <= 0 || ttl1 <= 0;

	    /* Verify checksum. */
	    ASSERT (ip0->checksum == ip4_header_checksum (ip0));
	    ASSERT (ip1->checksum == ip4_header_checksum (ip1));
	  }

	  /* Rewrite packet header and updates lengths. */
	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);
      
	  /* Multi-path should go elsewhere. */
	  ASSERT (adj0[0].n_adj == 1);
	  ASSERT (adj1[0].n_adj == 1);

	  rw_len0 = adj0[0].rewrite_header.data_bytes;
	  rw_len1 = adj1[0].rewrite_header.data_bytes;

	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index0,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len0);
	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index1,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len1);

	  p0->current_data -= rw_len0;
	  p1->current_data -= rw_len1;

	  len0 = p0->current_length;
	  len1 = p1->current_length;

	  len0 += rw_len0;
	  len1 += rw_len1;

	  p0->current_length = len0;
	  p1->current_length = len1;

	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
	  p1->sw_if_index[VLIB_TX] = adj1[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;
	  next1 = adj1[0].rewrite_header.next_index;

	  /* Check MTU of outgoing interface. */
	  is_slow_path += ((len0 > adj0[0].rewrite_header.max_packet_bytes)
			   || (len1 > adj1[0].rewrite_header.max_packet_bytes));

	  is_slow_path += (next0 != next_index || next1 != next_index);

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_two_headers (adj0[0], adj1[0],
				    ip0, ip1,
				    sizeof (ethernet_header_t));
      
	  if (PREDICT_FALSE (is_slow_path))
	    {
	      to_next -= 2;
	      n_left_to_next += 2;

	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);

	      ip4_rewrite_slow_path (vm, node, from[-2], adj_index0);
	      ip4_rewrite_slow_path (vm, node, from[-1], adj_index1);

	      next_index = next0 == next1 ? next1 : next_index;

	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  ip_adjacency_t * adj0;
	  ip_buffer_opaque_t * i0;
	  vlib_buffer_t * p0;
	  ip4_header_t * ip0;
	  u32 pi0, rw_len0, len0;
	  u8 is_slow_path;
	  u32 adj_index0, next0, checksum0;
      
	  pi0 = to_next[0] = from[0];

	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->dst_adj_index;
	  adj0 = ip_get_adjacency (lm, adj_index0);
      
	  /* Multi-path should go elsewhere. */
	  ASSERT (adj0[0].n_adj == 1);

	  ip0 = vlib_buffer_get_current (p0);

	  /* Decrement TTL & update checksum. */
	  checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);
	  ip0->checksum = checksum0 + (checksum0 >= 0xffff);

	  /* Check TTL */
	  {
	    i32 ttl0 = ip0->ttl;

	    ASSERT (ip0->ttl > 0);

	    ttl0 -= 1;

	    ip0->ttl = ttl0;

	    is_slow_path = ttl0 <= 0;
	  }

	  ASSERT (ip0->checksum == ip4_header_checksum (ip0));

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_one_header (adj0[0], ip0, sizeof (ethernet_header_t));
      
	  /* Update packet buffer attributes/set output interface. */
	  rw_len0 = adj0[0].rewrite_header.data_bytes;

	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index0,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len0);

	  p0->current_data -= rw_len0;
	  len0 = p0->current_length += rw_len0;
	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;

	  /* Check MTU of outgoing interface. */
	  is_slow_path += len0 > adj0[0].rewrite_header.max_packet_bytes;

	  is_slow_path += next0 != next_index;

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  if (PREDICT_FALSE (is_slow_path))
	    {
	      to_next -= 1;
	      n_left_to_next += 1;

	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
	      ip4_rewrite_slow_path (vm, node, from[-1], adj_index0);
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Need to do trace after rewrites to pick up new packet data. */
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return frame->n_vectors;
}

VLIB_REGISTER_NODE (ip4_rewrite_node) = {
  .function = ip4_rewrite,
  .name = "ip4-rewrite",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [IP4_REWRITE_NEXT_DROP] = "error-drop",
  },
};

static uword
ip4_multipath (vlib_main_t * vm,
	       vlib_node_runtime_t * node,
	       vlib_frame_t * frame)
{
  u32 * buffers = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   buffers,
			   /* stride */ 1,
			   n_packets,
			   /* next */ 0,
			   ip4_input_node.index,
			   IP4_ERROR_ADJACENCY_DROP);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  return n_packets;
}

static VLIB_REGISTER_NODE (ip4_multipath_node) = {
  .function = ip4_multipath,
  .name = "ip4-multipath",
  .vector_size = sizeof (u32),

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "ip4-rewrite",
  },
};
