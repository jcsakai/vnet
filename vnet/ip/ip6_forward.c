/*
 * ip/ip6_forward.c: IP v6 forwarding
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
#include <vnet/ethernet/ethernet.h> /* for ethernet_header_t */
#include <vnet/vnet/l3_types.h>

/* This is really, really simple but stupid fib. */
u32
ip6_fib_lookup (ip6_main_t * im, u32 sw_if_index, ip6_address_t * dst)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index = vec_elt (im->fib_index_by_sw_if_index, sw_if_index);
  ip6_fib_t * fib = vec_elt_at_index (im->fibs, fib_index);
  ip6_fib_mhash_t * fm;
  ip6_address_t masked_dst;
  uword i, * p;

  vec_foreach (fm, fib->non_empty_dst_address_length_mhash)
    {
      ip6_address_t * mask = &im->fib_masks[fm->dst_address_length];
      for (i = 0; i < ARRAY_LEN (mask->data_u32); i++)
	masked_dst.data_u32[i] = clib_mem_unaligned (&dst->data_u32[i], u32) & mask->data_u32[i];

      p = mhash_get (&fm->adj_index_by_dst_address, &masked_dst);
      if (p)
	return p[0];
    }
  
  /* Nothing matches in table. */
  return lm->miss_adj_index;
}

static ip6_fib_t *
create_fib_with_table_id (ip6_main_t * im, u32 table_id)
{
  ip6_fib_t * fib;
  hash_set (im->fib_index_by_table_id, table_id, vec_len (im->fibs));
  vec_add2 (im->fibs, fib, 1);
  memset (fib->mhash_index_by_dst_address_length, ~0, sizeof (fib->mhash_index_by_dst_address_length));
  fib->table_id = table_id;
  fib->index = fib - im->fibs;
  return fib;
}

static ip6_fib_t *
find_fib_by_table_index_or_id (ip6_main_t * im, u32 table_index_or_id, u32 flags)
{
  uword * p, fib_index;

  fib_index = table_index_or_id;
  if (! (flags & IP6_ROUTE_FLAG_FIB_INDEX))
    {
      p = hash_get (im->fib_index_by_table_id, table_index_or_id);
      if (! p)
	return create_fib_with_table_id (im, table_index_or_id);
      fib_index = p[0];
    }
  return vec_elt_at_index (im->fibs, fib_index);
}

static void
ip6_fib_init_address_length (ip_lookup_main_t * lm, ip6_fib_t * fib, u32 address_length)
{
  ip6_fib_mhash_t * mh;
  uword max_index;

  ASSERT (lm->fib_result_n_bytes >= sizeof (uword));
  lm->fib_result_n_words = round_pow2 (lm->fib_result_n_bytes, sizeof (uword)) / sizeof (uword);

  vec_add2 (fib->non_empty_dst_address_length_mhash, mh, 1);
  mh->dst_address_length = address_length;
  fib->mhash_index_by_dst_address_length[address_length] = mh - fib->non_empty_dst_address_length_mhash;

  mhash_init (&mh->adj_index_by_dst_address, lm->fib_result_n_words * sizeof (uword),
	      sizeof (ip6_address_t));

  max_index = (mhash_value_bytes (&mh->adj_index_by_dst_address) / sizeof (fib->new_hash_values[0])) - 1;

  /* Initialize new/old hash value vectors. */
  vec_validate_init_empty (fib->new_hash_values, max_index, ~0);
  vec_validate_init_empty (fib->old_hash_values, max_index, ~0);

  /* Sort so that longest prefix lengths are first. */
  vec_sort (fib->non_empty_dst_address_length_mhash,
	    m1, m2,
	    (int) m1->dst_address_length - (int) m2->dst_address_length);

  /* Rebuild index. */
  memset (fib->mhash_index_by_dst_address_length, ~0, sizeof (fib->mhash_index_by_dst_address_length));
  vec_foreach (mh, fib->non_empty_dst_address_length_mhash)
    fib->mhash_index_by_dst_address_length[mh->dst_address_length] = mh - fib->non_empty_dst_address_length_mhash;
}

static void serialize_ip6_address (serialize_main_t * m, va_list * va)
{
  ip6_address_t * a = va_arg (*va, ip6_address_t *);
  int i;
  for (i = 0; i < ARRAY_LEN (a->data_u32); i++)
    serialize_integer (m, a->data_u32[i], sizeof (a->data_u32[i]));
}

static void unserialize_ip6_address (serialize_main_t * m, va_list * va)
{
  ip6_address_t * a = va_arg (*va, ip6_address_t *);
  int i;
  for (i = 0; i < ARRAY_LEN (a->data_u32); i++)
    unserialize_integer (m, &a->data_u32[i], sizeof (a->data_u32[i]));
}

static void serialize_ip6_add_del_route_msg (serialize_main_t * m, va_list * va)
{
  ip6_add_del_route_args_t * a = va_arg (*va, ip6_add_del_route_args_t *);
    
  serialize_integer (m, a->table_index_or_table_id, sizeof (a->table_index_or_table_id));
  serialize_integer (m, a->flags, sizeof (a->flags));
  serialize (m, serialize_ip6_address, &a->dst_address);
  serialize_integer (m, a->dst_address_length, sizeof (a->dst_address_length));
  serialize_integer (m, a->adj_index, sizeof (a->adj_index));
  serialize_integer (m, a->n_add_adj, sizeof (a->n_add_adj));
  if (a->n_add_adj > 0)
    serialize (m, serialize_vec_ip_adjacency, a->add_adj, a->n_add_adj);
}

/* Serialized adjacencies for arp/rewrite do not send graph next_index
   since graph hookup is not guaranteed to be the same for both sides
   of serialize/unserialize. */
static void
unserialize_fixup_ip6_rewrite_adjacencies (vlib_main_t * vm,
					   ip_adjacency_t * adj,
					   u32 n_adj)
{
  u32 i, ni, sw_if_index, is_arp;
  vlib_hw_interface_t * hw;

  for (i = 0; i < n_adj; i++)
    {
      switch (adj[i].lookup_next_index)
	{
	case IP_LOOKUP_NEXT_REWRITE:
	case IP_LOOKUP_NEXT_ARP:
	  is_arp = adj[i].lookup_next_index == IP_LOOKUP_NEXT_ARP;
	  sw_if_index = adj[i].rewrite_header.sw_if_index;
	  hw = vlib_get_sup_hw_interface (vm, sw_if_index);
	  ni = is_arp ? ip6_arp_node.index : ip6_rewrite_node.index;
	  adj[i].rewrite_header.node_index = ni;
	  adj[i].rewrite_header.next_index = vlib_node_add_next (vm, ni, hw->output_node_index);
	  break;

	default:
	  break;
	}
    }
}

static void unserialize_ip6_add_del_route_msg (serialize_main_t * m, va_list * va)
{
  ip6_main_t * i4m = &ip6_main;
  ip6_add_del_route_args_t a;
    
  unserialize_integer (m, &a.table_index_or_table_id, sizeof (a.table_index_or_table_id));
  unserialize_integer (m, &a.flags, sizeof (a.flags));
  unserialize (m, unserialize_ip6_address, &a.dst_address);
  unserialize_integer (m, &a.dst_address_length, sizeof (a.dst_address_length));
  unserialize_integer (m, &a.adj_index, sizeof (a.adj_index));
  unserialize_integer (m, &a.n_add_adj, sizeof (a.n_add_adj));
  a.add_adj = 0;
  if (a.n_add_adj > 0)
    {
      vec_resize (a.add_adj, a.n_add_adj);
      unserialize (m, unserialize_vec_ip_adjacency, a.add_adj, a.n_add_adj);
      unserialize_fixup_ip6_rewrite_adjacencies (&vlib_global_main, a.add_adj, a.n_add_adj);
    }

  /* Prevent re-re-distribution. */
  a.flags |= IP6_ROUTE_FLAG_NO_REDISTRIBUTE;

  ip6_add_del_route (i4m, &a);

  vec_free (a.add_adj);
}

