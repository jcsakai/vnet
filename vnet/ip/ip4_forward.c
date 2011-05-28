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
#include <vnet/ppp/ppp.h>
#include <vnet/vnet/l3_types.h>

/* This is really, really simple but stupid fib. */
u32
ip4_fib_lookup_with_table (ip4_main_t * im, u32 fib_index,
			   ip4_address_t * dst,
			   u32 disable_default_route)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip4_fib_t * fib = vec_elt_at_index (im->fibs, fib_index);
  uword * p, * hash, key;
  i32 i, i_min, dst_address, ai;

  i_min = disable_default_route ? 0 : 1;
  dst_address = clib_mem_unaligned (&dst->data_u32, u32);
  for (i = ARRAY_LEN (fib->adj_index_by_dst_address) - 1; i >= i_min; i--)
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
  return ai;
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

static void
ip4_fib_init_adj_index_by_dst_address (ip_lookup_main_t * lm,
				       ip4_fib_t * fib,
				       u32 address_length)
{
  hash_t * h;
  uword max_index;

  ASSERT (lm->fib_result_n_bytes >= sizeof (uword));
  lm->fib_result_n_words = round_pow2 (lm->fib_result_n_bytes, sizeof (uword)) / sizeof (uword);

  fib->adj_index_by_dst_address[address_length] =
    hash_create (32 /* elts */, lm->fib_result_n_words * sizeof (uword));

  hash_set_flags (fib->adj_index_by_dst_address[address_length],
                  HASH_FLAG_NO_AUTO_SHRINK);

  h = hash_header (fib->adj_index_by_dst_address[address_length]);
  max_index = (hash_value_bytes (h) / sizeof (fib->new_hash_values[0])) - 1;

  /* Initialize new/old hash value vectors. */
  vec_validate_init_empty (fib->new_hash_values, max_index, ~0);
  vec_validate_init_empty (fib->old_hash_values, max_index, ~0);
}

static void serialize_ip4_address (serialize_main_t * m, va_list * va)
{
  ip4_address_t * a = va_arg (*va, ip4_address_t *);
  u8 * p = serialize_get (m, sizeof (a->as_u8));
  memcpy (p, a->as_u8, sizeof (a->as_u8));
}

static void unserialize_ip4_address (serialize_main_t * m, va_list * va)
{
  ip4_address_t * a = va_arg (*va, ip4_address_t *);
  u8 * p = unserialize_get (m, sizeof (a->as_u8));
  memcpy (a->as_u8, p, sizeof (a->as_u8));
}

static void serialize_ip4_address_and_length (serialize_main_t * m, va_list * va)
{
  ip4_address_t * a = va_arg (*va, ip4_address_t *);
  u32 l = va_arg (*va, u32);
  u32 n_bytes = (l / 8) + ((l % 8) != 0);
  u8 * p = serialize_get (m, 1 + n_bytes);
  ASSERT (l <= 32);
  p[0] = l;
  memcpy (p + 1, a->as_u8, n_bytes);
}

static void unserialize_ip4_address_and_length (serialize_main_t * m, va_list * va)
{
  ip4_address_t * a = va_arg (*va, ip4_address_t *);
  u32 * al = va_arg (*va, u32 *);
  u8 * p = unserialize_get (m, 1);
  u32 l, n_bytes;

  al[0] = l = p[0];
  ASSERT (l <= 32);
  n_bytes = (l / 8) + ((l % 8) != 0);

  if (n_bytes)
    {
      p = unserialize_get (m, n_bytes);
      memcpy (a->as_u8, p, n_bytes);
    }
}

static void serialize_ip4_add_del_route_msg (serialize_main_t * m, va_list * va)
{
  ip4_add_del_route_args_t * a = va_arg (*va, ip4_add_del_route_args_t *);
    
  serialize_likely_small_unsigned_integer (m, a->table_index_or_table_id);
  serialize_likely_small_unsigned_integer (m, a->flags);
  serialize (m, serialize_ip4_address_and_length, &a->dst_address, a->dst_address_length);
  serialize_likely_small_unsigned_integer (m, a->adj_index);
  serialize_likely_small_unsigned_integer (m, a->n_add_adj);
  if (a->n_add_adj > 0)
    serialize (m, serialize_vec_ip_adjacency, a->add_adj, a->n_add_adj);
}

/* Serialized adjacencies for arp/rewrite do not send graph next_index
   since graph hookup is not guaranteed to be the same for both sides
   of serialize/unserialize. */
static void
unserialize_fixup_ip4_rewrite_adjacencies (vlib_main_t * vm,
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
	  ni = is_arp ? ip4_arp_node.index : ip4_rewrite_node.index;
	  adj[i].rewrite_header.node_index = ni;
	  adj[i].rewrite_header.next_index = vlib_node_add_next (vm, ni, hw->output_node_index);
	  break;

	default:
	  break;
	}
    }
}

static void unserialize_ip4_add_del_route_msg (serialize_main_t * m, va_list * va)
{
  ip4_main_t * i4m = &ip4_main;
  ip4_add_del_route_args_t a;
    
  a.table_index_or_table_id = unserialize_likely_small_unsigned_integer (m);
  a.flags = unserialize_likely_small_unsigned_integer (m);
  unserialize (m, unserialize_ip4_address_and_length, &a.dst_address, &a.dst_address_length);
  a.adj_index = unserialize_likely_small_unsigned_integer (m);
  a.n_add_adj = unserialize_likely_small_unsigned_integer (m);
  a.add_adj = 0;
  if (a.n_add_adj > 0)
    {
      vec_resize (a.add_adj, a.n_add_adj);
      unserialize (m, unserialize_vec_ip_adjacency, a.add_adj, a.n_add_adj);
      unserialize_fixup_ip4_rewrite_adjacencies (&vlib_global_main, a.add_adj, a.n_add_adj);
    }

  /* Prevent re-re-distribution. */
  a.flags |= IP4_ROUTE_FLAG_NO_REDISTRIBUTE;

  ip4_add_del_route (i4m, &a);

  vec_free (a.add_adj);
}

static MC_SERIALIZE_MSG (ip4_add_del_route_msg) = {
  .name = "vnet_ip4_add_del_route",
  .serialize = serialize_ip4_add_del_route_msg,
  .unserialize = unserialize_ip4_add_del_route_msg,
};

static void
ip4_fib_set_adj_index (ip4_main_t * im,
		       ip4_fib_t * fib,
		       u32 flags,
		       u32 dst_address_u32,
		       u32 dst_address_length,
		       u32 adj_index)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  uword * hash;

  memset (fib->old_hash_values, ~0, vec_bytes (fib->old_hash_values));
  memset (fib->new_hash_values, ~0, vec_bytes (fib->new_hash_values));
  fib->new_hash_values[0] = adj_index;

  /* Make sure adj index is valid. */
  if (DEBUG > 0)
    (void) ip_get_adjacency (lm, adj_index);

  hash = fib->adj_index_by_dst_address[dst_address_length];

  hash = _hash_set3 (hash, dst_address_u32,
		     fib->new_hash_values,
		     fib->old_hash_values);

  fib->adj_index_by_dst_address[dst_address_length] = hash;

  if (vec_len (im->add_del_route_callbacks) > 0)
    {
      ip4_add_del_route_callback_t * cb;
      ip4_address_t d;
      uword * p;

      d.data_u32 = dst_address_u32;
      vec_foreach (cb, im->add_del_route_callbacks)
	if ((flags & cb->required_flags) == cb->required_flags)
	  cb->function (im, cb->function_opaque,
			fib, flags,
			&d, dst_address_length,
			fib->old_hash_values,
			fib->new_hash_values);

      p = hash_get (hash, dst_address_u32);
      memcpy (p, fib->new_hash_values, vec_bytes (fib->new_hash_values));
    }
}

