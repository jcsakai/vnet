/*
 * ip/ip6_neighbor.c: IP6 neighbor handling
 *
 * Copyright (c) 2010 Cisco Systems
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
#include <clib/mhash.h>

typedef struct {
  ip6_address_t ip6_address;
  u32 sw_if_index;
} ip6_neighbor_key_t;

typedef struct {
  ip6_neighbor_key_t key;
  u8 link_layer_address[8];
  u64 cpu_time_last_updated;
} ip6_neighbor_t;

typedef struct {
  /* Hash tables mapping name to opcode. */
  uword * opcode_by_name;

  u32 * neighbor_input_next_index_by_hw_if_index;

  ip6_neighbor_t * neighbor_pool;

  mhash_t neighbor_index_by_key;
} ip6_neighbor_main_t;

static ip6_neighbor_main_t ip6_neighbor_main;

static u8 * format_ip6_neighbor_ip6_entry (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  ip6_neighbor_t * n = va_arg (*va, ip6_neighbor_t *);
  vlib_sw_interface_t * si;

  if (! n)
    return format (s, "%=12s%=20s%=20s%=40s", "Time", "Address", "Link layer", "Interface");

  si = vlib_get_sw_interface (vm, n->key.sw_if_index);
  s = format (s, "%=12U%=20U%=20U%=40U",
	      format_vlib_cpu_time, vm, n->cpu_time_last_updated,
	      format_ip6_address, &n->key.ip6_address,
	      format_ethernet_address, n->link_layer_address,
	      format_vlib_sw_interface_name, vm, si);

  return s;
}

static clib_error_t *
ip6_neighbor_sw_interface_up_down (vlib_main_t * vm,
				   u32 sw_if_index,
				   u32 flags)
{
  ip6_neighbor_main_t * nm = &ip6_neighbor_main;
  ip6_neighbor_t * n;

  if (! (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP))
    {
      u32 i, * to_delete = 0;

      pool_foreach (n, nm->neighbor_pool, ({
	if (n->key.sw_if_index == sw_if_index)
	  vec_add1 (to_delete, n - nm->neighbor_pool);
      }));

      for (i = 0; i < vec_len (to_delete); i++)
	{
	  n = pool_elt_at_index (nm->neighbor_pool, to_delete[i]);
	  mhash_unset (&nm->neighbor_index_by_key, &n->key, 0);
	  pool_put (nm->neighbor_pool, n);
	}

      vec_free (to_delete);
    }

  return 0;
}

static void
set_ethernet_neighbor (vlib_main_t * vm,
		       ip6_neighbor_main_t * nm,
		       u32 sw_if_index,
		       ip6_address_t * a,
		       u8 * link_layer_address)
{
  ip6_neighbor_key_t k;
  ip6_neighbor_t * n;
  ip6_main_t * im = &ip6_main;
  uword * p;
  ethernet_header_t * eth;

  k.sw_if_index = sw_if_index;
  k.ip6_address = a[0];

  p = mhash_get (&nm->neighbor_index_by_key, &k);
  if (p)
    n = pool_elt_at_index (nm->neighbor_pool, p[0]);
  else
    {
      ip6_add_del_route_args_t args;
      ip_adjacency_t adj;

      adj.lookup_next_index = IP_LOOKUP_NEXT_REWRITE;

      vnet_rewrite_for_sw_interface
	(vm,
	 VNET_L3_PACKET_TYPE_IP6,
	 sw_if_index,
	 ip6_rewrite_node.index,
	 &adj.rewrite_header,
	 sizeof (adj.rewrite_data));

      /* Copy in destination ethernet address from ARP. */
      eth = vnet_rewrite_get_data (adj);
      memcpy (eth->dst_address, link_layer_address, sizeof (eth->dst_address));

      args.table_index_or_table_id = im->fib_index_by_sw_if_index[sw_if_index];
      args.flags = IP6_ROUTE_FLAG_FIB_INDEX | IP6_ROUTE_FLAG_ADD;
      args.dst_address = a[0];
      args.dst_address_length = 128;
      args.adj_index = ~0;
      args.add_adj = &adj;
      args.n_add_adj = 1;

      ip6_add_del_route (im, &args);
      pool_get (nm->neighbor_pool, n);
      mhash_set (&nm->neighbor_index_by_key, &k, n - nm->neighbor_pool,
		 /* old value */ 0);
      n->key = k;
    }

  /* Update time stamp and ethernet address. */
  memcpy (n->link_layer_address, link_layer_address, sizeof (eth->dst_address));
  n->cpu_time_last_updated = clib_cpu_time_now ();
}

static int
ip6_neighbor_sort (vlib_main_t * vm,
		   ip6_neighbor_t * n1, ip6_neighbor_t * n2)
{
  int cmp;
  cmp = vlib_sw_interface_compare (vm, n1->key.sw_if_index, n2->key.sw_if_index);
  if (! cmp)
    cmp = ip6_address_compare (&n1->key.ip6_address, &n2->key.ip6_address);
  return cmp;
}