static MC_SERIALIZE_MSG (ip6_add_del_route_msg) = {
  .name = "vnet_ip6_add_del_route",
  .serialize = serialize_ip6_add_del_route_msg,
  .unserialize = unserialize_ip6_add_del_route_msg,
};

static void
ip6_fib_set_adj_index (ip6_main_t * im,
		       ip6_fib_t * fib,
		       u32 flags,
		       ip6_address_t * dst_address,
		       u32 dst_address_length,
		       u32 adj_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_fib_mhash_t * mh;

  memset (fib->old_hash_values, ~0, vec_bytes (fib->old_hash_values));
  memset (fib->new_hash_values, ~0, vec_bytes (fib->new_hash_values));
  fib->new_hash_values[0] = adj_index;

  /* Make sure adj index is valid. */
  if (DEBUG > 0)
    (void) ip_get_adjacency (lm, adj_index);

  mh = ip6_fib_get_dst_address_length (fib, dst_address_length);

  mhash_set_mem (&mh->adj_index_by_dst_address,
		 dst_address,
		 fib->new_hash_values,
		 fib->old_hash_values);

  if (vec_len (im->add_del_route_callbacks) > 0)
    {
      ip6_add_del_route_callback_t * cb;
      ip6_address_t d;
      uword * p;

      memcpy (&d, dst_address, sizeof (d));
      vec_foreach (cb, im->add_del_route_callbacks)
	cb->function (im, cb->function_opaque,
		      fib, flags,
		      &d, dst_address_length,
		      fib->old_hash_values,
		      fib->new_hash_values);

      p = mhash_get (&mh->adj_index_by_dst_address, dst_address);
      memcpy (p, fib->new_hash_values, vec_bytes (fib->new_hash_values));
    }
}

void ip6_add_del_route (ip6_main_t * im, ip6_add_del_route_args_t * a)
{
  vlib_main_t * vm = &vlib_global_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_fib_t * fib;
  ip6_fib_mhash_t * mh;
  ip6_address_t dst_address;
  u32 dst_address_length, adj_index;
  uword is_del;
  ip6_add_del_route_callback_t * cb;

  if (vm->mc_main && ! (a->flags & IP6_ROUTE_FLAG_NO_REDISTRIBUTE))
    {
      mc_serialize (vm->mc_main, &ip6_add_del_route_msg, a);
      return;
    }

  /* Either create new adjacency or use given one depending on arguments. */
  if (a->n_add_adj > 0)
    ip_add_adjacency (lm, a->add_adj, a->n_add_adj, &adj_index);
  else
    adj_index = a->adj_index;

  dst_address = a->dst_address;
  dst_address_length = a->dst_address_length;
  fib = find_fib_by_table_index_or_id (im, a->table_index_or_table_id, a->flags);

  ASSERT (dst_address_length < ARRAY_LEN (im->fib_masks));
  ip6_address_mask (&dst_address, &im->fib_masks[dst_address_length]);

  if (fib->mhash_index_by_dst_address_length[dst_address_length] >= vec_len (fib->non_empty_dst_address_length_mhash))
    ip6_fib_init_address_length (lm, fib, dst_address_length);

  mh = ip6_fib_get_dst_address_length (fib, dst_address_length);

  is_del = (a->flags & IP6_ROUTE_FLAG_DEL) != 0;

  if (is_del)
    {
      fib->old_hash_values[0] = ~0;
      mhash_unset (&mh->adj_index_by_dst_address, &dst_address, fib->old_hash_values);

      if (vec_len (im->add_del_route_callbacks) > 0
	  && fib->old_hash_values[0] != ~0) /* make sure destination was found in hash */
	{
	  fib->new_hash_values[0] = ~0;
	  vec_foreach (cb, im->add_del_route_callbacks)
	    cb->function (im, cb->function_opaque,
			  fib, a->flags,
			  &a->dst_address, dst_address_length,
			  fib->old_hash_values,
			  fib->new_hash_values);
	}
    }
  else
    ip6_fib_set_adj_index (im, fib, a->flags, &dst_address, dst_address_length,
			   adj_index);

  /* Delete old adjacency index if present and changed. */
  {
    u32 old_adj_index = fib->old_hash_values[0];
    if (! (a->flags & IP6_ROUTE_FLAG_KEEP_OLD_ADJACENCY)
	&& old_adj_index != ~0
	&& old_adj_index != adj_index)
      ip_del_adjacency (lm, old_adj_index);
  }
}

static void serialize_ip6_add_del_route_next_hop_msg (serialize_main_t * m, va_list * va)
{
  u32 flags = va_arg (*va, u32);
  ip6_address_t * dst_address = va_arg (*va, ip6_address_t *);
  u32 dst_address_length = va_arg (*va, u32);
  ip6_address_t * next_hop_address = va_arg (*va, ip6_address_t *);
  u32 next_hop_sw_if_index = va_arg (*va, u32);
  u32 next_hop_weight = va_arg (*va, u32);

  serialize_integer (m, flags, sizeof (flags));
  serialize (m, serialize_ip6_address, dst_address);
  serialize_integer (m, dst_address_length, sizeof (dst_address_length));
  serialize (m, serialize_ip6_address, next_hop_address);
  serialize_integer (m, next_hop_sw_if_index, sizeof (next_hop_sw_if_index));
  serialize_integer (m, next_hop_weight, sizeof (next_hop_weight));
}

static void unserialize_ip6_add_del_route_next_hop_msg (serialize_main_t * m, va_list * va)
{
  ip6_main_t * im = &ip6_main;
  u32 flags, dst_address_length, next_hop_sw_if_index, next_hop_weight;
  ip6_address_t dst_address, next_hop_address;

  unserialize_integer (m, &flags, sizeof (flags));
  unserialize (m, unserialize_ip6_address, &dst_address);
  unserialize_integer (m, &dst_address_length, sizeof (dst_address_length));
  unserialize (m, unserialize_ip6_address, &next_hop_address);
  unserialize_integer (m, &next_hop_sw_if_index, sizeof (next_hop_sw_if_index));
  unserialize_integer (m, &next_hop_weight, sizeof (next_hop_weight));

  ip6_add_del_route_next_hop
    (im,
     flags | IP6_ROUTE_FLAG_NO_REDISTRIBUTE,
     &dst_address,
     dst_address_length,
     &next_hop_address,
     next_hop_sw_if_index,
     next_hop_weight);
}

static MC_SERIALIZE_MSG (ip6_add_del_route_next_hop_msg) = {
  .name = "vnet_ip6_add_del_route_next_hop",
  .serialize = serialize_ip6_add_del_route_next_hop_msg,
  .unserialize = unserialize_ip6_add_del_route_next_hop_msg,
};