void ip4_add_del_route (ip4_main_t * im, ip4_add_del_route_args_t * a)
{
  vlib_main_t * vm = &vlib_global_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip4_fib_t * fib;
  u32 dst_address, dst_address_length, adj_index;
  uword * hash, is_del;
  ip4_add_del_route_callback_t * cb;

  if (vm->mc_main && ! (a->flags & IP4_ROUTE_FLAG_NO_REDISTRIBUTE))
    {
      u32 multiple_messages_per_vlib_buffer = (a->flags & IP4_ROUTE_FLAG_NOT_LAST_IN_GROUP);
      mc_serialize2 (vm->mc_main, multiple_messages_per_vlib_buffer,
		     &ip4_add_del_route_msg, a);
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

  dst_address = a->dst_address.data_u32;
  dst_address_length = a->dst_address_length;
  fib = find_fib_by_table_index_or_id (im, a->table_index_or_table_id, a->flags);

  ASSERT (dst_address_length < ARRAY_LEN (im->fib_masks));
  dst_address &= im->fib_masks[dst_address_length];

  if (! fib->adj_index_by_dst_address[dst_address_length])
    ip4_fib_init_adj_index_by_dst_address (lm, fib, dst_address_length);

  hash = fib->adj_index_by_dst_address[dst_address_length];

  is_del = (a->flags & IP4_ROUTE_FLAG_DEL) != 0;

  if (is_del)
    {
      fib->old_hash_values[0] = ~0;
      hash = _hash_unset (hash, dst_address, fib->old_hash_values);
      fib->adj_index_by_dst_address[dst_address_length] = hash;

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
    ip4_fib_set_adj_index (im, fib, a->flags, dst_address, dst_address_length,
			   adj_index);

  /* Delete old adjacency index if present and changed. */
  {
    u32 old_adj_index = fib->old_hash_values[0];
    if (! (a->flags & IP4_ROUTE_FLAG_KEEP_OLD_ADJACENCY)
	&& old_adj_index != ~0
	&& old_adj_index != adj_index)
      ip_del_adjacency (lm, old_adj_index);
  }
}

static void serialize_ip4_add_del_route_next_hop_msg (serialize_main_t * m, va_list * va)
{
  u32 flags = va_arg (*va, u32);
  ip4_address_t * dst_address = va_arg (*va, ip4_address_t *);
  u32 dst_address_length = va_arg (*va, u32);
  ip4_address_t * next_hop_address = va_arg (*va, ip4_address_t *);
  u32 next_hop_sw_if_index = va_arg (*va, u32);
  u32 next_hop_weight = va_arg (*va, u32);

  serialize_likely_small_unsigned_integer (m, flags);
  serialize (m, serialize_ip4_address_and_length, dst_address, dst_address_length);
  serialize (m, serialize_ip4_address, next_hop_address);
  serialize_likely_small_unsigned_integer (m, next_hop_sw_if_index);
  serialize_likely_small_unsigned_integer (m, next_hop_weight);
}

static void unserialize_ip4_add_del_route_next_hop_msg (serialize_main_t * m, va_list * va)
{
  ip4_main_t * im = &ip4_main;
  u32 flags, dst_address_length, next_hop_sw_if_index, next_hop_weight;
  ip4_address_t dst_address, next_hop_address;

  flags = unserialize_likely_small_unsigned_integer (m);
  unserialize (m, unserialize_ip4_address_and_length, &dst_address, &dst_address_length);
  unserialize (m, unserialize_ip4_address, &next_hop_address);
  next_hop_sw_if_index = unserialize_likely_small_unsigned_integer (m);
  next_hop_weight = unserialize_likely_small_unsigned_integer (m);

  ip4_add_del_route_next_hop
    (im,
     flags | IP4_ROUTE_FLAG_NO_REDISTRIBUTE,
     &dst_address,
     dst_address_length,
     &next_hop_address,
     next_hop_sw_if_index,
     next_hop_weight);
}

static MC_SERIALIZE_MSG (ip4_add_del_route_next_hop_msg) = {
  .name = "vnet_ip4_add_del_route_next_hop",
  .serialize = serialize_ip4_add_del_route_next_hop_msg,
  .unserialize = unserialize_ip4_add_del_route_next_hop_msg,
};

void
ip4_add_del_route_next_hop (ip4_main_t * im,
			    u32 flags,
			    ip4_address_t * dst_address,
			    u32 dst_address_length,
			    ip4_address_t * next_hop,
			    u32 next_hop_sw_if_index,
			    u32 next_hop_weight)
{
  vlib_main_t * vm = &vlib_global_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 fib_index;
  ip4_fib_t * fib;
  u32 dst_address_u32, old_mp_adj_index, new_mp_adj_index;
  u32 dst_adj_index, nh_adj_index;
  uword * dst_hash, * dst_result;
  uword * nh_hash, * nh_result;
  ip_adjacency_t * dst_adj;
  ip_multipath_adjacency_t * old_mp, * new_mp;
  int is_del = (flags & IP4_ROUTE_FLAG_DEL) != 0;
  int is_interface_next_hop;
  clib_error_t * error = 0;

  if (vm->mc_main && ! (flags & IP4_ROUTE_FLAG_NO_REDISTRIBUTE))
    {
      u32 multiple_messages_per_vlib_buffer = (flags & IP4_ROUTE_FLAG_NOT_LAST_IN_GROUP);
      mc_serialize2 (vm->mc_main,
		     multiple_messages_per_vlib_buffer,
		     &ip4_add_del_route_next_hop_msg,
		     flags,
		     dst_address, dst_address_length,
		     next_hop, next_hop_sw_if_index, next_hop_weight);
      return;
    }

  fib_index = vec_elt (im->fib_index_by_sw_if_index, next_hop_sw_if_index);
  fib = vec_elt_at_index (im->fibs, fib_index);

  /* Lookup next hop to be added or deleted. */
  is_interface_next_hop = next_hop->data_u32 == 0;
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
	  ip4_adjacency_set_interface_route (vm, adj, next_hop_sw_if_index, /* if_address_index */ ~0);
	  ip_call_add_del_adjacency_callbacks (lm, nh_adj_index, /* is_del */ 0);
	  hash_set (im->interface_route_adj_index_by_sw_if_index, next_hop_sw_if_index, nh_adj_index);
	}
    }
  else
    {
      nh_hash = fib->adj_index_by_dst_address[32];
      nh_result = hash_get (nh_hash, next_hop->data_u32);

      /* Next hop must be known. */
      if (! nh_result)
	{
	  error = clib_error_return (0, "next-hop %U/32 not in FIB",
				     format_ip4_address, next_hop);
	  goto done;
	}
      nh_adj_index = *nh_result;
    }

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
	{
	  error = clib_error_return (0, "unknown destination %U/%d",
				     format_ip4_address, dst_address,
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
				 format_ip4_address, next_hop);
      goto done;
    }
  
  old_mp = new_mp = 0;
  if (old_mp_adj_index != ~0)
    old_mp = vec_elt_at_index (lm->multipath_adjacencies, old_mp_adj_index);
  if (new_mp_adj_index != ~0)
    new_mp = vec_elt_at_index (lm->multipath_adjacencies, new_mp_adj_index);

  if (old_mp != new_mp)
    {
      ip4_add_del_route_args_t a;
      a.table_index_or_table_id = fib_index;
      a.flags = ((is_del ? IP4_ROUTE_FLAG_DEL : IP4_ROUTE_FLAG_ADD)
		 | IP4_ROUTE_FLAG_FIB_INDEX
		 | IP4_ROUTE_FLAG_KEEP_OLD_ADJACENCY
		 | (flags & (IP4_ROUTE_FLAG_NO_REDISTRIBUTE | IP4_ROUTE_FLAG_NOT_LAST_IN_GROUP)));
      a.dst_address = dst_address[0];
      a.dst_address_length = dst_address_length;
      a.adj_index = new_mp ? new_mp->adj_index : dst_adj_index;
      a.add_adj = 0;
      a.n_add_adj = 0;

      ip4_add_del_route (im, &a);
    }

 done:
  if (error)
    clib_error_report (error);
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

void
ip4_foreach_matching_route (ip4_main_t * im,
			    u32 table_index_or_table_id,
			    u32 flags,
			    ip4_address_t * address,
			    u32 address_length,
			    ip4_address_t ** results,
			    u8 ** result_lengths)
{
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  u32 dst_address = address->data_u32;
  u32 this_length = address_length;
  
  if (*results)
    _vec_len (*results) = 0;
  if (*result_lengths)
    _vec_len (*result_lengths) = 0;

  while (this_length <= 32 && vec_len (results) == 0)
    {
      uword k, v;
      hash_foreach (k, v, fib->adj_index_by_dst_address[this_length], ({
	if (0 == ((k ^ dst_address) & im->fib_masks[address_length]))
	  {
	    ip4_address_t a;
	    a.data_u32 = k;
	    vec_add1 (*results, a);
	    vec_add1 (*result_lengths, this_length);
	  }
      }));

      this_length++;
    }
}

void ip4_maybe_remap_adjacencies (ip4_main_t * im,
				  u32 table_index_or_table_id,
				  u32 flags)
{
  ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_index_or_table_id, flags);
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 i, l;
  ip4_address_t a;
  ip4_add_del_route_callback_t * cb;
  static ip4_address_t * to_delete;

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

	if (m)
	  {
	    /* Record destination address from hash key. */
	    a.data_u32 = p->key;

	    /* Reset mapping table. */
	    lm->adjacency_remap_table[adj_index] = 0;

	    /* New adjacency points to nothing: so delete prefix. */
	    if (m == ~0)
	      vec_add1 (to_delete, a);
	    else
	      {
		/* Remap to new adjacency. */
		memcpy (fib->old_hash_values, p->value, vec_bytes (fib->old_hash_values));

		/* Set new adjacency value. */
		fib->new_hash_values[0] = p->value[0] = m - 1;

		vec_foreach (cb, im->add_del_route_callbacks)
		  if ((flags & cb->required_flags) == cb->required_flags)
		    cb->function (im, cb->function_opaque,
				  fib, flags | IP4_ROUTE_FLAG_ADD,
				  &a, l,
				  fib->old_hash_values,
				  fib->new_hash_values);
	      }
	  }
      }));

      fib->new_hash_values[0] = ~0;
      for (i = 0; i < vec_len (to_delete); i++)
	{
	  hash = _hash_unset (hash, to_delete[i].data_u32, fib->old_hash_values);
	  vec_foreach (cb, im->add_del_route_callbacks)
	    if ((flags & cb->required_flags) == cb->required_flags)
	      cb->function (im, cb->function_opaque,
			    fib, flags | IP4_ROUTE_FLAG_DEL,
			    &a, l,
			    fib->old_hash_values,
			    fib->new_hash_values);
	}
    }

  /* All remaps have been performed. */
  lm->n_adjacency_remaps = 0;
}