static clib_error_t *
show_ip6_neighbors (vlib_main_t * vm,
		    unformat_input_t * input,
		    vlib_cli_command_t * cmd)
{
  ip6_neighbor_main_t * nm = &ip6_neighbor_main;
  ip6_neighbor_t * n, * ns;
  clib_error_t * error = 0;
  u32 sw_if_index;

  /* Filter entries by interface if given. */
  sw_if_index = ~0;
  unformat_user (input, unformat_vlib_sw_interface, vm, &sw_if_index);

  ns = 0;
  pool_foreach (n, nm->neighbor_pool, ({ vec_add1 (ns, n[0]); }));
  vec_sort (ns, n1, n2, ip6_neighbor_sort (vm, n1, n2));
  vlib_cli_output (vm, "%U", format_ip6_neighbor_ip6_entry, vm, 0);
  vec_foreach (n, ns) {
    if (sw_if_index != ~0 && n->key.sw_if_index != sw_if_index)
      continue;
    vlib_cli_output (vm, "%U", format_ip6_neighbor_ip6_entry, vm, n);
  }
  vec_free (ns);

  return error;
}

static VLIB_CLI_COMMAND (show_ip6_neighbors_command) = {
  .name = "neighbors",
  .function = show_ip6_neighbors,
  .short_help = "Show ip6 neighbors",
  .parent = &vlib_cli_show_ip6_command,
};

typedef enum {
  ICMP6_NEIGHBOR_SOLICITATION_NEXT_DROP,
  ICMP6_NEIGHBOR_SOLICITATION_NEXT_REPLY,
  ICMP6_NEIGHBOR_SOLICITATION_N_NEXT,
} icmp6_neighbor_solicitation_or_advertisement_next_t;

static_always_inline uword
icmp6_neighbor_solicitation_or_advertisement (vlib_main_t * vm,
					      vlib_node_runtime_t * node,
					      vlib_frame_t * frame,
					      uword is_solicitation)
{
  ip6_main_t * im = &ip6_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip6_neighbor_main_t * nm = &ip6_neighbor_main;
  uword n_packets = frame->n_vectors;
  u32 * from, * to_next;
  u32 n_left_from, n_left_to_next, next_index, n_advertisements_sent;
  icmp6_neighbor_discovery_option_type_t option_type;
  vlib_node_runtime_t * error_node = vlib_node_get_runtime (vm, ip6_icmp_input_node.index);

  from = vlib_frame_vector_args (frame);
  n_left_from = n_packets;
  next_index = node->cached_next_index;
  
  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (icmp6_input_trace_t));

  option_type = 
    (is_solicitation
     ? ICMP6_NEIGHBOR_DISCOVERY_OPTION_source_link_layer_address
     : ICMP6_NEIGHBOR_DISCOVERY_OPTION_target_link_layer_address);
  n_advertisements_sent = 0;

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  ip6_header_t * ip0;
	  icmp6_neighbor_solicitation_or_advertisement_header_t * h0;
	  icmp6_neighbor_discovery_ethernet_link_layer_address_option_t * o0;
	  u32 bi0, options_len0, sw_if_index0, next0, error0;
      
	  bi0 = to_next[0] = from[0];

	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;
      
	  p0 = vlib_get_buffer (vm, bi0);
	  ip0 = vlib_buffer_get_current (p0);
	  h0 = ip6_next_header (ip0);
	  options_len0 = clib_net_to_host_u16 (ip0->payload_length) - sizeof (h0[0]);

	  error0 = ICMP6_ERROR_NONE;

	  sw_if_index0 = p0->sw_if_index[VLIB_RX];

	  /* Check that source address is unspecified, link-local or else on-link. */
	  if (! ip6_address_is_unspecified (&ip0->src_address)
	      && ! ip6_address_is_link_local_unicast (&ip0->src_address))
	    {
	      u32 src_adj_index0 = ip6_src_lookup_for_packet (im, p0, ip0);
	      ip_adjacency_t * adj0 = ip_get_adjacency (&im->lookup_main, src_adj_index0);

	      error0 = (adj0->rewrite_header.sw_if_index != sw_if_index0
			? ICMP6_ERROR_NEIGHBOR_SOLICITATION_SOURCE_NOT_ON_LINK
			: error0);
	  }
	      
	  o0 = (void *) (h0 + 1);
	  o0 = ((options_len0 == 8
		 && o0->header.type == option_type
		 && o0->header.n_data_u64s == 1)
		? o0
		: 0);

	  if (PREDICT_TRUE (error0 == ICMP6_ERROR_NONE && o0 != 0))
	    set_ethernet_neighbor (vm, nm, sw_if_index0,
				   is_solicitation ? &ip0->src_address : &h0->target_address,
				   o0->ethernet_address);

	  if (is_solicitation && error0 == ICMP6_ERROR_NONE)
	    {
	      /* Check that target address is one that we know about. */
	      ip_interface_address_t * ia0;
	      ia0 = ip_get_interface_address (lm, &h0->target_address);
	      error0 = ia0 == 0 ? ICMP6_ERROR_NEIGHBOR_SOLICITATION_SOURCE_UNKNOWN : error0;
	    }

	  if (is_solicitation)
	    next0 = (error0 != ICMP6_ERROR_NONE
		     ? ICMP6_NEIGHBOR_SOLICITATION_NEXT_DROP
		     : ICMP6_NEIGHBOR_SOLICITATION_NEXT_REPLY);
	  else
	    {
	      next0 = 0;
	      error0 = error0 == ICMP6_ERROR_NONE ? ICMP6_ERROR_NEIGHBOR_ADVERTISEMENTS_RX : error0;
	    }

	  if (is_solicitation && error0 == ICMP6_ERROR_NONE)
	    {
	      vlib_sw_interface_t * sw_if0;
	      ethernet_interface_t * eth_if0;

	      ip0->dst_address = ip0->src_address;
	      ip0->src_address = h0->target_address;
	      h0->icmp.type = ICMP6_neighbor_advertisement;

	      sw_if0 = vlib_get_sup_sw_interface (vm, sw_if_index0);
	      ASSERT (sw_if0->type == VLIB_SW_INTERFACE_TYPE_HARDWARE);
	      eth_if0 = ethernet_get_interface (&ethernet_main, sw_if0->hw_if_index);
	      if (eth_if0 && o0)
		{
		  memcpy (o0->ethernet_address, eth_if0->address, sizeof (o0->ethernet_address));
		  o0->header.type = ICMP6_NEIGHBOR_DISCOVERY_OPTION_target_link_layer_address;
		}

	      h0->advertisement_flags = clib_host_to_net_u32
		(ICMP6_NEIGHBOR_ADVERTISEMENT_FLAG_SOLICITED
		 | ICMP6_NEIGHBOR_ADVERTISEMENT_FLAG_OVERRIDE);

	      /* Don't want forwarding code to decrement hop_limit. */
	      p0->flags |= VNET_BUFFER_LOCALLY_GENERATED;

	      h0->icmp.checksum = 0;
	      h0->icmp.checksum = ip6_tcp_udp_icmp_compute_checksum (vm, p0, ip0);

	      n_advertisements_sent++;
	    }

	  p0->error = error_node->errors[error0];

	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Account for advertisements sent. */
  vlib_error_count (vm, error_node->node_index, ICMP6_ERROR_NEIGHBOR_ADVERTISEMENTS_TX, n_advertisements_sent);

  return frame->n_vectors;
}