void
ip6_add_del_route_next_hop (ip6_main_t * im,
			    u32 flags,
			    ip6_address_t * dst_address,
			    u32 dst_address_length,
			    ip6_address_t * next_hop,
			    u32 next_hop_sw_if_index,
			    u32 next_hop_weight)
{
  vlib_main_t * vm = &vlib_global_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index;
  ip6_fib_t * fib;
  ip6_fib_mhash_t * nh_hash, * dst_hash;
  ip6_address_t masked_dst_address;
  u32 old_mp_adj_index, new_mp_adj_index;
  u32 dst_adj_index, nh_adj_index;
  uword * dst_result;
  uword * nh_result;
  ip_adjacency_t * dst_adj;
  ip_multipath_adjacency_t * old_mp, * new_mp;
  int is_del = (flags & IP6_ROUTE_FLAG_DEL) != 0;
  int is_interface_next_hop;
  clib_error_t * error = 0;

  if (vm->mc_main && ! (flags & IP6_ROUTE_FLAG_NO_REDISTRIBUTE))
    {
      mc_serialize (vm->mc_main, &ip6_add_del_route_next_hop_msg,
		    flags,
		    dst_address, dst_address_length,
		    next_hop, next_hop_sw_if_index, next_hop_weight);
      return;
    }

  fib_index = vec_elt (im->fib_index_by_sw_if_index, next_hop_sw_if_index);
  fib = vec_elt_at_index (im->fibs, fib_index);

  /* Lookup next hop to be added or deleted. */
  is_interface_next_hop = ip6_address_is_zero (next_hop);
  if (is_interface_next_hop)
    {
      nh_result = hash_get (im->interface_adj_index_by_sw_if_index, next_hop_sw_if_index);
      if (nh_result)
	nh_adj_index = *nh_result;
      else
	{
	  ip_adjacency_t * adj;
	  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
				  &nh_adj_index);
	  ip6_adjacency_set_interface_route (vm, adj, next_hop_sw_if_index);
	  hash_set (im->interface_adj_index_by_sw_if_index, next_hop_sw_if_index, nh_adj_index);
	}
    }
  else
    {
      nh_hash = ip6_fib_get_dst_address_length (fib, 128);
      nh_result = mhash_get (&nh_hash->adj_index_by_dst_address, next_hop);

      /* Next hop must be known. */
      if (! nh_result)
	{
	  error = clib_error_return (0, "next-hop %U/128 not in FIB",
				     format_ip6_address, next_hop);
	  goto done;
	}
      nh_adj_index = *nh_result;
    }

  ASSERT (dst_address_length < ARRAY_LEN (im->fib_masks));
  masked_dst_address = dst_address[0];
  ip6_address_mask (&masked_dst_address, &im->fib_masks[dst_address_length]);

  dst_hash = ip6_fib_get_dst_address_length (fib, dst_address_length);
  dst_result = mhash_get (&dst_hash->adj_index_by_dst_address, &masked_dst_address);
  if (dst_result)
    {
      dst_adj_index = dst_result[0];
      dst_adj = ip_get_adjacency (lm, dst_adj_index);
    }
  else
    {
      /* For deletes destination must be known. */
      if (is_del)
	{
	  error = clib_error_return (0, "unknown destination %U/%d",
				     format_ip6_address, dst_address,
				     dst_address_length);
	  goto done;
	}

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
    {
      error = clib_error_return (0, "requested deleting next-hop %U not found in multi-path",
				 format_ip6_address, next_hop);
      goto done;
    }
  
  old_mp = new_mp = 0;
  if (old_mp_adj_index != ~0)
    old_mp = vec_elt_at_index (lm->multipath_adjacencies, old_mp_adj_index);
  if (new_mp_adj_index != ~0)
    new_mp = vec_elt_at_index (lm->multipath_adjacencies, new_mp_adj_index);

  if (old_mp != new_mp)
    {
      ip6_add_del_route_args_t a;
      a.table_index_or_table_id = fib_index;
      a.flags = ((is_del ? IP6_ROUTE_FLAG_DEL : IP6_ROUTE_FLAG_ADD)
		 | IP6_ROUTE_FLAG_FIB_INDEX
		 | IP6_ROUTE_FLAG_KEEP_OLD_ADJACENCY
		 | (flags & IP6_ROUTE_FLAG_NO_REDISTRIBUTE));
      a.dst_address = dst_address[0];
      a.dst_address_length = dst_address_length;
      a.adj_index = new_mp ? new_mp->adj_index : dst_adj_index;
      a.add_adj = 0;
      a.n_add_adj = 0;

      ip6_add_del_route (im, &a);
    }

 done:
  if (error)
    clib_error_report (error);
}

void *
ip6_get_route (ip6_main_t * im,
	       u32 table_index_or_table_id,
	       u32 flags,
	       u8 * address,
	       u32 address_length)
{
  ip6_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip6_fib_mhash_t * hash;
  ip6_address_t masked_dst_address;
  uword * p;

  ASSERT (address_length < ARRAY_LEN (im->fib_masks));
  memcpy (&masked_dst_address, address, sizeof (masked_dst_address));
  ip6_address_mask (&masked_dst_address, &im->fib_masks[address_length]);

  hash = ip6_fib_get_dst_address_length (fib, address_length);
  p = mhash_get (&hash->adj_index_by_dst_address, &masked_dst_address);
  return (void *) p;
}

void
ip6_foreach_matching_route (ip6_main_t * im,
			    u32 table_index_or_table_id,
			    u32 flags,
			    u8 * address,
			    u32 address_length,
			    ip6_address_t ** results,
			    u8 ** result_lengths)
{
  ip6_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip6_fib_mhash_t * mh;
  ip6_address_t dst_address, * k;
  uword * v;
  
  memcpy (&dst_address, address, sizeof (dst_address));

  if (*results)
    _vec_len (*results) = 0;
  if (*result_lengths)
    _vec_len (*result_lengths) = 0;

  vec_foreach (mh, fib->non_empty_dst_address_length_mhash)
    {
      if (mh->dst_address_length < address_length)
	continue;

      mhash_foreach (k, v, &mh->adj_index_by_dst_address, ({
	if (ip6_destination_matches_route (im, k, &dst_address, mh->dst_address_length))
	  {
	    vec_add1 (*results, k[0]);
	    vec_add1 (*result_lengths, mh->dst_address_length);
	  }
      }));
    }
}

void ip6_maybe_remap_adjacencies (ip6_main_t * im,
				  u32 table_index_or_table_id,
				  u32 flags)
{
  ip6_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip6_fib_mhash_t * mh;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_address_t * k;
  ip6_add_del_route_callback_t * cb;
  uword * v;
  static ip6_address_t * to_delete;

  if (lm->n_adjacency_remaps == 0)
    return;

  vec_foreach (mh, fib->non_empty_dst_address_length_mhash)
    {
      if (to_delete)
	_vec_len (to_delete) = 0;

      mhash_foreach (k, v, &mh->adj_index_by_dst_address, ({
	u32 adj_index = v[0];
	u32 m = vec_elt (lm->adjacency_remap_table, adj_index);

	if (m)
	  {
	    /* Reset mapping table. */
	    lm->adjacency_remap_table[adj_index] = 0;

	    /* New adjacency points to nothing: so delete prefix. */
	    if (m == ~0)
	      vec_add1 (to_delete, k[0]);
	    else
	      {
		/* Remap to new adjacency. */
		memcpy (fib->old_hash_values, v, vec_bytes (fib->old_hash_values));

		/* Set new adjacency value. */
		fib->new_hash_values[0] = v[0] = m - 1;

		vec_foreach (cb, im->add_del_route_callbacks)
		  cb->function (im, cb->function_opaque,
				fib, flags | IP6_ROUTE_FLAG_ADD,
				k, mh->dst_address_length,
				fib->old_hash_values,
				fib->new_hash_values);
	      }
	  }
      }));

      memset (fib->new_hash_values, ~0, vec_bytes (fib->new_hash_values));
      vec_foreach (k, to_delete)
	{
	  mhash_unset (&mh->adj_index_by_dst_address, k, fib->old_hash_values);
	  vec_foreach (cb, im->add_del_route_callbacks)
	    cb->function (im, cb->function_opaque,
			  fib, flags | IP6_ROUTE_FLAG_DEL,
			  k, mh->dst_address_length,
			  fib->old_hash_values,
			  fib->new_hash_values);
	}
    }

  /* All remaps have been performed. */
  lm->n_adjacency_remaps = 0;
}

void ip6_delete_matching_routes (ip6_main_t * im,
				 u32 table_index_or_table_id,
				 u32 flags,
				 u8 * address,
				 u32 address_length)
{
  static ip6_address_t * matching_addresses;
  static u8 * matching_address_lengths;
  u32 l, i;
  ip6_add_del_route_args_t a;

  a.flags = IP6_ROUTE_FLAG_DEL | IP6_ROUTE_FLAG_NO_REDISTRIBUTE | flags;
  a.table_index_or_table_id = table_index_or_table_id;
  a.adj_index = ~0;
  a.add_adj = 0;
  a.n_add_adj = 0;

  for (l = address_length + 1; l <= 128; )
    {
      ip6_foreach_matching_route (im, table_index_or_table_id, flags,
				  address,
				  l,
				  &matching_addresses,
				  &matching_address_lengths);
      for (i = 0; i < vec_len (matching_addresses); i++)
	{
	  a.dst_address = matching_addresses[i];
	  a.dst_address_length = matching_address_lengths[i];
	  ip6_add_del_route (im, &a);
	}
    }

  ip6_maybe_remap_adjacencies (im, table_index_or_table_id, flags);
}

