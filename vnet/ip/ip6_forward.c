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
#include <vnet/srp/srp.h>	/* for srp_hw_interface_class */
#include <vnet/vnet/l3_types.h>

always_inline u32
ip6_fib_lookup_buffer_flags (ip6_main_t * im, u32 sw_if_index, ip6_address_t * dst, uword b_flags)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index;
  ip6_fib_t * fib;
  ip6_fib_mhash_t * fm;
  ip6_address_t masked_dst;
  uword i, * p;

  fib_index = vec_elt (im->fib_index_by_sw_if_index, sw_if_index);

  fib_index = (b_flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : fib_index;

  fib = vec_elt_at_index (im->fibs, fib_index);

  vec_foreach (fm, fib->non_empty_dst_address_length_mhash)
    {
      ip6_address_t * mask = &im->fib_masks[fm->dst_address_length];
      for (i = 0; i < ARRAY_LEN (mask->as_uword); i++)
	masked_dst.as_uword[i] = clib_mem_unaligned (&dst->as_uword[i], uword) & mask->as_uword[i];

      p = mhash_get (&fm->adj_index_by_dst_address, &masked_dst);
      if (p)
	return p[0];
    }
  
  /* Nothing matches in table. */
  return lm->miss_adj_index;
}

u32
ip6_fib_lookup_buffer (ip6_main_t * im, u32 sw_if_index, ip6_address_t * dst, vlib_buffer_t * b)
{ return ip6_fib_lookup_buffer_flags (im, sw_if_index, dst, b->flags); }

u32
ip6_fib_lookup (ip6_main_t * im, u32 sw_if_index, ip6_address_t * dst)
{ return ip6_fib_lookup_buffer_flags (im, sw_if_index, dst, /* buffer flags */ 0); }

static void
ip6_fib_init (ip6_main_t * im, u32 fib_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_add_del_route_args_t a;
  ip_adjacency_t * adj;

  /* Add ff02::1:ff00:0/104 via local route for all tables.
     This is required for neighbor discovery to work. */
  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &a.adj_index);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;
  adj->if_address_index = ~0;

  a.table_index_or_table_id = fib_index;
  a.flags = (IP6_ROUTE_FLAG_ADD
	     | IP6_ROUTE_FLAG_FIB_INDEX
	     | IP6_ROUTE_FLAG_KEEP_OLD_ADJACENCY
	     | IP6_ROUTE_FLAG_NO_REDISTRIBUTE);
  ip6_set_solicited_node_multicast_address (&a.dst_address, 0);
  a.dst_address_length = 104;
  a.add_adj = 0;
  a.n_add_adj = 0;

  ip6_add_del_route (im, &a);
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
  ip6_fib_init (im, fib->index);
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
	    (int) m2->dst_address_length - (int) m1->dst_address_length);

  /* Rebuild index. */
  memset (fib->mhash_index_by_dst_address_length, ~0, sizeof (fib->mhash_index_by_dst_address_length));
  vec_foreach (mh, fib->non_empty_dst_address_length_mhash)
    fib->mhash_index_by_dst_address_length[mh->dst_address_length] = mh - fib->non_empty_dst_address_length_mhash;
}

static void serialize_ip6_address (serialize_main_t * m, va_list * va)
{
  ip6_address_t * a = va_arg (*va, ip6_address_t *);
  u8 * p = serialize_get (m, sizeof (a->as_u8));
  memcpy (p, a->as_u8, sizeof (a->as_u8));
}