void ip4_delete_matching_routes (ip4_main_t * im,
				 u32 table_index_or_table_id,
				 u32 flags,
				 ip4_address_t * address,
				 u32 address_length)
{
  static ip4_address_t * matching_addresses;
  static u8 * matching_address_lengths;
  u32 l, i;
  ip4_add_del_route_args_t a;

  a.flags = IP4_ROUTE_FLAG_DEL | IP4_ROUTE_FLAG_NO_REDISTRIBUTE | flags;
  a.table_index_or_table_id = table_index_or_table_id;
  a.adj_index = ~0;
  a.add_adj = 0;
  a.n_add_adj = 0;

  for (l = address_length + 1; l <= 32; l++)
    {
      ip4_foreach_matching_route (im, table_index_or_table_id, flags,
				  address,
				  l,
				  &matching_addresses,
				  &matching_address_lengths);
      for (i = 0; i < vec_len (matching_addresses); i++)
	{
	  a.dst_address = matching_addresses[i];
	  a.dst_address_length = matching_address_lengths[i];
	  ip4_add_del_route (im, &a);
	}
    }

  ip4_maybe_remap_adjacencies (im, table_index_or_table_id, flags);
}

/* Compute flow hash.  We'll use it to select which Sponge to use for this
   flow.  And other things. */
always_inline u32
ip4_compute_flow_hash (ip4_header_t * ip, u32 flow_hash_seed)
{
    tcp_header_t * tcp = (void *) (ip + 1);
    u32 a, b, c;
    uword is_tcp_udp = (ip->protocol == IP_PROTOCOL_TCP
			|| ip->protocol == IP_PROTOCOL_UDP);

    c = ip->dst_address.data_u32;
    b = ip->src_address.data_u32;
    a = is_tcp_udp ? tcp->ports.src_and_dst : 0;
    a ^= ip->protocol ^ flow_hash_seed;

    hash_v3_finalize32 (a, b, c);

    return c;
}

static uword
ip4_lookup (vlib_main_t * vm,
	    vlib_node_runtime_t * node,
	    vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
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
	  ip4_header_t * ip0, * ip1;
	  ip_adjacency_t * adj0, * adj1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->data, sizeof (ip0[0]), LOAD);
	    CLIB_PREFETCH (p3->data, sizeof (ip0[0]), LOAD);
	  }

	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  adj_index0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);
	  adj_index1 = ip4_fib_lookup (im, p1->sw_if_index[VLIB_RX], &ip1->dst_address, p1);

	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);

	  next0 = adj0->lookup_next_index;
	  next1 = adj1->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  i0->flow_hash = ip4_compute_flow_hash (ip0, im->flow_hash_seed);
	  i1->flow_hash = ip4_compute_flow_hash (ip1, im->flow_hash_seed);

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
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  u32 pi0, adj_index0;
	  ip_lookup_next_t next0;
	  ip_adjacency_t * adj0;

	  pi0 = from[0];
	  to_next[0] = pi0;

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);

	  adj_index0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);

	  adj0 = ip_get_adjacency (lm, adj_index0);

	  next0 = adj0->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);

	  i0->flow_hash = ip4_compute_flow_hash (ip0, im->flow_hash_seed);

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

void ip4_adjacency_set_interface_route (vlib_main_t * vm, ip_adjacency_t * adj,
					u32 sw_if_index,
					u32 if_address_index)
{
  vlib_hw_interface_t * hw = vlib_get_sup_hw_interface (vm, sw_if_index);
  ip_lookup_next_t n;
  u32 node_index;

  if (is_ethernet_interface (hw->hw_if_index))
    {
      n = IP_LOOKUP_NEXT_ARP;
      node_index = ip4_arp_node.index;
      adj->if_address_index = if_address_index;
    }
  else
    {
      n = IP_LOOKUP_NEXT_REWRITE;
      node_index = ip4_rewrite_node.index;
    }

  adj->lookup_next_index = n;
  vnet_rewrite_for_sw_interface
    (vm,
     VNET_L3_PACKET_TYPE_IP4,
     sw_if_index,
     node_index,
     &adj->rewrite_header,
     n == IP_LOOKUP_NEXT_REWRITE ? sizeof (adj->rewrite_data) : 0);
}