/* Compute flow hash.  We'll use it to select which Sponge to use for this
   flow.  And other things. */
always_inline u32
ip6_compute_flow_hash (ip6_header_t * ip, u32 flow_hash_seed)
{
    tcp_header_t * tcp = (void *) (ip + 1);
    u32 a, b, c;
    uword is_tcp_udp = (ip->protocol == IP_PROTOCOL_TCP
			|| ip->protocol == IP_PROTOCOL_UDP);

    a = is_tcp_udp ? tcp->ports.src_and_dst : 0;
    a ^= ip->protocol ^ flow_hash_seed;
    b = ip->src_address.data_u32[0];
    c = ip->src_address.data_u32[1];

    hash_v3_mix32 (a, b, c);

    a ^= ip->src_address.data_u32[2];
    b ^= ip->src_address.data_u32[3];
    c ^= ip->dst_address.data_u32[0];

    hash_v3_mix32 (a, b, c);

    a ^= ip->dst_address.data_u32[1];
    b ^= ip->dst_address.data_u32[2];
    c ^= ip->dst_address.data_u32[3];

    hash_v3_finalize32 (a, b, c);

    return c;
}

static uword
ip6_lookup (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * frame)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
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
	  ip6_header_t * ip0, * ip1;
	  ip_adjacency_t * adj0, * adj1;

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

	  adj_index0 = ip6_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address);
	  adj_index1 = ip6_fib_lookup (im, p1->sw_if_index[VLIB_RX], &ip1->dst_address);

	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);

	  next0 = adj0->lookup_next_index;
	  next1 = adj1->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  i0->flow_hash = ip6_compute_flow_hash (ip0, im->flow_hash_seed);
	  i1->flow_hash = ip6_compute_flow_hash (ip1, im->flow_hash_seed);

	  ASSERT (adj0->n_adj > 0);
	  ASSERT (adj1->n_adj > 0);
	  ASSERT (is_pow2 (adj0->n_adj));
	  ASSERT (is_pow2 (adj1->n_adj));
	  adj_index0 += (i0->flow_hash & (adj0->n_adj - 1));
	  adj_index1 += (i1->flow_hash & (adj1->n_adj - 1));

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
	  ip6_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  u32 pi0, adj_index0;
	  ip_lookup_next_t next0;
	  ip_adjacency_t * adj0;

	  pi0 = from[0];
	  to_next[0] = pi0;

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);

	  adj_index0 = ip6_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address);

	  adj0 = ip_get_adjacency (lm, adj_index0);

	  next0 = adj0->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);

	  i0->flow_hash = ip6_compute_flow_hash (ip0, im->flow_hash_seed);

	  ASSERT (adj0->n_adj > 0);
	  ASSERT (is_pow2 (adj0->n_adj));
	  adj_index0 += (i0->flow_hash & (adj0->n_adj - 1));

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

void ip6_adjacency_set_interface_route (vlib_main_t * vm, ip_adjacency_t * adj,
					u32 sw_if_index)
{
  /* vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index); FIXME */
  ip_lookup_next_t n;
  u32 node_index;

  /* FIXME */
  if (0/*is_ethernet_interface (hw->hw_if_index)*/)
    {
      n = IP_LOOKUP_NEXT_ARP;
      node_index = ip6_arp_node.index;
    }
  else
    {
      n = IP_LOOKUP_NEXT_REWRITE;
      node_index = ip6_rewrite_node.index;
    }

  adj->lookup_next_index = n;
  vnet_rewrite_for_sw_interface
    (vm,
     VNET_L3_PACKET_TYPE_IP6,
     sw_if_index,
     node_index,
     &adj->rewrite_header,
     n == IP_LOOKUP_NEXT_REWRITE ? sizeof (adj->rewrite_data) : 0);
}

static void
ip6_add_interface_routes (vlib_main_t * vm, u32 sw_if_index,
			  ip6_main_t * im, u32 fib_index,
			  ip6_address_t * address,
			  u32 address_length)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_adjacency_t * adj;
  ip6_add_del_route_args_t a;

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  a.table_index_or_table_id = fib_index;
  a.flags = (IP6_ROUTE_FLAG_ADD
	     | IP6_ROUTE_FLAG_FIB_INDEX
	     | IP6_ROUTE_FLAG_NO_REDISTRIBUTE);
  a.dst_address = address[0];
  a.dst_address_length = address_length;
  a.n_add_adj = 0;
  a.add_adj = 0;

  if (address_length < 128)
    {
      adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			      &a.adj_index);
      ip6_adjacency_set_interface_route (vm, adj, sw_if_index);
      ip6_add_del_route (im, &a);
    }

  /* Add e.g. ::1/128 as local to this host. */
  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &a.adj_index);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;
  a.dst_address_length = 128;
  ip6_add_del_route (im, &a);
}

static void
ip6_del_interface_routes (ip6_main_t * im, u32 fib_index,
			  ip6_address_t * address, u32 address_length)
{
  ip6_add_del_route_args_t a;

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  a.table_index_or_table_id = fib_index;
  a.flags = (IP6_ROUTE_FLAG_DEL
	     | IP6_ROUTE_FLAG_FIB_INDEX
	     | IP6_ROUTE_FLAG_NO_REDISTRIBUTE);
  a.dst_address = address[0];
  a.dst_address_length = address_length;
  a.adj_index = ~0;
  a.n_add_adj = 0;
  a.add_adj = 0;

  ASSERT (ip6_interface_address_is_valid (address));

  if (address_length < 128)
    ip6_add_del_route (im, &a);

  a.dst_address_length = 128;
  ip6_add_del_route (im, &a);

  ip6_delete_matching_routes (im,
			      fib_index,
			      IP6_ROUTE_FLAG_FIB_INDEX,
			      address->data,
			      address_length);
}

typedef struct {
    u32 sw_if_index;
    ip6_address_t address;
    u32 length;
} ip6_interface_address_t;

static void serialize_vec_ip6_set_interface_address (serialize_main_t * m, va_list * va)
{
    ip6_interface_address_t * a = va_arg (*va, ip6_interface_address_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        serialize_integer (m, a[i].sw_if_index, sizeof (a[i].sw_if_index));
        serialize (m, serialize_ip6_address, &a[i].address);
        serialize_integer (m, a[i].length, sizeof (a[i].length));
    }
}

static void unserialize_vec_ip6_set_interface_address (serialize_main_t * m, va_list * va)
{
    ip6_interface_address_t * a = va_arg (*va, ip6_interface_address_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        unserialize_integer (m, &a[i].sw_if_index, sizeof (a[i].sw_if_index));
        unserialize (m, unserialize_ip6_address, &a[i].address);
        unserialize_integer (m, &a[i].length, sizeof (a[i].length));
    }
}

static void serialize_ip6_set_interface_address_msg (serialize_main_t * m, va_list * va)
{
  ip6_interface_address_t * a = va_arg (*va, ip6_interface_address_t *);
  serialize (m, serialize_vec_ip6_set_interface_address, a, 1);
}

static void
ip6_set_interface_address_internal (vlib_main_t * vm,
				    u32 sw_if_index,
				    ip6_address_t * new_address,
				    u32 new_length,
				    u32 redistribute,
				    u32 insert_routes);

static void unserialize_ip6_set_interface_address_msg (serialize_main_t * m, va_list * va)
{
  mc_main_t * mcm = va_arg (*va, mc_main_t *);
  vlib_main_t * vm = mcm->vlib_main;
  ip6_interface_address_t a;
  unserialize (m, unserialize_vec_ip6_set_interface_address, &a, 1);
  ip6_set_interface_address_internal (vm, a.sw_if_index, &a.address, a.length,
				      /* redistribute */ 0,
				      /* insert_routes */ 1);
}