static void unserialize_ip6_address (serialize_main_t * m, va_list * va)
{
  ip6_address_t * a = va_arg (*va, ip6_address_t *);
  u8 * p = unserialize_get (m, sizeof (a->as_u8));
  memcpy (a->as_u8, p, sizeof (a->as_u8));
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
	  ni = is_arp ? ip6_discover_neighbor_node.index : ip6_rewrite_node.index;
	  adj[i].rewrite_header.node_index = ni;
	  adj[i].rewrite_header.next_index = vlib_node_add_next (vm, ni, hw->output_node_index);
	  if (is_arp)
	    vnet_rewrite_for_sw_interface
	      (vm,
	       VNET_L3_PACKET_TYPE_ARP,
	       sw_if_index,
	       ni,
	       VNET_REWRITE_FOR_SW_INTERFACE_ADDRESS_BROADCAST,
	       &adj[i].rewrite_header,
	       sizeof (adj->rewrite_data));
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
  if (CLIB_DEBUG > 0)
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
	if ((flags & cb->required_flags) == cb->required_flags)
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
    {
      ip_add_adjacency (lm, a->add_adj, a->n_add_adj, &adj_index);
      ip_call_add_del_adjacency_callbacks (lm, adj_index, /* is_del */ 0);
    }
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
	    if ((a->flags & cb->required_flags) == cb->required_flags)
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
      nh_result = hash_get (im->interface_route_adj_index_by_sw_if_index, next_hop_sw_if_index);
      if (nh_result)
	nh_adj_index = *nh_result;
      else
	{
	  ip_adjacency_t * adj;
	  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
				  &nh_adj_index);
	  ip6_adjacency_set_interface_route (vm, adj, next_hop_sw_if_index, ~0);
	  ip_call_add_del_adjacency_callbacks (lm, next_hop_sw_if_index, /* is_del */ 0);
	  hash_set (im->interface_route_adj_index_by_sw_if_index, next_hop_sw_if_index, nh_adj_index);
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

  /* Ignore adds of X/128 with next hop of X. */
  if (! is_del
      && dst_address_length == 128
      && ip6_address_is_equal (dst_address, next_hop))
    {
      error = clib_error_return (0, "prefix matches next hop %U/%d",
                                 format_ip6_address, dst_address,
                                 dst_address_length);
      goto done;
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
			    ip6_address_t * dst_address,
			    u32 address_length,
			    ip6_address_t ** results,
			    u8 ** result_lengths)
{
  ip6_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip6_fib_mhash_t * mh;
  ip6_address_t * k;
  CLIB_UNUSED (uword * v);
  
  if (*results)
    _vec_len (*results) = 0;
  if (*result_lengths)
    _vec_len (*result_lengths) = 0;

  vec_foreach (mh, fib->non_empty_dst_address_length_mhash)
    {
      if (mh->dst_address_length < address_length)
	continue;

      mhash_foreach (k, v, &mh->adj_index_by_dst_address, ({
	if (ip6_destination_matches_route (im, k, dst_address, mh->dst_address_length))
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
		  if ((flags & cb->required_flags) == cb->required_flags)
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
	    if ((flags & cb->required_flags) == cb->required_flags)
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
				 ip6_address_t * address,
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

  for (l = address_length + 1; l <= 128; l++)
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
    b = ip->src_address.as_u32[0];
    c = ip->src_address.as_u32[1];

    hash_v3_mix32 (a, b, c);

    a ^= ip->src_address.as_u32[2];
    b ^= ip->src_address.as_u32[3];
    c ^= ip->dst_address.as_u32[0];

    hash_v3_mix32 (a, b, c);

    a ^= ip->dst_address.as_u32[1];
    b ^= ip->dst_address.as_u32[2];
    c ^= ip->dst_address.as_u32[3];

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

	  adj_index0 = ip6_fib_lookup_buffer (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);
	  adj_index1 = ip6_fib_lookup_buffer (im, p1->sw_if_index[VLIB_RX], &ip1->dst_address, p1);

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

	  vlib_increment_combined_counter (cm, adj_index0, 1,
					   vlib_buffer_length_in_chain (vm, p0));
	  vlib_increment_combined_counter (cm, adj_index1, 1,
					   vlib_buffer_length_in_chain (vm, p1));

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

	  adj_index0 = ip6_fib_lookup_buffer (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);

	  adj0 = ip_get_adjacency (lm, adj_index0);

	  next0 = adj0->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);

	  i0->flow_hash = ip6_compute_flow_hash (ip0, im->flow_hash_seed);

	  ASSERT (adj0->n_adj > 0);
	  ASSERT (is_pow2 (adj0->n_adj));
	  adj_index0 += (i0->flow_hash & (adj0->n_adj - 1));

	  i0->dst_adj_index = adj_index0;

	  vlib_increment_combined_counter (cm, adj_index0, 1,
					   vlib_buffer_length_in_chain (vm, p0));

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

void ip6_adjacency_set_interface_route (vlib_main_t * vm,
					ip_adjacency_t * adj,
					u32 sw_if_index,
					u32 if_address_index)
{
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  ip_lookup_next_t n;
  u32 node_index;

  if (hw->hw_class_index == ethernet_hw_interface_class.index
      || hw->hw_class_index == srp_hw_interface_class.index)
    {
      n = IP_LOOKUP_NEXT_ARP;
      node_index = ip6_discover_neighbor_node.index;
      adj->if_address_index = if_address_index;
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
    VNET_REWRITE_FOR_SW_INTERFACE_ADDRESS_BROADCAST,
    &adj->rewrite_header,
    sizeof (adj->rewrite_data));
}

static void
ip6_add_interface_routes (vlib_main_t * vm, u32 sw_if_index,
			  ip6_main_t * im, u32 fib_index,
			  ip_interface_address_t * a)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_adjacency_t * adj;
  ip6_address_t * address = ip_interface_address_get_address (lm, a);
  ip6_add_del_route_args_t x;
  vlib_hw_interface_t * hw_if = vlib_get_sup_hw_interface (vm, sw_if_index);

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  x.table_index_or_table_id = fib_index;
  x.flags = (IP6_ROUTE_FLAG_ADD
	     | IP6_ROUTE_FLAG_FIB_INDEX
	     | IP6_ROUTE_FLAG_NO_REDISTRIBUTE);
  x.dst_address = address[0];
  x.dst_address_length = a->address_length;
  x.n_add_adj = 0;
  x.add_adj = 0;

  a->neighbor_probe_adj_index = ~0;
  if (a->address_length < 128)
    {
      adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			      &x.adj_index);
      ip6_adjacency_set_interface_route (vm, adj, sw_if_index, a - lm->if_address_pool);
      ip_call_add_del_adjacency_callbacks (lm, x.adj_index, /* is_del */ 0);
      ip6_add_del_route (im, &x);
      a->neighbor_probe_adj_index = x.adj_index;
    }

  /* Add e.g. ::1/128 as local to this host. */
  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &x.adj_index);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;
  adj->if_address_index = a - lm->if_address_pool;
  adj->rewrite_header.sw_if_index = sw_if_index;
  adj->rewrite_header.max_l3_packet_bytes = hw_if->max_l3_packet_bytes[VLIB_RX];
  ip_call_add_del_adjacency_callbacks (lm, x.adj_index, /* is_del */ 0);
  x.dst_address_length = 128;
  ip6_add_del_route (im, &x);
}

static void
ip6_del_interface_routes (ip6_main_t * im, u32 fib_index,
			  ip6_address_t * address, u32 address_length)
{
  ip6_add_del_route_args_t x;

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  x.table_index_or_table_id = fib_index;
  x.flags = (IP6_ROUTE_FLAG_DEL
	     | IP6_ROUTE_FLAG_FIB_INDEX
	     | IP6_ROUTE_FLAG_NO_REDISTRIBUTE);
  x.dst_address = address[0];
  x.dst_address_length = address_length;
  x.adj_index = ~0;
  x.n_add_adj = 0;
  x.add_adj = 0;

  if (address_length < 128)
    ip6_add_del_route (im, &x);

  x.dst_address_length = 128;
  ip6_add_del_route (im, &x);

  ip6_delete_matching_routes (im,
			      fib_index,
			      IP6_ROUTE_FLAG_FIB_INDEX,
			      address,
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

static clib_error_t *
ip6_add_del_interface_address_internal (vlib_main_t * vm,
					u32 sw_if_index,
					ip6_address_t * new_address,
					u32 new_length,
					u32 redistribute,
					u32 insert_routes,
					u32 is_del);

static void unserialize_ip6_set_interface_address_msg (serialize_main_t * m, va_list * va)
{
  mc_main_t * mcm = va_arg (*va, mc_main_t *);
  vlib_main_t * vm = mcm->vlib_main;
  ip6_interface_address_t a;
  unserialize (m, unserialize_vec_ip6_set_interface_address, &a, 1);
  ip6_add_del_interface_address_internal
    (vm, a.sw_if_index, &a.address, a.length,
     /* redistribute */ 0,
     /* insert_routes */ 1,
     /* is_del */ 0);
}

static MC_SERIALIZE_MSG (ip6_set_interface_address_msg) = {
  .name = "vnet_ip6_set_interface_address",
  .serialize = serialize_ip6_set_interface_address_msg,
  .unserialize = unserialize_ip6_set_interface_address_msg,
};

static clib_error_t *
ip6_add_del_interface_address_internal (vlib_main_t * vm,
					u32 sw_if_index,
					ip6_address_t * address,
					u32 address_length,
					u32 redistribute,
					u32 insert_routes,
					u32 is_del)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  clib_error_t * error;
  u32 if_address_index;

  {
    uword elts_before = pool_elts (lm->if_address_pool);

    error = ip_interface_address_add_del
      (lm,
       sw_if_index,
       address,
       address_length,
       is_del,
       &if_address_index);
    if (error)
      goto done;

    /* Pool did not grow: add duplicate address. */
    if (elts_before == pool_elts (lm->if_address_pool))
      goto done;
  }

  if (vm->mc_main && redistribute)
    {
      ip6_interface_address_t a;
      a.sw_if_index = sw_if_index;
      a.address = address[0];
      a.length = address_length;
      mc_serialize (vm->mc_main, &ip6_set_interface_address_msg, &a);
      goto done;
    }

  if (vlib_sw_interface_is_admin_up (vm, sw_if_index) && insert_routes)
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];

      if (is_del)
	ip6_del_interface_routes (im, fib_index, address, address_length);

      else
	ip6_add_interface_routes (vm, sw_if_index,
				  im, fib_index,
				  pool_elt_at_index (lm->if_address_pool, if_address_index));
    }

  {
    ip6_add_del_interface_address_callback_t * cb;
    vec_foreach (cb, im->add_del_interface_address_callbacks)
      cb->function (im, cb->function_opaque, sw_if_index,
		    address, address_length,
		    if_address_index,
		    is_del);
  }

 done:
  return error;
}

clib_error_t *
ip6_add_del_interface_address (vlib_main_t * vm, u32 sw_if_index,
			       ip6_address_t * address, u32 address_length,
			       u32 is_del)
{
  return ip6_add_del_interface_address_internal
    (vm, sw_if_index, address, address_length,
     /* redistribute */ 1,
     /* insert_routes */ 1,
     is_del);
}

static void serialize_ip6_fib (serialize_main_t * m, va_list * va)
{
  ip6_fib_t * f = va_arg (*va, ip6_fib_t *);
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

static void unserialize_ip6_fib (serialize_main_t * m, va_list * va)
{
  ip6_add_del_route_args_t a;
  u32 i, n_dst_address_lengths;

  a.flags = (IP6_ROUTE_FLAG_ADD
             | IP6_ROUTE_FLAG_NO_REDISTRIBUTE
             | IP6_ROUTE_FLAG_TABLE_ID);
  a.n_add_adj = 0;
  a.add_adj = 0;

  unserialize_integer (m, &a.table_index_or_table_id,
                       sizeof (a.table_index_or_table_id));

  unserialize_integer (m, &n_dst_address_lengths, sizeof (n_dst_address_lengths));
  for (i = 0; i < n_dst_address_lengths; i++)
    {
      u32 n_elts;

      unserialize_integer (m, &a.dst_address_length,
                           sizeof (a.dst_address_length));
      unserialize_integer (m, &n_elts, sizeof (u32));
      while (n_elts > 0)
        {
          unserialize (m, unserialize_ip6_address, &a.dst_address);
          unserialize_integer (m, &a.adj_index, sizeof (a.adj_index));
          ip6_add_del_route (&ip6_main, &a);
          n_elts--;
        }
    }
}

void serialize_vnet_ip6_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_interface_main_t * vim = &vm->interface_main;
  vlib_sw_interface_t * si;
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_interface_address_t * as = 0, * a;

  /* Download adjacency tables & multipath stuff. */
  serialize (m, serialize_ip_lookup_main, lm);

  /* FIBs. */
  {
    ip6_fib_t * f;
    u32 n_fibs = vec_len (im->fibs);
    serialize_integer (m, n_fibs, sizeof (n_fibs));
    vec_foreach (f, im->fibs)
      serialize (m, serialize_ip6_fib, f);
  }

  /* FIB interface config. */
  vec_serialize (m, im->fib_index_by_sw_if_index, serialize_vec_32);

  /* Interface ip6 addresses. */
  pool_foreach (si, vim->sw_interfaces, ({
    u32 sw_if_index = si->sw_if_index;
    ip_interface_address_t * ia;
    foreach_ip_interface_address (lm, ia, sw_if_index, ({
      ip6_address_t * x = ip_interface_address_get_address (lm, ia);
      vec_add2 (as, a, 1);
      a->address = x[0];
      a->length = ia->address_length;
      a->sw_if_index = sw_if_index;
    }));
  }));
  vec_serialize (m, as, serialize_vec_ip6_set_interface_address);
  vec_free (as);
}

void unserialize_vnet_ip6_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_interface_address_t * as = 0, * a;

  unserialize (m, unserialize_ip_lookup_main, lm);

  {
    ip_adjacency_t * adj, * adj_heap;
    u32 n_adj;
    adj_heap = im->lookup_main.adjacency_heap;
    heap_foreach (adj, n_adj, adj_heap, ({
      unserialize_fixup_ip6_rewrite_adjacencies (vm, adj, n_adj);
      ip_call_add_del_adjacency_callbacks (&im->lookup_main, adj - adj_heap, /* is_del */ 0);
    }));
  }

  /* FIBs */
  {
    u32 i, n_fibs;
    unserialize_integer (m, &n_fibs, sizeof (n_fibs));
    for (i = 0; i < n_fibs; i++)
      unserialize (m, unserialize_ip6_fib);
  }

  vec_unserialize (m, &im->fib_index_by_sw_if_index, unserialize_vec_32);

  vec_unserialize (m, &as, unserialize_vec_ip6_set_interface_address);
  vec_foreach (a, as) {
    ip6_add_del_interface_address_internal
      (vm, a->sw_if_index, &a->address, a->length,
       /* redistribute */ 0,
       /* insert_routes */ 0,
       /* is_del */ 0);
  }
  vec_free (as);
}

