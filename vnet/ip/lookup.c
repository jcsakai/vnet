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

#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ppp/ppp.h>

static always_inline void
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

  if (hw->hw_class == &ethernet_hw_interface_class)
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
      if (hw->hw_class == &ppp_hw_interface_class)
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
      u32 adj_index;

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
			   r->address, r->address_length,
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