static MC_SERIALIZE_MSG (ip6_set_interface_address_msg) = {
  .name = "vnet_ip6_set_interface_address",
  .serialize = serialize_ip6_set_interface_address_msg,
  .unserialize = unserialize_ip6_set_interface_address_msg,
};

static void
ip6_set_interface_address_internal (vlib_main_t * vm,
				    u32 sw_if_index,
				    ip6_address_t * new_address,
				    u32 new_length,
				    u32 redistribute,
				    u32 insert_routes)
{
  ip6_main_t * im = &ip6_main;
  ip6_address_t old_address;
  uword old_length;

  ASSERT (sw_if_index < vec_len (im->ip6_address_by_sw_if_index));
  ASSERT (sw_if_index < vec_len (im->ip6_address_length_by_sw_if_index));

  old_address = im->ip6_address_by_sw_if_index[sw_if_index];
  old_length = im->ip6_address_length_by_sw_if_index[sw_if_index];

  if (new_address->data_u32 == old_address.data_u32
      && old_length == new_length)
    return;

  if (vm->mc_main && redistribute)
    {
      ip6_interface_address_t a;
      a.sw_if_index = sw_if_index;
      a.address = new_address[0];
      a.length = new_length;
      mc_serialize (vm->mc_main, &ip6_set_interface_address_msg, &a);
      return;
    }

  im->ip6_address_by_sw_if_index[sw_if_index] = new_address[0];
  im->ip6_address_length_by_sw_if_index[sw_if_index] = new_length;

  if (vlib_sw_interface_is_admin_up (vm, sw_if_index) && insert_routes)
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];

      if (ip6_interface_address_is_valid (&old_address))
	ip6_del_interface_routes (im, fib_index, &old_address, old_length);

      if (ip6_interface_address_is_valid (new_address))
	ip6_add_interface_routes (vm, sw_if_index,
				  im, fib_index, new_address, new_length);
    }

  {
    ip6_set_interface_address_callback_t * cb;
    vec_foreach (cb, im->set_interface_address_callbacks)
      cb->function (im, cb->function_opaque, sw_if_index,
		    new_address, new_length);
  }
}

void
ip6_set_interface_address (vlib_main_t * vm, u32 sw_if_index,
			   ip6_address_t * new_address, u32 new_length)
{
  ip6_set_interface_address_internal (vm, sw_if_index, new_address, new_length,
				      /* redistribute */ 1,
				      /* insert_routes */ 1);
}

static void serialize_vec_ip6_fib (serialize_main_t * m, va_list * va)
{
  ip6_fib_t * fibs = va_arg (*va, ip6_fib_t *);
  u32 n = va_arg (*va, u32);
  u32 i;

  for (i = 0; i < n; i++)
    {
      ip6_fib_t * f = fibs + i;
      ip6_fib_mhash_t * mh;
      ip6_address_t * dst;
      uword * v;

      serialize_integer (m, f->table_id, sizeof (f->table_id));
      serialize_integer (m, vec_len (f->non_empty_dst_address_length_mhash), sizeof (u32));

      vec_foreach (mh, f->non_empty_dst_address_length_mhash)
	{
	  u32 n_elts;

	  n_elts = mhash_elts (&mh->adj_index_by_dst_address);

	  serialize_integer (m, mh->dst_address_length, sizeof (mh->dst_address_length));
	  serialize_integer (m, n_elts, sizeof (n_elts));
	  
	  mhash_foreach (dst, v, &mh->adj_index_by_dst_address, ({
	    serialize (m, serialize_ip6_address, dst);
	    serialize_integer (m, v[0], sizeof (u32));
	  }));
	}
    }
}

static void unserialize_vec_ip6_fib (serialize_main_t * m, va_list * va)
{
  ip6_fib_t * fibs = va_arg (*va, ip6_fib_t *);
  u32 n = va_arg (*va, u32);
  u32 i, l, flags;

  flags = IP6_ROUTE_FLAG_ADD | IP6_ROUTE_FLAG_NO_REDISTRIBUTE;

  for (i = 0; i < n; i++)
    {
      ip6_fib_t * f = fibs + i;
      ip6_fib_mhash_t * mh;

      memset (f, 0, sizeof (f[0]));
      unserialize_integer (m, &f->table_id, sizeof (f->table_id));
      f->index = i;

      unserialize_integer (m, &l, sizeof (l));

      vec_resize (f->non_empty_dst_address_length_mhash, l);
      vec_foreach (mh, f->non_empty_dst_address_length_mhash)
	{
	  ip6_address_t dst_address;
	  u32 n_elts, adj_index;

	  unserialize_integer (m, &l, sizeof (l));

	  mh->dst_address_length = l;
	  f->mhash_index_by_dst_address_length[l] = mh - f->non_empty_dst_address_length_mhash;
	  unserialize_integer (m, &n_elts, sizeof (u32));

	  while (n_elts > 0)
	    {
	      unserialize (m, unserialize_ip6_address, &dst_address);
	      unserialize_integer (m, &adj_index, sizeof (adj_index));
	      ip6_fib_set_adj_index (&ip6_main, f, flags, &dst_address, l, adj_index);
	      n_elts--;
	    }
	}
    }
}

void serialize_vnet_ip6_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_interface_main_t * vim = &vm->interface_main;
  vlib_sw_interface_t * si;
  ip6_main_t * i4m = &ip6_main;
  ip6_interface_address_t * as = 0, * a;

  /* Download adjacency tables & multipath stuff. */
  serialize (m, serialize_ip_lookup_main, &i4m->lookup_main);

  /* FIBs. */
  vec_serialize (m, i4m->fibs, serialize_vec_ip6_fib);

  /* FIB interface config. */
  vec_serialize (m, i4m->fib_index_by_sw_if_index, serialize_vec_32);

  /* Interface ip6 addresses. */
  pool_foreach (si, vim->sw_interfaces, ({
    u32 sw_if_index = si->sw_if_index;
    ip6_address_t * x = vec_elt_at_index (i4m->ip6_address_by_sw_if_index, sw_if_index);
    if (ip6_interface_address_is_valid (x))
      {
	vec_add2 (as, a, 1);
	a->address = x[0];
	a->length = vec_elt (i4m->ip6_address_length_by_sw_if_index, sw_if_index);
	a->sw_if_index = sw_if_index;
      }
  }));
  vec_serialize (m, as, serialize_vec_ip6_set_interface_address);
  vec_free (as);
}

void unserialize_vnet_ip6_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  ip6_main_t * i4m = &ip6_main;
  ip6_interface_address_t * as = 0, * a;

  unserialize (m, unserialize_ip_lookup_main, &i4m->lookup_main);

  {
    ip_adjacency_t * adj;
    u32 n_adj;
    heap_foreach (adj, n_adj, i4m->lookup_main.adjacency_heap, ({
	  unserialize_fixup_ip6_rewrite_adjacencies (vm, adj, n_adj);
    }));
  }

  vec_unserialize (m, &i4m->fibs, unserialize_vec_ip6_fib);

  /* Re-construct table id -> table index hash. */
  {
    ip6_fib_t * f;
    vec_foreach (f, i4m->fibs)
      hash_set (i4m->fib_index_by_table_id, f->table_id, f->index);
  }

  vec_unserialize (m, &i4m->fib_index_by_sw_if_index, unserialize_vec_32);

  vec_unserialize (m, &as, unserialize_vec_ip6_set_interface_address);
  vec_foreach (a, as) {
    ip6_set_interface_address_internal
      (vm, a->sw_if_index, &a->address, a->length,
       /* redistribute */ 0,
       /* insert_routes */ 0);
  }
  vec_free (as);
}