static clib_error_t *
ip6_sw_interface_admin_up_down (vlib_main_t * vm,
				u32 sw_if_index,
				u32 flags)
{
  ip6_main_t * im = &ip6_main;
  ip_interface_address_t * ia;
  ip6_address_t * a;
  u32 is_admin_up, fib_index;

  /* Fill in lookup tables with default table (0). */
  vec_validate (im->fib_index_by_sw_if_index, sw_if_index);

  vec_validate_init_empty (im->lookup_main.if_address_pool_index_by_sw_if_index, sw_if_index, ~0);

  is_admin_up = (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) != 0;

  fib_index = vec_elt (im->fib_index_by_sw_if_index, sw_if_index);

  foreach_ip_interface_address (&im->lookup_main, ia, sw_if_index, ({
    a = ip_interface_address_get_address (&im->lookup_main, ia);
    if (is_admin_up)
      ip6_add_interface_routes (vm, sw_if_index,
				im, fib_index,
				ia);
    else
      ip6_del_interface_routes (im, fib_index,
				a, ia->address_length);
  }));

  return 0;
}

static clib_error_t *
ip6_sw_interface_add_del (vlib_main_t * vm,
			  u32 sw_if_index,
			  u32 is_add)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 ci, cast;

  for (cast = 0; cast < VNET_N_CAST; cast++)
    {
      ip_config_main_t * cm = &lm->rx_config_mains[cast];
      vnet_config_main_t * vcm = &cm->config_main;

      /* FIXME multicast. */
      if (! vcm->node_index_by_feature_index)
	{
	  char * start_nodes[] = { "ip6-input", };
	  char * feature_nodes[] = {
	    [IP6_RX_FEATURE_LOOKUP] = "ip6-lookup",
	  };
	  vnet_config_init (vm, vcm,
			    start_nodes, ARRAY_LEN (start_nodes),
			    feature_nodes, ARRAY_LEN (feature_nodes));
	}

      vec_validate_init_empty (cm->config_index_by_sw_if_index, sw_if_index, ~0);
      ci = cm->config_index_by_sw_if_index[sw_if_index];

      if (is_add)
	ci = vnet_config_add_feature (vm, vcm,
				      ci,
				      IP6_RX_FEATURE_LOOKUP,
				      /* config data */ 0,
				      /* # bytes of config data */ 0);
      else
	ci = vnet_config_del_feature (vm, vcm,
				      ci,
				      IP6_RX_FEATURE_LOOKUP,
				      /* config data */ 0,
				      /* # bytes of config data */ 0);

      cm->config_index_by_sw_if_index[sw_if_index] = ci;
    }

  return /* no error */ 0;
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
    [IP_LOOKUP_NEXT_ARP] = "ip6-discover-neighbor",
    [IP_LOOKUP_NEXT_REWRITE] = "ip6-rewrite",
  },

  .sw_interface_admin_up_down_function = ip6_sw_interface_admin_up_down,
  .sw_interface_add_del_function = ip6_sw_interface_add_del,
};