static void
ip4_add_interface_routes (vlib_main_t * vm, u32 sw_if_index,
			  ip4_main_t * im, u32 fib_index,
			  ip_interface_address_t * a)
{
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_adjacency_t * adj;
  ip4_address_t * address = ip_interface_address_get_address (lm, a);
  ip4_add_del_route_args_t x;
  vlib_hw_interface_t * hw_if = vlib_get_sup_hw_interface (vm, sw_if_index);

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  x.table_index_or_table_id = fib_index;
  x.flags = (IP4_ROUTE_FLAG_ADD
	     | IP4_ROUTE_FLAG_FIB_INDEX
	     | IP4_ROUTE_FLAG_NO_REDISTRIBUTE);
  x.dst_address = address[0];
  x.dst_address_length = a->address_length;
  x.n_add_adj = 0;
  x.add_adj = 0;

  if (a->address_length < 32)
    {
      adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			      &x.adj_index);
      ip4_adjacency_set_interface_route (vm, adj, sw_if_index, a - lm->if_address_pool);
      ip_call_add_del_adjacency_callbacks (lm, x.adj_index, /* is_del */ 0);
      ip4_add_del_route (im, &x);
    }

  /* Add e.g. 1.1.1.1/32 as local to this host. */
  adj = ip_add_adjacency (lm, /* template */ 0, /* block size */ 1,
			  &x.adj_index);
  adj->lookup_next_index = IP_LOOKUP_NEXT_LOCAL;
  adj->if_address_index = a - lm->if_address_pool;
  adj->rewrite_header.sw_if_index = sw_if_index;
  adj->rewrite_header.max_l3_packet_bytes = hw_if->max_l3_packet_bytes[VLIB_RX];

  ip_call_add_del_adjacency_callbacks (lm, x.adj_index, /* is_del */ 0);
  x.dst_address_length = 32;
  ip4_add_del_route (im, &x);
}

static void
ip4_del_interface_routes (ip4_main_t * im, u32 fib_index, ip4_address_t * address, u32 address_length)
{
  ip4_add_del_route_args_t x;

  /* Add e.g. 1.0.0.0/8 as interface route (arp for Ethernet). */
  x.table_index_or_table_id = fib_index;
  x.flags = (IP4_ROUTE_FLAG_DEL
	     | IP4_ROUTE_FLAG_FIB_INDEX
	     | IP4_ROUTE_FLAG_NO_REDISTRIBUTE);
  x.dst_address = address[0];
  x.dst_address_length = address_length;
  x.adj_index = ~0;
  x.n_add_adj = 0;
  x.add_adj = 0;

  if (address_length < 32)
    ip4_add_del_route (im, &x);

  x.dst_address_length = 32;
  ip4_add_del_route (im, &x);

  ip4_delete_matching_routes (im,
			      fib_index,
			      IP4_ROUTE_FLAG_FIB_INDEX,
			      address,
			      address_length);
}

typedef struct {
    u32 sw_if_index;
    ip4_address_t address;
    u32 length;
} ip4_interface_address_t;

static void serialize_vec_ip4_set_interface_address (serialize_main_t * m, va_list * va)
{
    ip4_interface_address_t * a = va_arg (*va, ip4_interface_address_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        serialize_integer (m, a[i].sw_if_index, sizeof (a[i].sw_if_index));
        serialize (m, serialize_ip4_address, &a[i].address);
        serialize_integer (m, a[i].length, sizeof (a[i].length));
    }
}

static void unserialize_vec_ip4_set_interface_address (serialize_main_t * m, va_list * va)
{
    ip4_interface_address_t * a = va_arg (*va, ip4_interface_address_t *);
    u32 n = va_arg (*va, u32);
    u32 i;
    for (i = 0; i < n; i++) {
        unserialize_integer (m, &a[i].sw_if_index, sizeof (a[i].sw_if_index));
        unserialize (m, unserialize_ip4_address, &a[i].address);
        unserialize_integer (m, &a[i].length, sizeof (a[i].length));
    }
}

static void serialize_ip4_set_interface_address_msg (serialize_main_t * m, va_list * va)
{
  ip4_interface_address_t * a = va_arg (*va, ip4_interface_address_t *);
  int is_del = va_arg (*va, int);
  serialize (m, serialize_vec_ip4_set_interface_address, a, 1);
  serialize_integer (m, is_del, sizeof (is_del));
}

static clib_error_t *
ip4_add_del_interface_address_internal (vlib_main_t * vm,
					u32 sw_if_index,
					ip4_address_t * new_address,
					u32 new_length,
					u32 redistribute,
					u32 insert_routes,
					u32 is_del);

static void unserialize_ip4_set_interface_address_msg (serialize_main_t * m, va_list * va)
{
  mc_main_t * mcm = va_arg (*va, mc_main_t *);
  vlib_main_t * vm = mcm->vlib_main;
  ip4_interface_address_t a;
  clib_error_t * error;
  int is_del;

  unserialize (m, unserialize_vec_ip4_set_interface_address, &a, 1);
  unserialize_integer (m, &is_del, sizeof (is_del));
  error = ip4_add_del_interface_address_internal
    (vm, a.sw_if_index, &a.address, a.length,
     /* redistribute */ 0,
     /* insert_routes */ 1,
     is_del);
  if (error)
    clib_error_report (error);
}

static MC_SERIALIZE_MSG (ip4_set_interface_address_msg) = {
  .name = "vnet_ip4_set_interface_address",
  .serialize = serialize_ip4_set_interface_address_msg,
  .unserialize = unserialize_ip4_set_interface_address_msg,
};