static clib_error_t *
ip6_sw_interface_admin_up_down (vlib_main_t * vm,
				u32 sw_if_index,
				u32 flags)
{
  ip6_main_t * im = &ip6_main;
  ip6_address_t * a;
  uword is_admin_up;

  /* Fill in lookup tables with default table (0). */
  vec_validate (im->fib_index_by_sw_if_index, sw_if_index);

  /* Validate interface address/length table. */
  {
    ip6_address_t null;

    ip6_interface_address_set_invalid (&null);
    ASSERT (! ip6_interface_address_is_valid (&null));
    vec_validate_init_empty (im->ip6_address_by_sw_if_index, sw_if_index, null);
    vec_validate_init_empty (im->ip6_address_length_by_sw_if_index, sw_if_index, ~0);
  }

  is_admin_up = (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) != 0;
  a = ip6_get_interface_address (im, sw_if_index);
  if (ip6_interface_address_is_valid (a))
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];
      u32 length = ip6_get_interface_address_length (im, sw_if_index);

      if (is_admin_up)
	ip6_add_interface_routes (vm, sw_if_index,
				  im, fib_index,
				  a, length);
      else
	ip6_del_interface_routes (im, fib_index,
				  a, length);
    }

  return 0;
}

static VLIB_REGISTER_NODE (ip6_lookup_node) = {
  .function = ip6_lookup,
  .name = "ip6-lookup",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP_LOOKUP_N_NEXT,
  .next_nodes = {
    [IP_LOOKUP_NEXT_MISS] = "ip6-miss",
    [IP_LOOKUP_NEXT_DROP] = "ip6-drop",
    [IP_LOOKUP_NEXT_PUNT] = "ip6-punt",
    [IP_LOOKUP_NEXT_LOCAL] = "ip6-local",
    [IP_LOOKUP_NEXT_ARP] = "ip6-arp",
    [IP_LOOKUP_NEXT_REWRITE] = "ip6-rewrite",
  },

  .sw_interface_admin_up_down_function = ip6_sw_interface_admin_up_down,
};

/* Global IP6 main. */
ip6_main_t ip6_main;

static clib_error_t *
ip6_lookup_init (vlib_main_t * vm)
{
  ip6_main_t * im = &ip6_main;
  uword i;

  for (i = 0; i < ARRAY_LEN (im->fib_masks); i++)
    {
      u32 j, i0, i1;

      i0 = i / 32;
      i1 = i % 32;

      for (j = 0; j < i0; j++)
	im->fib_masks[i].data_u32[j] = ~0;

      if (i1)
	im->fib_masks[i].data_u32[i0] = clib_host_to_net_u32 (pow2_mask (i1) << (32 - i1));
    }

  /* Create FIB with index 0 and table id of 0. */
  find_fib_by_table_index_or_id (im, /* table id */ 0, IP6_ROUTE_FLAG_TABLE_ID);

  ip_lookup_init (&im->lookup_main, ip6_lookup_node.index);

#if 0
  /* FIXME ip6 neighbor solicitation. */
  {
    ethernet_and_arp_header_t h[2];

    memset (&h, 0, sizeof (h));

    /* Send to broadcast address ffff.ffff.ffff */
    memset (h[0].ethernet.dst_address, ~0, sizeof (h[0].ethernet.dst_address));
    memset (h[1].ethernet.dst_address, ~0, sizeof (h[1].ethernet.dst_address));

    /* Set target ethernet address to all zeros. */
    memset (h[0].arp.ip6_over_ethernet[1].ethernet, 0, sizeof (h[0].arp.ip6_over_ethernet[1].ethernet));
    memset (h[1].arp.ip6_over_ethernet[1].ethernet, ~0, sizeof (h[1].arp.ip6_over_ethernet[1].ethernet));

#define _16(f,v) h[0].f = clib_host_to_net_u16 (v); h[1].f = ~0
#define _8(f,v) h[0].f = v; h[1].f = ~0
    _16 (ethernet.type, ETHERNET_TYPE_ARP);
    _16 (arp.l2_type, ETHERNET_ARP_HARDWARE_TYPE_ethernet);
    _16 (arp.l3_type, ETHERNET_TYPE_IP6);
    _8 (arp.n_l2_address_bytes, 6);
    _8 (arp.n_l3_address_bytes, 4);
    _16 (arp.opcode, ETHERNET_ARP_OPCODE_request);
#undef _16
#undef _8

    vlib_packet_template_init (vm,
			       &im->ip6_arp_request_packet_template,
			       /* data */ &h[0],
			       /* mask */ &h[1],
			       sizeof (h[0]),
			       /* alloc chunk size */ 8);
  }
#endif

  return 0;
}

VLIB_INIT_FUNCTION (ip6_lookup_init);

typedef struct {
  /* Adjacency taken. */
  u32 adj_index;

  /* Packet data, possibly *after* rewrite. */
  u8 packet_data[64 - 1*sizeof(u32)];
} ip6_forward_next_trace_t;