typedef struct {
  /* Adjacency taken. */
  u32 adj_index;

  /* Packet data, possibly *after* rewrite. */
  u8 packet_data[64 - 1*sizeof(u32)];
} ip6_forward_next_trace_t;

static u8 * format_ip6_forward_next_trace (u8 * s, va_list * args)
{
  vlib_main_t * vm = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
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

static VLIB_REGISTER_NODE (ip6_multicast_node) = {
  .function = ip6_drop,
  .name = "ip6-multicast",
  .vector_size = sizeof (u32),

  .format_trace = format_ip6_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

/* Compute TCP/UDP/ICMP6 checksum in software. */
u16 ip6_tcp_udp_icmp_compute_checksum (vlib_main_t * vm, vlib_buffer_t * p0, ip6_header_t * ip0)
{
  ip_csum_t sum0;
  u16 sum16, payload_length_host_byte_order;
  u32 i, n_this_buffer, n_bytes_left;
  void * data_this_buffer;

  /* Initialize checksum with ip header. */
  sum0 = ip0->payload_length + clib_host_to_net_u16 (ip0->protocol);
  payload_length_host_byte_order = clib_net_to_host_u16 (ip0->payload_length);

  for (i = 0; i < ARRAY_LEN (ip0->src_address.as_uword); i++)
    {
      sum0 = ip_csum_with_carry (sum0,
				 clib_mem_unaligned (&ip0->src_address.as_uword[i], uword));
      sum0 = ip_csum_with_carry (sum0,
				 clib_mem_unaligned (&ip0->dst_address.as_uword[i], uword));
    }

  n_bytes_left = n_this_buffer = payload_length_host_byte_order;
  data_this_buffer = (void *) (ip0 + 1);
  if (p0 && n_this_buffer + sizeof (ip0[0]) > p0->current_length)
    n_this_buffer = p0->current_length > sizeof (ip0[0]) ? p0->current_length - sizeof (ip0[0]) : 0;
  while (1)
    {
      sum0 = ip_incremental_checksum (sum0, data_this_buffer, n_this_buffer);
      n_bytes_left -= n_this_buffer;
      if (n_bytes_left == 0)
	break;

      ASSERT (p0->flags & VLIB_BUFFER_NEXT_PRESENT);
      p0 = vlib_get_buffer (vm, p0->next_buffer);
      data_this_buffer = vlib_buffer_get_current (p0);
      n_this_buffer = p0->current_length;
    }

  sum16 = ~ ip_csum_fold (sum0);

  return sum16;
}

static u32 ip6_tcp_udp_icmp_validate_checksum (vlib_main_t * vm, vlib_buffer_t * p0)
{
  ip6_header_t * ip0 = vlib_buffer_get_current (p0);
  udp_header_t * udp0;
  u16 sum16;

  ASSERT (ip0->protocol == IP_PROTOCOL_TCP
	  || ip0->protocol == IP_PROTOCOL_ICMP6
	  || ip0->protocol == IP_PROTOCOL_UDP);

  udp0 = (void *) (ip0 + 1);
  if (ip0->protocol == IP_PROTOCOL_UDP && udp0->checksum == 0)
    {
      p0->flags |= (IP_BUFFER_L4_CHECKSUM_COMPUTED
		    | IP_BUFFER_L4_CHECKSUM_CORRECT);
      return p0->flags;
    }

  sum16 = ip6_tcp_udp_icmp_compute_checksum (vm, p0, ip0);

  p0->flags |= (IP_BUFFER_L4_CHECKSUM_COMPUTED
		| ((sum16 == 0) << LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT));

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
	  ip6_header_t * ip0, * ip1;
	  udp_header_t * udp0, * udp1;
	  u32 pi0, ip_len0, udp_len0, flags0, next0;
	  u32 pi1, ip_len1, udp_len1, flags1, next1;
	  i32 len_diff0, len_diff1;
	  u8 error0, type0, good_l4_checksum0;
	  u8 error1, type1, good_l4_checksum1;
      
	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];
	  from += 2;
	  n_left_from -= 2;
	  to_next += 2;
	  n_left_to_next -= 2;
      
	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  type0 = lm->builtin_protocol_by_ip_protocol[ip0->protocol];
	  type1 = lm->builtin_protocol_by_ip_protocol[ip1->protocol];

	  next0 = lm->local_next_by_ip_protocol[ip0->protocol];
	  next1 = lm->local_next_by_ip_protocol[ip1->protocol];

	  flags0 = p0->flags;
	  flags1 = p1->flags;

	  good_l4_checksum0 = (flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	  good_l4_checksum1 = (flags1 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;

	  udp0 = ip6_next_header (ip0);
	  udp1 = ip6_next_header (ip1);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_l4_checksum0 |= type0 == IP_BUILTIN_PROTOCOL_UDP && udp0->checksum == 0;
	  good_l4_checksum1 |= type1 == IP_BUILTIN_PROTOCOL_UDP && udp1->checksum == 0;

	  good_l4_checksum0 |= type0 == IP_BUILTIN_PROTOCOL_UNKNOWN;
	  good_l4_checksum1 |= type1 == IP_BUILTIN_PROTOCOL_UNKNOWN;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->payload_length);
	  ip_len1 = clib_net_to_host_u16 (ip1->payload_length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);
	  udp_len1 = clib_net_to_host_u16 (udp1->length);

	  len_diff0 = ip_len0 - udp_len0;
	  len_diff1 = ip_len1 - udp_len1;

	  len_diff0 = type0 == IP_BUILTIN_PROTOCOL_UDP ? len_diff0 : 0;
	  len_diff1 = type1 == IP_BUILTIN_PROTOCOL_UDP ? len_diff1 : 0;

	  if (PREDICT_FALSE (type0 != IP_BUILTIN_PROTOCOL_UNKNOWN
			     && ! good_l4_checksum0
			     && ! (flags0 & IP_BUFFER_L4_CHECKSUM_COMPUTED)))
	    {
	      flags0 = ip6_tcp_udp_icmp_validate_checksum (vm, p0);
	      good_l4_checksum0 =
		(flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	    }
	  if (PREDICT_FALSE (type1 != IP_BUILTIN_PROTOCOL_UNKNOWN
			     && ! good_l4_checksum1
			     && ! (flags1 & IP_BUFFER_L4_CHECKSUM_COMPUTED)))
	    {
	      flags1 = ip6_tcp_udp_icmp_validate_checksum (vm, p1);
	      good_l4_checksum1 =
		(flags1 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	    }

	  error0 = error1 = IP6_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP6_ERROR_UDP_LENGTH : error0;
	  error1 = len_diff1 < 0 ? IP6_ERROR_UDP_LENGTH : error1;

	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_UDP == IP6_ERROR_UDP_CHECKSUM);
	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_TCP == IP6_ERROR_TCP_CHECKSUM);
	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_ICMP == IP6_ERROR_ICMP_CHECKSUM);
	  error0 = (! good_l4_checksum0
		    ? IP6_ERROR_UDP_CHECKSUM + type0
		    : error0);
	  error1 = (! good_l4_checksum1
		    ? IP6_ERROR_UDP_CHECKSUM + type1
		    : error1);

	  /* Drop packets from unroutable hosts. */
	  if (error0 == IP6_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index0 = ip6_src_lookup_for_packet (im, p0, ip0);
	      error0 = (lm->miss_adj_index == src_adj_index0
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }
	  if (error1 == IP6_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index1 = ip6_src_lookup_for_packet (im, p1, ip1);
	      error1 = (lm->miss_adj_index == src_adj_index1
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error1);
	    }

	  next0 = error0 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;
	  next1 = error1 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next1;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, pi1, next0, next1);
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  udp_header_t * udp0;
	  u32 pi0, ip_len0, udp_len0, flags0, next0;
	  i32 len_diff0;
	  u8 error0, type0, good_l4_checksum0;
      
	  pi0 = to_next[0] = from[0];
	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);

	  type0 = lm->builtin_protocol_by_ip_protocol[ip0->protocol];
	  next0 = lm->local_next_by_ip_protocol[ip0->protocol];

	  flags0 = p0->flags;

	  good_l4_checksum0 = (flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;

	  udp0 = ip6_next_header (ip0);

	  /* Don't verify UDP checksum for packets with explicit zero checksum. */
	  good_l4_checksum0 |= type0 == IP_BUILTIN_PROTOCOL_UDP && udp0->checksum == 0;

	  good_l4_checksum0 |= type0 == IP_BUILTIN_PROTOCOL_UNKNOWN;

	  /* Verify UDP length. */
	  ip_len0 = clib_net_to_host_u16 (ip0->payload_length);
	  udp_len0 = clib_net_to_host_u16 (udp0->length);

	  len_diff0 = ip_len0 - udp_len0;

	  len_diff0 = type0 == IP_BUILTIN_PROTOCOL_UDP ? len_diff0 : 0;

	  if (PREDICT_FALSE (type0 != IP_BUILTIN_PROTOCOL_UNKNOWN
			     && ! good_l4_checksum0
			     && ! (flags0 & IP_BUFFER_L4_CHECKSUM_COMPUTED)))
	    {
	      flags0 = ip6_tcp_udp_icmp_validate_checksum (vm, p0);
	      good_l4_checksum0 =
		(flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	    }

	  error0 = IP6_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP6_ERROR_UDP_LENGTH : error0;

	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_UDP == IP6_ERROR_UDP_CHECKSUM);
	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_TCP == IP6_ERROR_TCP_CHECKSUM);
	  ASSERT (IP6_ERROR_UDP_CHECKSUM + IP_BUILTIN_PROTOCOL_ICMP == IP6_ERROR_ICMP_CHECKSUM);
	  error0 = (! good_l4_checksum0
		    ? IP6_ERROR_UDP_CHECKSUM + type0
		    : error0);

	  if (error0 == IP6_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index0 = ip6_src_lookup_for_packet (im, p0, ip0);
	      error0 = (lm->miss_adj_index == src_adj_index0
			? IP6_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }

	  next0 = error0 != IP6_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;

	  p0->error = error_node->errors[error0];

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, next0);
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
    [IP_LOCAL_NEXT_TCP_LOOKUP] = "ip6-tcp-lookup",
    [IP_LOCAL_NEXT_UDP_LOOKUP] = "ip6-udp-lookup",
    [IP_LOCAL_NEXT_ICMP] = "ip6-icmp-input",
  },
};