static uword
icmp6_neighbor_solicitation (vlib_main_t * vm,
			     vlib_node_runtime_t * node,
			     vlib_frame_t * frame)
{ return icmp6_neighbor_solicitation_or_advertisement (vm, node, frame, /* is_solicitation */ 1); }

static uword
icmp6_neighbor_advertisement (vlib_main_t * vm,
			      vlib_node_runtime_t * node,
			      vlib_frame_t * frame)
{ return icmp6_neighbor_solicitation_or_advertisement (vm, node, frame, /* is_solicitation */ 0); }

static VLIB_REGISTER_NODE (ip6_icmp_neighbor_solicitation_node) = {
  .function = icmp6_neighbor_solicitation,
  .name = "icmp6-neighbor-solicitation",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp6_input_trace,

  .sw_interface_admin_up_down_function = ip6_neighbor_sw_interface_up_down,

  .n_next_nodes = ICMP6_NEIGHBOR_SOLICITATION_N_NEXT,
  .next_nodes = {
    [ICMP6_NEIGHBOR_SOLICITATION_NEXT_DROP] = "error-drop",
    [ICMP6_NEIGHBOR_SOLICITATION_NEXT_REPLY] = DEBUG > 0 ? "ip6-input" : "ip6-lookup",
  },
};

static VLIB_REGISTER_NODE (ip6_icmp_neighbor_advertisement_node) = {
  .function = icmp6_neighbor_advertisement,
  .name = "icmp6-neighbor-advertisement",

  .vector_size = sizeof (u32),

  .format_trace = format_icmp6_input_trace,

  .n_next_nodes = 1,
  .next_nodes = {
    [0] = "error-drop",
  },
};

static clib_error_t * ip6_neighbor_init (vlib_main_t * vm)
{
  ip6_neighbor_main_t * nm = &ip6_neighbor_main;

  mhash_init (&nm->neighbor_index_by_key,
	      /* value size */ sizeof (uword),
	      /* key size */ sizeof (ip6_neighbor_key_t));

  icmp6_register_type (vm, ICMP6_neighbor_solicitation, ip6_icmp_neighbor_solicitation_node.index);
  icmp6_register_type (vm, ICMP6_neighbor_advertisement, ip6_icmp_neighbor_advertisement_node.index);

  return 0;
}

VLIB_INIT_FUNCTION (ip6_neighbor_init);