static u8 * format_ip6_forward_next_trace (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  ip6_forward_next_trace_t * t = va_arg (*args, ip6_forward_next_trace_t *);
  ip6_main_t * im = &ip6_main;
  ip_adjacency_t * adj;
  uword indent = format_get_indent (s);

  adj = ip_get_adjacency (&im->lookup_main, t->adj_index);
  s = format (s, "adjacency: %U",
	      format_ip_adjacency,
	      vm, &im->lookup_main, t->adj_index);
  switch (adj->lookup_next_index)
    {
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

/* Common trace function for all ip6-forward next nodes. */
static void
ip6_forward_next_trace (vlib_main_t * vm,
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
      ip6_forward_next_trace_t * t0, * t1;

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
      ip6_forward_next_trace_t * t0;

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
ip6_drop_or_punt (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * frame,
		  ip6_error_t error_code)
{
  u32 * buffers = vlib_frame_vector_args (frame);
  uword n_packets = frame->n_vectors;

  vlib_error_drop_buffers (vm, node,
			   buffers,
			   /* stride */ 1,
			   n_packets,
			   /* next */ 0,
			   ip6_input_node.index,
			   error_code);

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip6_forward_next_trace (vm, node, frame);

  return n_packets;
}

static uword
ip6_drop (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip6_drop_or_punt (vm, node, frame, IP6_ERROR_ADJACENCY_DROP); }

static uword
ip6_punt (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip6_drop_or_punt (vm, node, frame, IP6_ERROR_ADJACENCY_PUNT); }

static uword
ip6_miss (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{ return ip6_drop_or_punt (vm, node, frame, IP6_ERROR_DST_LOOKUP_MISS); }

static VLIB_REGISTER_NODE (ip6_drop_node) = {
  .function = ip6_drop,
  .name = "ip6-drop",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

static VLIB_REGISTER_NODE (ip6_punt_node) = {
  .function = ip6_punt,
  .name = "ip6-punt",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-punt",
  },
};

static VLIB_REGISTER_NODE (ip6_miss_node) = {
  .function = ip6_miss,
  .name = "ip6-miss",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

/* Compute TCP/UDP checksum in software. */
u32 ip6_tcp_udp_checksum (vlib_buffer_t * p0)
{
  ip6_header_t * ip0;
  ip_csum_t sum0;
  u16 sum16, payload_length_host_byte_order;

  ip0 = vlib_buffer_get_current (p0);

  /* FIXME handle other headers. */
  ASSERT (ip0->protocol == IP_PROTOCOL_TCP
	  || ip0->protocol == IP_PROTOCOL_UDP);

  /* Initialize checksum with ip6 header. */
  sum0 = clib_mem_unaligned (&ip0->src_address.data_u64[0], u64);
  sum0 = ip_csum_with_carry (sum0,
			     clib_mem_unaligned (&ip0->src_address.data_u64[1], u64));
  sum0 = ip_csum_with_carry (sum0,
			     clib_mem_unaligned (&ip0->dst_address.data_u64[0], u64));
  sum0 = ip_csum_with_carry (sum0,
			     clib_mem_unaligned (&ip0->dst_address.data_u64[1], u64));

  sum0 = ip_csum_with_carry (sum0, ip0->payload_length);
  sum0 = ip_csum_with_carry (sum0, ip0->protocol);
  payload_length_host_byte_order = clib_net_to_host_u16 (ip0->payload_length);

  if (ip0->protocol == IP_PROTOCOL_TCP)
    {
      sum0 = ip_incremental_checksum (sum0, (void *) (ip0 + 1), payload_length_host_byte_order);
      sum16 = ~ ip_csum_fold (sum0);
    }
  else
    {
      udp_header_t * udp0 = (void *) (ip0 + 1);

      if (udp0->checksum == 0)
	{
	  sum16 = 0;
	  goto done;
	}

      sum0 = ip_incremental_checksum (sum0, udp0, payload_length_host_byte_order);

      sum16 = ~ ip_csum_fold (sum0);
    }

 done:
  p0->flags |= IP_BUFFER_TCP_UDP_CHECKSUM_COMPUTED;
  if (sum16 == 0)
    p0->flags |= IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT;

  return p0->flags;
}

static uword
ip6_local (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_local_next_t next_index;
  u32 * from, * to_next, n_left_from, n_left_to_next;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip6_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip6_forward_next_trace (vm, node, frame);

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip_local_buffer_opaque_t * i0, * i1;
	  ip6_header_t * ip0, * ip1;
	  udp_header_t * udp0, * udp1;
	  u32 pi0, ip_len0, udp_len0, flags0, adj_index0, next0;
	  u32 pi1, ip_len1, udp_len1, flags1, adj_index1, next1;
	  i32 len_diff0, len_diff1;
	  u8 error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
	  u8 error1, is_udp1, is_tcp_udp1, good_tcp_udp1, proto1;
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

	  good_tcp_udp0 = (flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
	  good_tcp_udp1 = (flags1 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  udp0 = ip6_next_header (ip0);
	  udp1 = ip6_next_header (ip1);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->payload_length);
	  ip_len1 = clib_net_to_host_u16 (ip1->payload_length);
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
		  if (is_tcp_udp0
		      && ! (flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags0 = ip6_tcp_udp_checksum (p0);
		  good_tcp_udp0 =
		    (flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
		}
	      if (is_tcp_udp1)
		{
		  if (is_tcp_udp1
		      && ! (flags1 & IP_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		    flags1 = ip6_tcp_udp_checksum (p1);
		  good_tcp_udp1 =
		    (flags1 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;
		}
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;
	  good_tcp_udp1 &= len_diff1 >= 0;

	  error0 = error1 = IP6_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP6_ERROR_UDP_LENGTH : error0;
	  error1 = len_diff1 < 0 ? IP6_ERROR_UDP_LENGTH : error1;

	  ASSERT (IP6_ERROR_TCP_CHECKSUM + 1 == IP6_ERROR_UDP_CHECKSUM);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? IP6_ERROR_TCP_CHECKSUM + is_udp0
		    : error0);
	  error1 = (is_tcp_udp1 && ! good_tcp_udp1
		    ? IP6_ERROR_TCP_CHECKSUM + is_udp1
		    : error1);

	  if (error0 == IP6_ERROR_UNKNOWN_PROTOCOL
	      && i0->non_local.src_adj_index == ~0)
	    {
	      i0->non_local.src_adj_index =
		ip6_fib_lookup (im, p0->sw_if_index[VLIB_RX],
				&ip0->src_address);
	      error0 = (lm->miss_adj_index == i0->non_local.src_adj_index
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }
	  if (error1 == IP6_ERROR_UNKNOWN_PROTOCOL
	      && i1->non_local.src_adj_index == ~0)
	    {
	      i1->non_local.src_adj_index =
		ip6_fib_lookup (im, p1->sw_if_index[VLIB_RX],
				&ip1->src_address);
	      error1 = (lm->miss_adj_index == i1->non_local.src_adj_index
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error1);
	    }

	  next0 = lm->local_next_by_ip_protocol[proto0];
	  next1 = lm->local_next_by_ip_protocol[proto1];

	  next0 = error0 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;
	  next1 = error1 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next1;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

	  enqueue_code = (next0 != next_index) + 2*(next1 != next_index);

	  if (PREDICT_FALSE (enqueue_code != 0))
	    {
	      switch (enqueue_code)
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
		  /* A B B or A B C */
		  to_next -= 2;
		  n_left_to_next += 2;
		  vlib_set_next_frame_buffer (vm, node, next0, pi0);
		  vlib_set_next_frame_buffer (vm, node, next1, pi1);
		  if (next0 == next1)
		    {
		      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
		      next_index = next1;
		      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
		    }
		  break;
		}
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  ip_local_buffer_opaque_t * i0;
	  udp_header_t * udp0;
	  u32 pi0, next0, ip_len0, udp_len0, flags0, adj_index0;
	  i32 len_diff0;
	  u8 error0, is_udp0, is_tcp_udp0, good_tcp_udp0, proto0;
      
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

	  flags0 = p0->flags;

	  good_tcp_udp0 = (flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;

	  udp0 = ip6_next_header (ip0);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_tcp_udp0 |= (is_udp0 && udp0->checksum == 0);

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->payload_length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);

	  len_diff0 = ip_len0 - udp_len0;

	  len_diff0 = is_udp0 ? len_diff0 : 0;

	  if (PREDICT_FALSE (! (is_tcp_udp0 & good_tcp_udp0)))
	    {
	      if (is_tcp_udp0
		  && ! (flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_COMPUTED))
		flags0 = ip6_tcp_udp_checksum (p0);
	      good_tcp_udp0 =
		(flags0 & IP_BUFFER_TCP_UDP_CHECKSUM_CORRECT) != 0;
	      good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;

	  error0 = IP6_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP6_ERROR_UDP_LENGTH : error0;

	  ASSERT (IP6_ERROR_TCP_CHECKSUM + 1 == IP6_ERROR_UDP_CHECKSUM);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? IP6_ERROR_TCP_CHECKSUM + is_udp0
		    : error0);

	  if (error0 == IP6_ERROR_UNKNOWN_PROTOCOL
	      && i0->non_local.src_adj_index == ~0)
	    {
	      i0->non_local.src_adj_index =
		ip6_fib_lookup (im, p0->sw_if_index[VLIB_RX],
				&ip0->src_address);
	      error0 = (lm->miss_adj_index == i0->non_local.src_adj_index
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }

	  next0 = lm->local_next_by_ip_protocol[proto0];

	  next0 = error0 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;

	  p0->error = error_node->errors[error0];

	  if (PREDICT_FALSE (next0 != next_index))
	    {
	      n_left_to_next += 1;
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next);

	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	      to_next[0] = pi0;
	      to_next += 1;
	      n_left_to_next -= 1;
	    }
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

static VLIB_REGISTER_NODE (ip6_local_node) = {
  .function = ip6_local,
  .name = "ip6-local",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = IP_LOCAL_N_NEXT,
  .next_nodes = {
    [IP_LOCAL_NEXT_DROP] = "error-drop",
    [IP_LOCAL_NEXT_PUNT] = "error-punt",
    [IP_LOCAL_NEXT_TCP_LOOKUP] = "tcp4-lookup",
    [IP_LOCAL_NEXT_UDP_LOOKUP] = "udp4-lookup",
    [IP_LOCAL_NEXT_ICMP] = "ip6-icmp-input",
  },
};

typedef enum {
  IP6_ARP_NEXT_DROP,
  IP6_ARP_N_NEXT,
} ip6_arp_next_t;

typedef enum {
  IP6_ARP_ERROR_DROP,
  IP6_ARP_ERROR_REQUEST_SENT,
} ip6_arp_error_t;

static uword
ip6_arp (vlib_main_t * vm,
	 vlib_node_runtime_t * node,
	 vlib_frame_t * frame)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 * from, * to_next_drop;
  uword n_left_from, n_left_to_next_drop, next_index;
  static f64 time_last_seed_change = -1e100;
  static u32 hash_seeds[3];
  static uword hash_bitmap[256 / BITS (uword)]; 
  f64 time_now;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip6_forward_next_trace (vm, node, frame);

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
  if (next_index == IP6_ARP_NEXT_DROP)
    next_index = IP6_ARP_N_NEXT; /* point to first interface */

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, IP6_ARP_NEXT_DROP,
			   to_next_drop, n_left_to_next_drop);

      while (n_left_from > 0 && n_left_to_next_drop > 0)
	{
	  vlib_buffer_t * p0;
	  ip_buffer_opaque_t * i0;
	  ip6_header_t * ip0;
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

	  a0 ^= sw_if_index0;
	  b0 ^= ip0->dst_address.data_u32[0];
	  c0 ^= ip0->dst_address.data_u32[1];

	  hash_v3_mix32 (a0, b0, c0);

	  b0 ^= ip0->dst_address.data_u32[2];
	  c0 ^= ip0->dst_address.data_u32[3];

	  hash_v3_finalize32 (a0, b0, c0);

	  c0 &= BITS (hash_bitmap) - 1;
	  c0 = c0 / BITS (uword);
	  m0 = (uword) 1 << (c0 % BITS (uword));

	  bm0 = hash_bitmap[c0];
	  drop0 = (bm0 & m0) != 0;
	  next0 = drop0 ? IP6_ARP_NEXT_DROP : adj0->rewrite_header.next_index;

	  /* Mark it as seen. */
	  hash_bitmap[c0] = bm0 | m0;

	  from += 1;
	  n_left_from -= 1;
	  to_next_drop[0] = pi0;
	  to_next_drop += 1;
	  n_left_to_next_drop -= 1;

	  p0->error = node->errors[drop0 ? IP6_ARP_ERROR_DROP : IP6_ARP_ERROR_REQUEST_SENT];

	  if (drop0)
	    continue;

#if 0
	  {
	    u32 bi0, * to_next_request;
	    ethernet_and_arp_header_t * h0;
	    vlib_sw_interface_t * swif0;
	    ethernet_interface_t * eif0;
	    ip6_address_t * swif_ip0;
	    u8 * eth_addr0, dummy[6] = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, };

	    h0 = vlib_packet_template_get_packet (vm, &im->ip6_arp_request_packet_template, &bi0);

	    swif0 = vlib_get_sup_sw_interface (vm, sw_if_index0);
	    ASSERT (swif0->type == VLIB_SW_INTERFACE_TYPE_HARDWARE);
	    eif0 = ethernet_get_interface (&ethernet_main, swif0->hw_if_index);
	    eth_addr0 = eif0 ? eif0->address : dummy;
	    memcpy (h0->ethernet.src_address, eth_addr0, sizeof (h0->ethernet.src_address));
	    memcpy (h0->arp.ip6_over_ethernet[0].ethernet, eth_addr0, sizeof (h0->arp.ip6_over_ethernet[0].ethernet));

	    swif_ip0 = ip6_get_interface_address (im, sw_if_index0);
	    h0->arp.ip6_over_ethernet[0].ip6.data_u32 = swif_ip0->data_u32;

	    /* Copy in destination address we are requesting. */
	    h0->arp.ip6_over_ethernet[1].ip6.data_u32 = ip0->dst_address.data_u32;

	    vlib_buffer_copy_trace_flag (vm, p0, bi0);

	    to_next_request = vlib_set_next_frame (vm, node, next0);
	    to_next_request[0] = bi0;
	  }
#else
	  ASSERT (0);
#endif
	}

      vlib_put_next_frame (vm, node, IP6_ARP_NEXT_DROP, n_left_to_next_drop);
    }

  return frame->n_vectors;
}

static char * ip6_arp_error_strings[] = {
  [IP6_ARP_ERROR_DROP] = "address overflow drops",
  [IP6_ARP_ERROR_REQUEST_SENT] = "ARP requests sent",
};

VLIB_REGISTER_NODE (ip6_arp_node) = {
  .function = ip6_arp,
  .name = "ip6-arp",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_errors = ARRAY_LEN (ip6_arp_error_strings),
  .error_strings = ip6_arp_error_strings,

  .n_next_nodes = IP6_ARP_N_NEXT,
  .next_nodes = {
    [IP6_ARP_NEXT_DROP] = "error-drop",
  },
};

typedef enum {
  IP6_REWRITE_NEXT_DROP,
} ip6_rewrite_next_t;

static uword
ip6_rewrite_slow_path (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       u32 buffer_index,
		       u32 adj_index)
{
  ip_adjacency_t * adj0;
  ip_lookup_main_t * lm = &ip6_main.lookup_main;
  vlib_buffer_t * p0;
  ip6_header_t * ip0;
  u32 pi0, next0, rw_len0, error_code0, adj_index0;

  adj_index0 = adj_index;
  adj0 = ip_get_adjacency (lm, adj_index0);
      
  pi0 = buffer_index;
  p0 = vlib_get_buffer (vm, pi0);

  rw_len0 = adj0[0].rewrite_header.data_bytes;

  ip0 = vlib_buffer_get_current (p0) + rw_len0;
  next0 = adj0[0].rewrite_header.next_index;
  error_code0 = ~0;
  
  if (ip0->ttl == 0 || ip0->ttl == 255)
    {
      error_code0 = IP6_ERROR_TIME_EXPIRED;

      /* FIXME send an ICMP. */
    }

  else if (p0->current_length > adj0[0].rewrite_header.max_packet_bytes)
    {
      /* MTU check failed. */
      error_code0 = IP6_ERROR_MTU_EXCEEDED;

      /* FIXME fragment packet. */
    }

  p0->error = vlib_error_set (ip6_input_node.index, error_code0);

  next0 = error_code0 != ~0 ? IP6_REWRITE_NEXT_DROP : next0;

  /* Now put the packet on the appropriate next frame. */
  vlib_set_next_frame_buffer (vm, node, next0, pi0);

  return 0;
}

static uword
ip6_rewrite (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  ip_lookup_main_t * lm = &ip6_main.lookup_main;
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
	  ip6_header_t * ip0, * ip1;
	  u32 pi0, rw_len0, len0, next0, adj_index0;
	  u32 pi1, rw_len1, len1, next1, adj_index1;
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

	    ttl0 -= 1;
	    ttl1 -= 1;

	    ip0->ttl = ttl0;
	    ip1->ttl = ttl1;

	    is_slow_path = ttl0 <= 0 || ttl1 <= 0;
	  }

	  /* Rewrite packet header and updates lengths. */
	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);
      
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

	      ip6_rewrite_slow_path (vm, node, from[-2], adj_index0);
	      ip6_rewrite_slow_path (vm, node, from[-1], adj_index1);

	      next_index = next0 == next1 ? next1 : next_index;

	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  ip_adjacency_t * adj0;
	  ip_buffer_opaque_t * i0;
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  u32 pi0, rw_len0, len0;
	  u8 is_slow_path;
	  u32 adj_index0, next0;
      
	  pi0 = to_next[0] = from[0];

	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->dst_adj_index;
	  adj0 = ip_get_adjacency (lm, adj_index0);
      
	  ip0 = vlib_buffer_get_current (p0);

	  /* Check TTL */
	  {
	    i32 ttl0 = ip0->ttl;

	    ASSERT (ip0->ttl > 0);

	    ttl0 -= 1;

	    ip0->ttl = ttl0;

	    is_slow_path = ttl0 <= 0;
	  }

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
	      ip6_rewrite_slow_path (vm, node, from[-1], adj_index0);
	      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);
	    }
	}
  
      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Need to do trace after rewrites to pick up new packet data. */
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip6_forward_next_trace (vm, node, frame);

  return frame->n_vectors;
}

VLIB_REGISTER_NODE (ip6_rewrite_node) = {
  .function = ip6_rewrite,
  .name = "ip6-rewrite",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [IP6_REWRITE_NEXT_DROP] = "error-drop",
  },
};