void ip6_register_protocol (u32 protocol, u32 node_index)
{
  vlib_main_t * vm = &vlib_global_main;
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;

  ASSERT (protocol < ARRAY_LEN (lm->local_next_by_ip_protocol));
  lm->local_next_by_ip_protocol[protocol] = vlib_node_add_next (vm, ip6_local_node.index, node_index);
}

typedef enum {
  IP6_DISCOVER_NEIGHBOR_NEXT_DROP,
  IP6_DISCOVER_NEIGHBOR_N_NEXT,
} ip6_discover_neighbor_next_t;

typedef enum {
  IP6_DISCOVER_NEIGHBOR_ERROR_DROP,
  IP6_DISCOVER_NEIGHBOR_ERROR_REQUEST_SENT,
} ip6_discover_neighbor_error_t;

static uword
ip6_discover_neighbor (vlib_main_t * vm,
		       vlib_node_runtime_t * node,
		       vlib_frame_t * frame)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 * from, * to_next_drop;
  uword n_left_from, n_left_to_next_drop;
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

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, IP6_DISCOVER_NEIGHBOR_NEXT_DROP,
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
	  b0 ^= ip0->dst_address.as_u32[0];
	  c0 ^= ip0->dst_address.as_u32[1];

	  hash_v3_mix32 (a0, b0, c0);

	  b0 ^= ip0->dst_address.as_u32[2];
	  c0 ^= ip0->dst_address.as_u32[3];

	  hash_v3_finalize32 (a0, b0, c0);

	  c0 &= BITS (hash_bitmap) - 1;
	  c0 = c0 / BITS (uword);
	  m0 = (uword) 1 << (c0 % BITS (uword));

	  bm0 = hash_bitmap[c0];
	  drop0 = (bm0 & m0) != 0;

	  /* Mark it as seen. */
	  hash_bitmap[c0] = bm0 | m0;

	  from += 1;
	  n_left_from -= 1;
	  to_next_drop[0] = pi0;
	  to_next_drop += 1;
	  n_left_to_next_drop -= 1;

	  p0->error = node->errors[drop0 ? IP6_DISCOVER_NEIGHBOR_ERROR_DROP : IP6_DISCOVER_NEIGHBOR_ERROR_REQUEST_SENT];

	  if (drop0)
	    continue;

	  {
	    u32 bi0;
	    icmp6_neighbor_solicitation_header_t * h0;
	    vlib_hw_interface_t * hw_if0;
	    vlib_buffer_t * b0;

	    h0 = vlib_packet_template_get_packet (vm, &im->discover_neighbor_packet_template, &bi0);

	    /* Build ethernet header. */
	    hw_if0 = vlib_get_sup_hw_interface (vm, sw_if_index0);

	    /* Choose source address based on destination lookup adjacency. */
	    ip6_src_address_for_packet (im, p0, &h0->ip.src_address, sw_if_index0);

	    /* Destination address is a solicited node multicast address.  We need to fill in
	       the low 24 bits with low 24 bits of target's address. */
	    h0->ip.dst_address.as_u8[13] = ip0->dst_address.as_u8[13];
	    h0->ip.dst_address.as_u8[14] = ip0->dst_address.as_u8[14];
	    h0->ip.dst_address.as_u8[15] = ip0->dst_address.as_u8[15];

	    h0->neighbor.target_address = ip0->dst_address;

	    memcpy (h0->link_layer_option.ethernet_address, hw_if0->hw_address,
		    vec_len (hw_if0->hw_address));

	    h0->neighbor.icmp.checksum = ip6_tcp_udp_icmp_compute_checksum (vm, 0, &h0->ip);
	    ASSERT (0 == ip6_tcp_udp_icmp_compute_checksum (vm, 0, &h0->ip));

	    vlib_buffer_copy_trace_flag (vm, p0, bi0);
	    b0 = vlib_get_buffer (vm, bi0);
	    b0->sw_if_index[VLIB_TX] = p0->sw_if_index[VLIB_TX];

	    /* Add rewrite/encap string. */
	    vnet_rewrite_one_header (adj0[0], h0, sizeof (ethernet_header_t));
	    vlib_buffer_advance (b0, -adj0->rewrite_header.data_bytes);

	    next0 = vec_elt (im->discover_neighbor_next_index_by_hw_if_index, hw_if0->hw_if_index);

	    vlib_set_next_frame_buffer (vm, node, next0, bi0);
	  }
	}

      vlib_put_next_frame (vm, node, IP6_DISCOVER_NEIGHBOR_NEXT_DROP, n_left_to_next_drop);
    }

  return frame->n_vectors;
}