static clib_error_t *
ip4_add_del_interface_address_internal (vlib_main_t * vm,
					u32 sw_if_index,
					ip4_address_t * address,
					u32 address_length,
					u32 redistribute,
					u32 insert_routes,
					u32 is_del)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  clib_error_t * error = 0;
  u32 if_address_index;

  {
    uword elts_before = pool_elts (lm->if_address_pool);

    if (vm->mc_main && redistribute)
      {
	ip4_interface_address_t a;
	a.sw_if_index = sw_if_index;
	a.address = address[0];
	a.length = address_length;
	mc_serialize (vm->mc_main, &ip4_set_interface_address_msg, 
		      &a, (int)is_del);
	goto done;
      }

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

  if (vlib_sw_interface_is_admin_up (vm, sw_if_index) && insert_routes)
    {
      u32 fib_index = im->fib_index_by_sw_if_index[sw_if_index];

      if (is_del)
	ip4_del_interface_routes (im, fib_index, address, address_length);

      else
	ip4_add_interface_routes (vm, sw_if_index,
				  im, fib_index,
				  pool_elt_at_index (lm->if_address_pool, if_address_index));
    }

  {
    ip4_add_del_interface_address_callback_t * cb;
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
ip4_add_del_interface_address (vlib_main_t * vm, u32 sw_if_index,
			       ip4_address_t * address, u32 address_length,
			       u32 is_del)
{
  return ip4_add_del_interface_address_internal
    (vm, sw_if_index, address, address_length,
     /* redistribute */ 1,
     /* insert_routes */ 1,
     is_del);
}

static void serialize_ip4_fib (serialize_main_t * m, va_list * va)
{
  ip4_fib_t * f = va_arg (*va, ip4_fib_t *);
  u32 l, dst, adj_index;

  serialize_integer (m, f->table_id, sizeof (f->table_id));
  for (l = 0; l < ARRAY_LEN (f->adj_index_by_dst_address); l++)
    {
      u32 n_elts = hash_elts (f->adj_index_by_dst_address[l]);

      serialize_integer (m, n_elts, sizeof (n_elts));
      hash_foreach (dst, adj_index, f->adj_index_by_dst_address[l], ({
        ip4_address_t tmp;
        tmp.as_u32 = dst;
	serialize (m, serialize_ip4_address, &tmp);
        serialize_integer (m, adj_index, sizeof (adj_index));
      }));
    }
}

static void unserialize_ip4_fib (serialize_main_t * m, va_list * va)
{
  ip4_add_del_route_args_t a;
  u32 i;

  a.flags = (IP4_ROUTE_FLAG_ADD
             | IP4_ROUTE_FLAG_NO_REDISTRIBUTE
             | IP4_ROUTE_FLAG_TABLE_ID);
  a.n_add_adj = 0;
  a.add_adj = 0;

  unserialize_integer (m, &a.table_index_or_table_id,
                       sizeof (a.table_index_or_table_id));

  for (i = 0; i < STRUCT_ARRAY_LEN (ip4_fib_t, adj_index_by_dst_address); i++)
    {
      u32 n_elts;
      unserialize_integer (m, &n_elts, sizeof (u32));
      a.dst_address_length = i;
      while (n_elts > 0)
        {
          unserialize (m, unserialize_ip4_address, &a.dst_address);
          unserialize_integer (m, &a.adj_index, sizeof (a.adj_index));
          ip4_add_del_route (&ip4_main, &a);
          n_elts--;
        }
    }
}

void serialize_vnet_ip4_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  vlib_interface_main_t * vim = &vm->interface_main;
  vlib_sw_interface_t * si;
  ip4_main_t * i4m = &ip4_main;
  ip4_interface_address_t * as = 0, * a;

  /* Download adjacency tables & multipath stuff. */
  serialize (m, serialize_ip_lookup_main, &i4m->lookup_main);

  /* FIBs. */
  {
    ip4_fib_t * f;
    u32 n_fibs = vec_len (i4m->fibs);
    serialize_integer (m, n_fibs, sizeof (n_fibs));
    vec_foreach (f, i4m->fibs)
      serialize (m, serialize_ip4_fib, f);
  }

  /* FIB interface config. */
  vec_serialize (m, i4m->fib_index_by_sw_if_index, serialize_vec_32);

  /* Interface ip4 addresses. */
  pool_foreach (si, vim->sw_interfaces, ({
    u32 sw_if_index = si->sw_if_index;
    ip_interface_address_t * ia;
    foreach_ip_interface_address (&i4m->lookup_main, ia, sw_if_index, ({
      ip4_address_t * x = ip_interface_address_get_address (&i4m->lookup_main, ia);
      vec_add2 (as, a, 1);
      a->address = x[0];
      a->length = ia->address_length;
      a->sw_if_index = sw_if_index;
    }));
  }));
  vec_serialize (m, as, serialize_vec_ip4_set_interface_address);
  vec_free (as);
}

void unserialize_vnet_ip4_main (serialize_main_t * m, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  ip4_main_t * i4m = &ip4_main;
  ip4_interface_address_t * as = 0, * a;

  unserialize (m, unserialize_ip_lookup_main, &i4m->lookup_main);

  {
    ip_adjacency_t * adj;
    u32 n_adj;
    heap_foreach (adj, n_adj, i4m->lookup_main.adjacency_heap, ({
      unserialize_fixup_ip4_rewrite_adjacencies (vm, adj, n_adj);
    }));
  }

  /* FIBs */
  {
    u32 i, n_fibs;
    unserialize_integer (m, &n_fibs, sizeof (n_fibs));
    for (i = 0; i < n_fibs; i++)
      unserialize (m, unserialize_ip4_fib);
  }

  vec_unserialize (m, &i4m->fib_index_by_sw_if_index, unserialize_vec_32);

  vec_unserialize (m, &as, unserialize_vec_ip4_set_interface_address);
  vec_foreach (a, as) {
    ip4_add_del_interface_address_internal
      (vm, a->sw_if_index, &a->address, a->length,
       /* redistribute */ 0,
       /* insert_routes */ 0,
       /* is_del */ 0);
  }
  vec_free (as);
}

static clib_error_t *
ip4_sw_interface_admin_up_down (vlib_main_t * vm,
				u32 sw_if_index,
				u32 flags)
{
  ip4_main_t * im = &ip4_main;
  ip_interface_address_t * ia;
  ip4_address_t * a;
  u32 is_admin_up, fib_index;

  /* Fill in lookup tables with default table (0). */
  vec_validate (im->fib_index_by_sw_if_index, sw_if_index);

  vec_validate_init_empty (im->lookup_main.if_address_pool_index_by_sw_if_index, sw_if_index, ~0);

  is_admin_up = (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP) != 0;

  fib_index = vec_elt (im->fib_index_by_sw_if_index, sw_if_index);

  foreach_ip_interface_address (&im->lookup_main, ia, sw_if_index, ({
    a = ip_interface_address_get_address (&im->lookup_main, ia);
    if (is_admin_up)
      ip4_add_interface_routes (vm, sw_if_index,
				im, fib_index,
				ia);
    else
      ip4_del_interface_routes (im, fib_index,
				a, ia->address_length);
  }));

  return 0;
}

static clib_error_t *
ip4_sw_interface_add_del (vlib_main_t * vm,
			  u32 sw_if_index,
			  u32 is_add)
{
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u32 ci, cast;

  for (cast = 0; cast < VNET_N_CAST; cast++)
    {
      ip_config_main_t * cm = &lm->rx_config_mains[cast];
      vnet_config_main_t * vcm = &cm->config_main;

      if (! vcm->node_index_by_feature_index)
	{
	  if (cast == VNET_UNICAST)
	    {
	      static char * start_nodes[] = { "ip4-input", "ip4-input-no-checksum", };
	      static char * feature_nodes[] = {
		[IP4_RX_FEATURE_SOURCE_CHECK_REACHABLE_VIA_RX] = "ip4-source-check-via-rx",
		[IP4_RX_FEATURE_SOURCE_CHECK_REACHABLE_VIA_ANY] = "ip4-source-check-via-any",
		[IP4_RX_FEATURE_LOOKUP] = "ip4-lookup",
	      };

	      vnet_config_init (vm, vcm,
				start_nodes, ARRAY_LEN (start_nodes),
				feature_nodes, ARRAY_LEN (feature_nodes));
	    }
	  else
	    {
	      static char * start_nodes[] = { "ip4-input", "ip4-input-no-checksum", };
	      static char * feature_nodes[] = {
		[IP4_RX_FEATURE_LOOKUP] = "ip4-lookup-multicast",
	      };

	      vnet_config_init (vm, vcm,
				start_nodes, ARRAY_LEN (start_nodes),
				feature_nodes, ARRAY_LEN (feature_nodes));
	    }
	}

      vec_validate_init_empty (cm->config_index_by_sw_if_index, sw_if_index, ~0);
      ci = cm->config_index_by_sw_if_index[sw_if_index];

      if (is_add)
	ci = vnet_config_add_feature (vm, vcm,
				      ci,
				      IP4_RX_FEATURE_LOOKUP,
				      /* config data */ 0,
				      /* # bytes of config data */ 0);
      else
	ci = vnet_config_del_feature (vm, vcm,
				      ci,
				      IP4_RX_FEATURE_LOOKUP,
				      /* config data */ 0,
				      /* # bytes of config data */ 0);

      cm->config_index_by_sw_if_index[sw_if_index] = ci;
    }

  return /* no error */ 0;
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
  },

  .sw_interface_admin_up_down_function = ip4_sw_interface_admin_up_down,
  .sw_interface_add_del_function = ip4_sw_interface_add_del,
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

  ip_lookup_init (&im->lookup_main, /* is_ip6 */ 0);

  {
    pg_node_t * pn;
    pn = pg_get_node (ip4_lookup_node.index);
    pn->unformat_edit = unformat_pg_ip4_header;
  }

  {
    ethernet_and_arp_header_t h;

    memset (&h, 0, sizeof (h));

    /* Send to broadcast address ffff.ffff.ffff */
    memset (h.ethernet.dst_address, ~0, sizeof (h.ethernet.dst_address));

    /* Set target ethernet address to all zeros. */
    memset (h.arp.ip4_over_ethernet[1].ethernet, 0, sizeof (h.arp.ip4_over_ethernet[1].ethernet));

#define _16(f,v) h.f = clib_host_to_net_u16 (v);
#define _8(f,v) h.f = v;
    _16 (ethernet.type, ETHERNET_TYPE_ARP);
    _16 (arp.l2_type, ETHERNET_ARP_HARDWARE_TYPE_ethernet);
    _16 (arp.l3_type, ETHERNET_TYPE_IP4);
    _8 (arp.n_l2_address_bytes, 6);
    _8 (arp.n_l3_address_bytes, 4);
    _16 (arp.opcode, ETHERNET_ARP_OPCODE_request);
#undef _16
#undef _8

    vlib_packet_template_init (vm,
			       &im->ip4_arp_request_packet_template,
			       /* data */ &h,
			       sizeof (h),
			       /* alloc chunk size */ 8,
			       /* flags */ VNET_BUFFER_LOCALLY_GENERATED,
			       "ip4 arp");
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

/* Compute TCP/UDP/ICMP4 checksum in software. */
u16
ip4_tcp_udp_compute_checksum (vlib_main_t * vm, vlib_buffer_t * p0,
			      ip4_header_t * ip0)
{
  ip_csum_t sum0;
  u32 ip_header_length, payload_length_host_byte_order;
  u32 n_this_buffer, n_bytes_left;
  u16 sum16;
  void * data_this_buffer;
  
  /* Initialize checksum with ip header. */
  ip_header_length = ip4_header_bytes (ip0);
  payload_length_host_byte_order = clib_net_to_host_u16 (ip0->length) - ip_header_length;
  sum0 = clib_host_to_net_u32 (payload_length_host_byte_order + (ip0->protocol << 16));

  if (BITS (uword) == 32)
    {
      sum0 = ip_csum_with_carry (sum0, clib_mem_unaligned (&ip0->src_address, u32));
      sum0 = ip_csum_with_carry (sum0, clib_mem_unaligned (&ip0->dst_address, u32));
    }
  else
    sum0 = ip_csum_with_carry (sum0, clib_mem_unaligned (&ip0->src_address, u64));

  n_bytes_left = n_this_buffer = payload_length_host_byte_order;
  data_this_buffer = (void *) ip0 + ip_header_length;
  if (n_this_buffer + ip_header_length > p0->current_length)
    n_this_buffer = p0->current_length > ip_header_length ? p0->current_length - ip_header_length : 0;
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

static u32
ip4_tcp_udp_validate_checksum (vlib_main_t * vm, vlib_buffer_t * p0)
{
  ip4_header_t * ip0 = vlib_buffer_get_current (p0);
  udp_header_t * udp0;
  u16 sum16;

  ASSERT (ip0->protocol == IP_PROTOCOL_TCP
	  || ip0->protocol == IP_PROTOCOL_UDP);

  udp0 = (void *) (ip0 + 1);
  if (ip0->protocol == IP_PROTOCOL_UDP && udp0->checksum == 0)
    {
      p0->flags |= (IP_BUFFER_L4_CHECKSUM_COMPUTED
		    | IP_BUFFER_L4_CHECKSUM_CORRECT);
      return p0->flags;
    }

  sum16 = ip4_tcp_udp_compute_checksum (vm, p0, ip0);

  p0->flags |= (IP_BUFFER_L4_CHECKSUM_COMPUTED
		| ((sum16 == 0) << LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT));

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
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip4_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    ip4_forward_next_trace (vm, node, frame);

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
	{
	  vlib_buffer_t * p0, * p1;
	  ip_buffer_opaque_t * i0, * i1;
	  ip4_header_t * ip0, * ip1;
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

	  adj_index0 = i0->dst_adj_index;
	  adj_index1 = i1->dst_adj_index;

	  proto0 = ip0->protocol;
	  proto1 = ip1->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_udp1 = proto1 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;
	  is_tcp_udp1 = is_udp1 || proto1 == IP_PROTOCOL_TCP;

	  flags0 = p0->flags;
	  flags1 = p1->flags;

	  good_tcp_udp0 = (flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	  good_tcp_udp1 = (flags1 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;

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
		  if (is_tcp_udp0
		      && ! (flags0 & IP_BUFFER_L4_CHECKSUM_COMPUTED))
		    flags0 = ip4_tcp_udp_validate_checksum (vm, p0);
		  good_tcp_udp0 =
		    (flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
		}
	      if (is_tcp_udp1)
		{
		  if (is_tcp_udp1
		      && ! (flags1 & IP_BUFFER_L4_CHECKSUM_COMPUTED))
		    flags1 = ip4_tcp_udp_validate_checksum (vm, p1);
		  good_tcp_udp1 =
		    (flags1 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
		  good_tcp_udp1 |= is_udp1 && udp1->checksum == 0;
		}
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;
	  good_tcp_udp1 &= len_diff1 >= 0;

	  error0 = error1 = IP4_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP4_ERROR_UDP_LENGTH : error0;
	  error1 = len_diff1 < 0 ? IP4_ERROR_UDP_LENGTH : error1;

	  ASSERT (IP4_ERROR_TCP_CHECKSUM + 1 == IP4_ERROR_UDP_CHECKSUM);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? IP4_ERROR_TCP_CHECKSUM + is_udp0
		    : error0);
	  error1 = (is_tcp_udp1 && ! good_tcp_udp1
		    ? IP4_ERROR_TCP_CHECKSUM + is_udp1
		    : error1);

	  if (error0 == IP4_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index0 = ip4_src_lookup_for_packet (im, p0, ip0);
	      error0 = (lm->miss_adj_index == src_adj_index0
			? IP4_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }
	  if (error1 == IP4_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index1 = ip4_src_lookup_for_packet (im, p1, ip1);
	      error1 = (lm->miss_adj_index == src_adj_index1
			? IP4_ERROR_SRC_LOOKUP_MISS
			: error1);
	    }

	  next0 = lm->local_next_by_ip_protocol[proto0];
	  next1 = lm->local_next_by_ip_protocol[proto1];

	  next0 = error0 != IP4_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;
	  next1 = error1 != IP4_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next1;

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
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
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

	  adj_index0 = i0->dst_adj_index;

	  ip0 = vlib_buffer_get_current (p0);

	  proto0 = ip0->protocol;
	  is_udp0 = proto0 == IP_PROTOCOL_UDP;
	  is_tcp_udp0 = is_udp0 || proto0 == IP_PROTOCOL_TCP;

	  flags0 = p0->flags;

	  good_tcp_udp0 = (flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;

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
	      if (is_tcp_udp0
		  && ! (flags0 & IP_BUFFER_L4_CHECKSUM_COMPUTED))
		flags0 = ip4_tcp_udp_validate_checksum (vm, p0);
	      good_tcp_udp0 =
		(flags0 & IP_BUFFER_L4_CHECKSUM_CORRECT) != 0;
	      good_tcp_udp0 |= is_udp0 && udp0->checksum == 0;
	    }

	  good_tcp_udp0 &= len_diff0 >= 0;

	  error0 = IP4_ERROR_UNKNOWN_PROTOCOL;

	  error0 = len_diff0 < 0 ? IP4_ERROR_UDP_LENGTH : error0;

	  ASSERT (IP4_ERROR_TCP_CHECKSUM + 1 == IP4_ERROR_UDP_CHECKSUM);
	  error0 = (is_tcp_udp0 && ! good_tcp_udp0
		    ? IP4_ERROR_TCP_CHECKSUM + is_udp0
		    : error0);

	  next0 = lm->local_next_by_ip_protocol[proto0];

	  if (error0 == IP4_ERROR_UNKNOWN_PROTOCOL)
	    {
	      u32 src_adj_index0 = ip4_src_lookup_for_packet (im, p0, ip0);
	      error0 = (lm->miss_adj_index == src_adj_index0
			? IP4_ERROR_SRC_LOOKUP_MISS
			: error0);
	    }

	  next0 = error0 != IP4_ERROR_UNKNOWN_PROTOCOL ? IP_LOCAL_NEXT_DROP : next0;

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

static VLIB_REGISTER_NODE (ip4_local_node) = {
  .function = ip4_local,
  .name = "ip4-local",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = IP_LOCAL_N_NEXT,
  .next_nodes = {
    [IP_LOCAL_NEXT_DROP] = "error-drop",
    [IP_LOCAL_NEXT_PUNT] = "error-punt",
    [IP_LOCAL_NEXT_TCP_LOOKUP] = "ip4-tcp-lookup",
    [IP_LOCAL_NEXT_UDP_LOOKUP] = "ip4-udp-lookup",
    [IP_LOCAL_NEXT_ICMP] = "ip4-icmp-input",
  },
};

void ip4_register_protocol (u32 protocol, u32 node_index)
{
  vlib_main_t * vm = &vlib_global_main;
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;

  ASSERT (protocol < ARRAY_LEN (lm->local_next_by_ip_protocol));
  lm->local_next_by_ip_protocol[protocol] = vlib_node_add_next (vm, ip4_local_node.index, node_index);
}

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
      vlib_get_next_frame (vm, node, IP4_ARP_NEXT_DROP,
			   to_next_drop, n_left_to_next_drop);

      while (n_left_from > 0 && n_left_to_next_drop > 0)
	{
	  vlib_buffer_t * p0;
	  ip_buffer_opaque_t * i0;
	  ip4_header_t * ip0;
	  u32 pi0, adj_index0, a0, b0, c0, m0, sw_if_index0, drop0;
	  uword bm0;
	  ip_adjacency_t * adj0;

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

	  /* Mark it as seen. */
	  hash_bitmap[c0] = bm0 | m0;

	  from += 1;
	  n_left_from -= 1;
	  to_next_drop[0] = pi0;
	  to_next_drop += 1;
	  n_left_to_next_drop -= 1;

	  p0->error = node->errors[drop0 ? IP4_ARP_ERROR_DROP : IP4_ARP_ERROR_REQUEST_SENT];

	  if (drop0)
	    continue;

	  {
	    u32 bi0;
	    vlib_buffer_t * b0;
	    ethernet_and_arp_header_t * h0;
	    vlib_sw_interface_t * swif0;
	    ethernet_interface_t * eif0;
	    u8 * eth_addr0;
	    static u8 zero[6];

	    h0 = vlib_packet_template_get_packet (vm, &im->ip4_arp_request_packet_template, &bi0);

	    swif0 = vlib_get_sup_sw_interface (vm, sw_if_index0);
	    ASSERT (swif0->type == VLIB_SW_INTERFACE_TYPE_HARDWARE);
	    eif0 = ethernet_get_interface (&ethernet_main, swif0->hw_if_index);
	    eth_addr0 = eif0 ? eif0->address : zero;
	    memcpy (h0->ethernet.src_address, eth_addr0, sizeof (h0->ethernet.src_address));
	    memcpy (h0->arp.ip4_over_ethernet[0].ethernet, eth_addr0, sizeof (h0->arp.ip4_over_ethernet[0].ethernet));

	    ip4_src_address_for_packet (im, p0, &h0->arp.ip4_over_ethernet[0].ip4, sw_if_index0);

	    /* Copy in destination address we are requesting. */
	    h0->arp.ip4_over_ethernet[1].ip4.data_u32 = ip0->dst_address.data_u32;

	    vlib_buffer_copy_trace_flag (vm, p0, bi0);
	    b0 = vlib_get_buffer (vm, bi0);
	    b0->sw_if_index[VLIB_TX] = p0->sw_if_index[VLIB_TX];

	    vlib_set_next_frame_buffer (vm, node, adj0->rewrite_header.next_index, bi0);
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
    [IP4_ARP_NEXT_DROP] = "error-drop",
  },
};

/* Send an ARP request to see if given destination is reachable on given interface. */
clib_error_t *
ip4_probe_neighbor (vlib_main_t * vm, ip4_address_t * dst, u32 sw_if_index)
{
  ip4_main_t * im = &ip4_main;
  u32 bi;
  ethernet_and_arp_header_t * h;
  vlib_hw_interface_t * hi;
  ethernet_interface_t * eif;
  u8 * eth_addr;
  static u8 zero[6];
  ip4_address_t * src;

  src = ip4_interface_address_matching_destination (im, dst, sw_if_index, 0);
  if (! src)
    return clib_error_return (0, "no matching interface address for destination %U (interface %U)",
			      format_ip4_address, dst,
			      format_vlib_sw_if_index_name, vm, sw_if_index);

  h = vlib_packet_template_get_packet (vm, &im->ip4_arp_request_packet_template, &bi);

  hi = vlib_get_sup_hw_interface (vm, sw_if_index);

  eif = ethernet_get_interface (&ethernet_main, hi->hw_if_index);
  eth_addr = eif ? eif->address : zero;
  memcpy (h->ethernet.src_address, eth_addr, sizeof (h->ethernet.src_address));
  memcpy (h->arp.ip4_over_ethernet[0].ethernet, eth_addr, sizeof (h->arp.ip4_over_ethernet[0].ethernet));

  h->arp.ip4_over_ethernet[0].ip4 = src[0];
  h->arp.ip4_over_ethernet[1].ip4 = dst[0];

  {
    vlib_buffer_t * b = vlib_get_buffer (vm, bi);
    b->sw_if_index[VLIB_RX] = b->sw_if_index[VLIB_TX] = sw_if_index;
  }

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
  IP4_REWRITE_NEXT_DROP,
} ip4_rewrite_next_t;

static uword
ip4_rewrite (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
  ip_lookup_main_t * lm = &ip4_main.lookup_main;
  u32 * from = vlib_frame_vector_args (frame);
  u32 n_left_from, n_left_to_next, * to_next, next_index;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip4_input_node.index);

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
	  u32 pi0, rw_len0, next0, error0, checksum0, adj_index0;
	  u32 pi1, rw_len1, next1, error1, checksum1, adj_index1;
      
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

	  error0 = error1 = IP4_ERROR_NONE;

	  /* Decrement TTL & update checksum.
	     Works either endian, so no need for byte swap. */
	  {
	    i32 ttl0 = ip0->ttl, ttl1 = ip1->ttl;
	    u8 decrement0 = (p0->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;
	    u8 decrement1 = (p1->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;

	    /* Input node should have reject packets with ttl 0. */
	    ASSERT (ip0->ttl > 0);
	    ASSERT (ip1->ttl > 0);

	    checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);
	    checksum1 = ip1->checksum + clib_host_to_net_u16 (0x0100);

	    checksum0 += checksum0 >= 0xffff;
	    checksum1 += checksum1 >= 0xffff;

	    checksum0 = decrement0 ? checksum0 : ip0->checksum;
	    checksum1 = decrement1 ? checksum1 : ip1->checksum;

	    ip0->checksum = checksum0;
	    ip1->checksum = checksum1;

	    ttl0 -= decrement0;
	    ttl1 -= decrement1;

	    ip0->ttl = ttl0;
	    ip1->ttl = ttl1;

	    error0 = ttl0 <= 0 ? IP4_ERROR_TIME_EXPIRED : error0;
	    error1 = ttl1 <= 0 ? IP4_ERROR_TIME_EXPIRED : error1;

	    /* Verify checksum. */
	    ASSERT (ip0->checksum == ip4_header_checksum (ip0));
	    ASSERT (ip1->checksum == ip4_header_checksum (ip1));
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
		    ? IP4_ERROR_MTU_EXCEEDED
		    : error0);
	  error1 = (vlib_buffer_length_in_chain (vm, p1) > adj1[0].rewrite_header.max_l3_packet_bytes
		    ? IP4_ERROR_MTU_EXCEEDED
		    : error1);

	  p0->current_data -= rw_len0;
	  p1->current_data -= rw_len1;

	  p0->current_length += rw_len0;
	  p1->current_length += rw_len1;

	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
	  p1->sw_if_index[VLIB_TX] = adj1[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;
	  next1 = adj1[0].rewrite_header.next_index;

	  p0->error = error_node->errors[error0];
	  p1->error = error_node->errors[error1];

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
	  ip4_header_t * ip0;
	  u32 pi0, rw_len0, adj_index0, next0, error0, checksum0;
      
	  pi0 = to_next[0] = from[0];

	  p0 = vlib_get_buffer (vm, pi0);
	  i0 = vlib_get_buffer_opaque (p0);

	  adj_index0 = i0->dst_adj_index;
	  adj0 = ip_get_adjacency (lm, adj_index0);
      
	  ip0 = vlib_buffer_get_current (p0);

	  error0 = IP4_ERROR_NONE;

	  /* Decrement TTL & update checksum. */
	  {
	    i32 ttl0 = ip0->ttl;
	    u8 decrement0 = (p0->flags & VNET_BUFFER_LOCALLY_GENERATED) ? 0 : 1;

	    checksum0 = ip0->checksum + clib_host_to_net_u16 (0x0100);

	    checksum0 += checksum0 >= 0xffff;

	    checksum0 = decrement0 ? checksum0 : ip0->checksum;

	    ip0->checksum = checksum0;

	    ASSERT (ip0->ttl > 0);

	    ttl0 -= decrement0;

	    ip0->ttl = ttl0;

	    ASSERT (ip0->checksum == ip4_header_checksum (ip0));

	    error0 = ttl0 <= 0 ? IP4_ERROR_TIME_EXPIRED : error0;
	  }

	  p0->error = error_node->errors[error0];

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
		    ? IP4_ERROR_MTU_EXCEEDED
		    : error0);

	  p0->current_data -= rw_len0;
	  p0->current_length += rw_len0;
	  p0->sw_if_index[VLIB_TX] = adj0[0].rewrite_header.sw_if_index;
      
	  next0 = adj0[0].rewrite_header.next_index;

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

static clib_error_t *
add_del_interface_table (vlib_main_t * vm,
			 unformat_input_t * input,
			 vlib_cli_command_t * cmd)
{
  clib_error_t * error = 0;
  u32 sw_if_index, table_id, is_del;

  sw_if_index = ~0;
  is_del = 0;

  if (unformat (input, "del"))
    is_del = 1;

  if (! unformat_user (input, unformat_vlib_sw_interface, vm, &sw_if_index))
    {
      error = clib_error_return (0, "unknown interface `%U'",
				 format_unformat_error, input);
      goto done;
    }

  if (unformat (input, "%d", &table_id))
    ;
  else
    {
      error = clib_error_return (0, "expected table id `%U'",
				 format_unformat_error, input);
      goto done;
    }

  {
    ip4_main_t * im = &ip4_main;
    ip4_fib_t * fib = find_fib_by_table_index_or_id (im, table_id, IP4_ROUTE_FLAG_TABLE_ID);

    if (fib)
      im->fib_index_by_sw_if_index[sw_if_index] = fib->index;
  }

 done:
  return error;
}

static VLIB_CLI_COMMAND (set_interface_ip_table_command) = {
  .path = "set interface ip table",
  .function = add_del_interface_table,
  .short_help = "Add/delete FIB table id for interface",
};

static uword
ip4_lookup_multicast (vlib_main_t * vm,
		      vlib_node_runtime_t * node,
		      vlib_frame_t * frame)
{
  ip4_main_t * im = &ip4_main;
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
	  ip4_header_t * ip0, * ip1;
	  ip_adjacency_t * adj0, * adj1;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t * p2, * p3;

	    p2 = vlib_get_buffer (vm, from[2]);
	    p3 = vlib_get_buffer (vm, from[3]);

	    vlib_prefetch_buffer_header (p2, LOAD);
	    vlib_prefetch_buffer_header (p3, LOAD);

	    CLIB_PREFETCH (p2->data, sizeof (ip0[0]), LOAD);
	    CLIB_PREFETCH (p3->data, sizeof (ip0[0]), LOAD);
	  }

	  pi0 = to_next[0] = from[0];
	  pi1 = to_next[1] = from[1];

	  p0 = vlib_get_buffer (vm, pi0);
	  p1 = vlib_get_buffer (vm, pi1);

	  ip0 = vlib_buffer_get_current (p0);
	  ip1 = vlib_buffer_get_current (p1);

	  adj_index0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);
	  adj_index1 = ip4_fib_lookup (im, p1->sw_if_index[VLIB_RX], &ip1->dst_address, p1);

	  adj0 = ip_get_adjacency (lm, adj_index0);
	  adj1 = ip_get_adjacency (lm, adj_index1);

	  next0 = adj0->lookup_next_index;
	  next1 = adj1->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);
	  i1 = vlib_get_buffer_opaque (p1);

	  i0->flow_hash = ip4_compute_flow_hash (ip0, im->flow_hash_seed);
	  i1->flow_hash = ip4_compute_flow_hash (ip1, im->flow_hash_seed);

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
	  ip4_header_t * ip0;
	  ip_buffer_opaque_t * i0;
	  u32 pi0, adj_index0;
	  ip_lookup_next_t next0;
	  ip_adjacency_t * adj0;

	  pi0 = from[0];
	  to_next[0] = pi0;

	  p0 = vlib_get_buffer (vm, pi0);

	  ip0 = vlib_buffer_get_current (p0);

	  adj_index0 = ip4_fib_lookup (im, p0->sw_if_index[VLIB_RX], &ip0->dst_address, p0);

	  adj0 = ip_get_adjacency (lm, adj_index0);

	  next0 = adj0->lookup_next_index;

	  i0 = vlib_get_buffer_opaque (p0);

	  i0->flow_hash = ip4_compute_flow_hash (ip0, im->flow_hash_seed);

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

static VLIB_REGISTER_NODE (ip4_lookup_multicast_node) = {
  .function = ip4_lookup_multicast,
  .name = "ip4-lookup-multicast",
  .vector_size = sizeof (u32),

  .n_next_nodes = IP_LOOKUP_N_NEXT,
  .next_nodes = {
    [IP_LOOKUP_NEXT_MISS] = "ip4-miss",
    [IP_LOOKUP_NEXT_DROP] = "ip4-drop",
    [IP_LOOKUP_NEXT_PUNT] = "ip4-punt",
    [IP_LOOKUP_NEXT_LOCAL] = "ip4-local",
    [IP_LOOKUP_NEXT_ARP] = "ip4-arp",
    [IP_LOOKUP_NEXT_REWRITE] = "ip4-rewrite",
  },
};

static VLIB_REGISTER_NODE (ip4_multicast_node) = {
  .function = ip4_drop,
  .name = "ip4-multicast",
  .vector_size = sizeof (u32),

  .format_trace = format_ip4_forward_next_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};