static char * ip6_discover_neighbor_error_strings[] = {
  [IP6_DISCOVER_NEIGHBOR_ERROR_DROP] = "address overflow drops",
  [IP6_DISCOVER_NEIGHBOR_ERROR_REQUEST_SENT] = "neighbor solicitations sent",
};

static clib_error_t *
ip6_discover_neighbor_hw_interface_link_up_down (vlib_main_t * vm,
				   u32 hw_if_index,
				   u32 flags);

VLIB_REGISTER_NODE (ip6_discover_neighbor_node) = {
  .function = ip6_discover_neighbor,
  .name = "ip6-discover-neighbor",
  .vector_size = sizeof (u32),

  .hw_interface_link_up_down_function = ip6_discover_neighbor_hw_interface_link_up_down,

  .format_trace = format_ip6_forward_next_trace,

  .n_errors = ARRAY_LEN (ip6_discover_neighbor_error_strings),
  .error_strings = ip6_discover_neighbor_error_strings,

  .n_next_nodes = IP6_DISCOVER_NEIGHBOR_N_NEXT,
  .next_nodes = {
    [IP6_DISCOVER_NEIGHBOR_NEXT_DROP] = "error-drop",
  },
};

static clib_error_t *
ip6_discover_neighbor_hw_interface_link_up_down (vlib_main_t * vm,
						 u32 hw_if_index,
						 u32 flags)
{
  ip6_main_t * im = &ip6_main;
  vlib_hw_interface_t * hw_if;

  hw_if = vlib_get_hw_interface (vm, hw_if_index);

  vec_validate_init_empty (im->discover_neighbor_next_index_by_hw_if_index, hw_if_index, ~0);
  im->discover_neighbor_next_index_by_hw_if_index[hw_if_index]
    = vlib_node_add_next (vm, ip6_discover_neighbor_node.index, hw_if->output_node_index);

  return 0;
}

clib_error_t *
ip6_probe_neighbor (vlib_main_t * vm, ip6_address_t * dst, u32 sw_if_index)
{
  ip6_main_t * im = &ip6_main;
  icmp6_neighbor_solicitation_header_t * h;
  ip6_address_t * src;
  ip_interface_address_t * ia;
  ip_adjacency_t * adj;
  vlib_hw_interface_t * hi;
  vlib_buffer_t * b;
  u32 bi;

  src = ip6_interface_address_matching_destination (im, dst, sw_if_index, &ia);
  if (! src)
    return clib_error_return (0, "no matching interface address for destination %U (interface %U)",
			      format_ip6_address, dst,
			      format_vlib_sw_if_index_name, vm, sw_if_index);

  h = vlib_packet_template_get_packet (vm, &im->discover_neighbor_packet_template, &bi);

  hi = vlib_get_sup_hw_interface (vm, sw_if_index);

  /* Destination address is a solicited node multicast address.  We need to fill in
     the low 24 bits with low 24 bits of target's address. */
  h->ip.dst_address.as_u8[13] = dst->as_u8[13];
  h->ip.dst_address.as_u8[14] = dst->as_u8[14];
  h->ip.dst_address.as_u8[15] = dst->as_u8[15];

  h->ip.src_address = src[0];
  h->neighbor.target_address = dst[0];

  memcpy (h->link_layer_option.ethernet_address, hi->hw_address, vec_len (hi->hw_address));

  h->neighbor.icmp.checksum = ip6_tcp_udp_icmp_compute_checksum (vm, 0, &h->ip);
  ASSERT (0 == ip6_tcp_udp_icmp_compute_checksum (vm, 0, &h->ip));

  b = vlib_get_buffer (vm, bi);
  b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX] = sw_if_index;

  /* Add encapsulation string for software interface (e.g. ethernet header). */
  adj = ip_get_adjacency (&im->lookup_main, ia->neighbor_probe_adj_index);
  vnet_rewrite_one_header (adj[0], h, sizeof (ethernet_header_t));
  vlib_buffer_advance (b, -adj->rewrite_header.data_bytes);

  {
    vlib_frame_t * f = vlib_get_frame_to_node (vm, hi->output_node_index);
    u32 * to_next = vlib_frame_vector_args (f);
    to_next[0] = bi;
    f->n_vectors = 1;
    vlib_put_frame_to_node (vm, hi->output_node_index, f);
  }

  return /* no error */ 0;
}

typedef enum {
  IP6_REWRITE_NEXT_DROP,
} ip6_rewrite_next_t;

static uword
ip6_rewrite (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  ip_lookup_main_t * lm = &ip6_main.lookup_main;
  u32 * from = vlib_frame_vector_args (frame);
  u32 n_left_from, n_left_to_next, * to_next, next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip6_input_node.index);

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
	  u32 pi0, rw_len0, next0, error0, adj_index0;
	  u32 pi1, rw_len1, next1, error1, adj_index1;
      
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

	  error0 = error1 = IP6_ERROR_NONE;

	  {
	    i32 hop_limit0 = ip0->hop_limit, hop_limit1 = ip1->hop_limit;

	    /* Input node should have reject packets with hop limit 0. */
	    ASSERT (ip0->hop_limit > 0);
	    ASSERT (ip1->hop_limit > 0);

	    hop_limit0 -= (p0->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;
	    hop_limit1 -= (p1->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;

	    ip0->hop_limit = hop_limit0;
	    ip1->hop_limit = hop_limit1;

	    error0 = hop_limit0 <= 0 ? IP6_ERROR_TIME_EXPIRED : error0;
	    error1 = hop_limit1 <= 0 ? IP6_ERROR_TIME_EXPIRED : error1;
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

	  /* Check MTU of outgoing interface. */
	  error0 = (vlib_buffer_length_in_chain (vm, p0) > adj0[0].rewrite_header.max_l3_packet_bytes
		    ? IP6_ERROR_MTU_EXCEEDED
		    : error0);
	  error1 = (vlib_buffer_length_in_chain (vm, p1) > adj1[0].rewrite_header.max_l3_packet_bytes
		    ? IP6_ERROR_MTU_EXCEEDED
		    : error1);

	  p0->current_data -= rw_len0;
	  p1->current_data -= rw_len1;

	  p0->current_length += rw_len0;
	  p1->current_length += rw_len1;

	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
	  p1->sw_if_index[VLIB_TX] = adj1[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;
	  next1 = adj1[0].rewrite_header.next_index;

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_two_headers (adj0[0], adj1[0],
				    ip0, ip1,
				    sizeof (ethernet_header_t));
      
	  vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, pi1, next0, next1);
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  ip_adjacency_t * adj0;
	  ip_buffer_opaque_t * i0;
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  u32 pi0, rw_len0;
	  u32 adj_index0, next0, error0;
      
	  pi0 = to_next[0] = from[0];

	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->dst_adj_index;
	  adj0 = ip_get_adjacency (lm, adj_index0);
      
	  ip0 = vlib_buffer_get_current (p0);

	  error0 = IP6_ERROR_NONE;

	  /* Check hop limit */
	  {
	    i32 hop_limit0 = ip0->hop_limit;

	    ASSERT (ip0->hop_limit > 0);

	    hop_limit0 -= (p0->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;

	    ip0->hop_limit = hop_limit0;

	    error0 = hop_limit0 <= 0 ? IP6_ERROR_TIME_EXPIRED : error0;
	  }

	  /* Guess we are only writing on simple Ethernet header. */
	  vnet_rewrite_one_header (adj0[0], ip0, sizeof (ethernet_header_t));
      
	  /* Update packet buffer attributes/set output interface. */
	  rw_len0 = adj0[0].rewrite_header.data_bytes;

	  vlib_increment_combined_counter (&lm->adjacency_counters,
					   adj_index0,
					   /* packet increment */ 0,
					   /* byte increment */ rw_len0);

	  /* Check MTU of outgoing interface. */
	  error0 = (vlib_buffer_length_in_chain (vm, p0) > adj0[0].rewrite_header.max_l3_packet_bytes
		    ? IP6_ERROR_MTU_EXCEEDED
		    : error0);

	  p0->current_data -= rw_len0;
	  p0->current_length += rw_len0;
	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;

	  next0 = error0 != IP6_ERROR_NONE ? IP6_REWRITE_NEXT_DROP : next0;
	  p0->error = error_node->errors[error0];

	  from += 1;
	  n_left_from -= 1;
	  to_next += 1;
	  n_left_to_next -= 1;
      
	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   pi0, next0);
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
	im->fib_masks[i].as_u32[j] = ~0;

      if (i1)
	im->fib_masks[i].as_u32[i0] = clib_host_to_net_u32 (pow2_mask (i1) << (32 - i1));
    }

  ip_lookup_init (&im->lookup_main, /* is_ip6 */ 1);

  /* Create FIB with index 0 and table id of 0. */
  find_fib_by_table_index_or_id (im, /* table id */ 0, IP6_ROUTE_FLAG_TABLE_ID);

  {
    pg_node_t * pn;
    pn = pg_get_node (ip6_lookup_node.index);
    pn->unformat_edit = unformat_pg_ip6_header;
  }

  {
    icmp6_neighbor_solicitation_header_t p;

    memset (&p, 0, sizeof (p));

    p.ip.ip_version_traffic_class_and_flow_label = clib_host_to_net_u32 (0x6 << 28);
    p.ip.payload_length = clib_host_to_net_u16 (sizeof (p)
						- STRUCT_OFFSET_OF (icmp6_neighbor_solicitation_header_t, neighbor));
    p.ip.protocol = IP_PROTOCOL_ICMP6;
    p.ip.hop_limit = 255;
    ip6_set_solicited_node_multicast_address (&p.ip.dst_address, 0);

    p.neighbor.icmp.type = ICMP6_neighbor_solicitation;

    p.link_layer_option.header.type = ICMP6_NEIGHBOR_DISCOVERY_OPTION_source_link_layer_address;
    p.link_layer_option.header.n_data_u64s = sizeof (p.link_layer_option) / sizeof (u64);

    vlib_packet_template_init (vm,
			       &im->discover_neighbor_packet_template,
			       &p, sizeof (p),
			       /* alloc chunk size */ 8,
			       VNET_BUFFER_LOCALLY_GENERATED,
			       "ip6 neighbor discovery");
  }

  return 0;
}

VLIB_INIT_FUNCTION (ip6_lookup_init);
