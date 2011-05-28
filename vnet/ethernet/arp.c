/*
 * ethernet/arp.c: IP v4 ARP node
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
#include <vnet/ethernet/arp_packet.h>
#include <clib/mhash.h>

typedef struct {
  u32 sw_if_index;
  ip4_address_t ip4_address;
} ethernet_arp_ip4_key_t;

typedef struct {
  ethernet_arp_ip4_key_t key;
  u8 ethernet_address[6];

  u16 flags;
#define ETHERNET_ARP_IP4_ENTRY_FLAG_STATIC (1 << 0)

  u64 cpu_time_last_updated;
} ethernet_arp_ip4_entry_t;

typedef struct {
  /* Hash tables mapping name to opcode. */
  uword * opcode_by_name;

  u32 * arp_input_next_index_by_hw_if_index;

  ethernet_arp_ip4_entry_t * ip4_entry_pool;

  mhash_t ip4_entry_by_key;
} ethernet_arp_main_t;

static ethernet_arp_main_t ethernet_arp_main;

static u8 * format_ethernet_arp_hardware_type (u8 * s, va_list * va)
{
  ethernet_arp_hardware_type_t h = va_arg (*va, ethernet_arp_hardware_type_t);
  char * t = 0;
  switch (h)
    {
#define _(n,f) case n: t = #f; break;
      foreach_ethernet_arp_hardware_type;
#undef _

    default:
      return format (s, "unknown 0x%x", h);
    }

  return format (s, "%s", t);
}

static u8 * format_ethernet_arp_opcode (u8 * s, va_list * va)
{
  ethernet_arp_opcode_t o = va_arg (*va, ethernet_arp_opcode_t);
  char * t = 0;
  switch (o)
    {
#define _(f) case ETHERNET_ARP_OPCODE_##f: t = #f; break;
      foreach_ethernet_arp_opcode;
#undef _

    default:
      return format (s, "unknown 0x%x", o);
    }

  return format (s, "%s", t);
}

static uword
unformat_ethernet_arp_opcode_host_byte_order (unformat_input_t * input,
					      va_list * args)
{
  int * result = va_arg (*args, int *);
  ethernet_arp_main_t * am = &ethernet_arp_main;
  int x, i;

  /* Numeric opcode. */
  if (unformat (input, "0x%x", &x)
      || unformat (input, "%d", &x))
    {
      if (x >= (1 << 16))
	return 0;
      *result = x;
      return 1;
    }

  /* Named type. */
  if (unformat_user (input, unformat_vlib_number_by_name,
		     am->opcode_by_name, &i))
    {
      *result = i;
      return 1;
    }

  return 0;
}

static uword
unformat_ethernet_arp_opcode_net_byte_order (unformat_input_t * input,
					     va_list * args)
{
  int * result = va_arg (*args, int *);
  if (! unformat_user (input, unformat_ethernet_arp_opcode_host_byte_order, result))
    return 0;

  *result = clib_host_to_net_u16 ((u16) *result);
  return 1;
}

static u8 * format_ethernet_arp_header (u8 * s, va_list * va)
{
  ethernet_arp_header_t * a = va_arg (*va, ethernet_arp_header_t *);
  u32 max_header_bytes = va_arg (*va, u32);
  uword indent;
  u16 l2_type, l3_type;

  if (max_header_bytes != 0 && sizeof (a[0]) > max_header_bytes)
    return format (s, "ARP header truncated");

  l2_type = clib_net_to_host_u16 (a->l2_type);
  l3_type = clib_net_to_host_u16 (a->l3_type);

  indent = format_get_indent (s);

  s = format (s, "%U, type %U/%U, address size %d/%d",
	      format_ethernet_arp_opcode, clib_net_to_host_u16 (a->opcode),
	      format_ethernet_arp_hardware_type, l2_type,
	      format_ethernet_type, l3_type,
	      a->n_l2_address_bytes, a->n_l3_address_bytes);
	      
  if (l2_type == ETHERNET_ARP_HARDWARE_TYPE_ethernet
      && l3_type == ETHERNET_TYPE_IP4)
    {
      s = format (s, "\n%U%U/%U -> %U/%U",
		  format_white_space, indent,
		  format_ethernet_address, a->ip4_over_ethernet[0].ethernet,
		  format_ip4_address, &a->ip4_over_ethernet[0].ip4,
		  format_ethernet_address, a->ip4_over_ethernet[1].ethernet,
		  format_ip4_address, &a->ip4_over_ethernet[1].ip4);
    }
  else
    {
      uword n2 = a->n_l2_address_bytes;
      uword n3 = a->n_l3_address_bytes;
      s = format (s, "\n%U%U/%U -> %U/%U",
		  format_white_space, indent,
		  format_hex_bytes, a->data + 0*n2 + 0*n3, n2,
		  format_hex_bytes, a->data + 1*n2 + 0*n3, n3,
		  format_hex_bytes, a->data + 1*n2 + 1*n3, n2,
		  format_hex_bytes, a->data + 2*n2 + 1*n3, n3);
    }

  return s;
}

static u8 * format_ethernet_arp_ip4_entry (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  ethernet_arp_ip4_entry_t * e = va_arg (*va, ethernet_arp_ip4_entry_t *);
  vlib_sw_interface_t * si;

  if (! e)
    return format (s, "%=12s%=20s%=20s%=40s", "Time", "IP4", "Ethernet", "Interface");

  si = vlib_get_sw_interface (vm, e->key.sw_if_index);
  s = format (s, "%=12U%=20U%=20U%=40U",
	      format_vlib_cpu_time, vm, e->cpu_time_last_updated,
	      format_ip4_address, &e->key.ip4_address,
	      format_ethernet_address, e->ethernet_address,
	      format_vlib_sw_interface_name, vm, si);

  return s;
}

typedef struct {
  u8 packet_data[64];
} ethernet_arp_input_trace_t;

static u8 * format_ethernet_arp_input_trace (u8 * s, va_list * va)
{
  UNUSED (vlib_main_t * vm) = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  ethernet_arp_input_trace_t * t = va_arg (*va, ethernet_arp_input_trace_t *);

  s = format (s, "%U",
	      format_ethernet_arp_header,
	      t->packet_data, sizeof (t->packet_data));

  return s;
}

static clib_error_t *
ethernet_arp_sw_interface_up_down (vlib_main_t * vm,
				   u32 sw_if_index,
				   u32 flags)
{
  ethernet_arp_main_t * am = &ethernet_arp_main;
  ethernet_arp_ip4_entry_t * e;

  if (! (flags & VLIB_SW_INTERFACE_FLAG_ADMIN_UP))
    {
      u32 i, * to_delete = 0;

      pool_foreach (e, am->ip4_entry_pool, ({
	    if (e->key.sw_if_index == sw_if_index)
	      vec_add1 (to_delete, e - am->ip4_entry_pool);
      }));

      for (i = 0; i < vec_len (to_delete); i++)
	{
	  e = pool_elt_at_index (am->ip4_entry_pool, to_delete[i]);
	  mhash_unset (&am->ip4_entry_by_key, &e->key, 0);
	  pool_put (am->ip4_entry_pool, e);
	}

      vec_free (to_delete);
    }

  return 0;
}

static void
arp_set_ip4_over_ethernet (vlib_main_t * vm,
			   ethernet_arp_main_t * am,
			   u32 sw_if_index,
			   ethernet_arp_ip4_over_ethernet_address_t * a)
{
  ethernet_arp_ip4_key_t k;
  ethernet_arp_ip4_entry_t * e;
  ip4_main_t * im = &ip4_main;
  uword * p, fib_index;

  fib_index = im->fib_index_by_sw_if_index[sw_if_index];

  k.sw_if_index = sw_if_index;
  k.ip4_address = a->ip4;

  p = mhash_get (&am->ip4_entry_by_key, &k);
  if (p)
    {
      e = pool_elt_at_index (am->ip4_entry_pool, p[0]);

      /* Refuse to over-write static arp. */
      if (e->flags & ETHERNET_ARP_IP4_ENTRY_FLAG_STATIC)
	return;
    }
  else
    {
      ip4_add_del_route_args_t args;
      ip_adjacency_t adj;
      ethernet_header_t * eth;

      adj.lookup_next_index = IP_LOOKUP_NEXT_REWRITE;

      vnet_rewrite_for_sw_interface
	(vm,
	 VNET_L3_PACKET_TYPE_IP4,
	 sw_if_index,
	 ip4_rewrite_node.index,
	 &adj.rewrite_header,
	 sizeof (adj.rewrite_data));

      /* Copy in destination ethernet address from ARP. */
      eth = vnet_rewrite_get_data (adj);
      memcpy (eth->dst_address, a->ethernet, sizeof (eth->dst_address));

      args.table_index_or_table_id = fib_index;
      args.flags = IP4_ROUTE_FLAG_FIB_INDEX | IP4_ROUTE_FLAG_ADD | IP4_ROUTE_FLAG_NEIGHBOR;
      args.dst_address = a->ip4;
      args.dst_address_length = 32;
      args.adj_index = ~0;
      args.add_adj = &adj;
      args.n_add_adj = 1;

      ip4_add_del_route (im, &args);
      pool_get (am->ip4_entry_pool, e);
      mhash_set (&am->ip4_entry_by_key, &k,
                 e - am->ip4_entry_pool,
		 /* old value */ 0);
      e->key = k;
    }

  /* Update time stamp and ethernet address. */
  memcpy (e->ethernet_address, a->ethernet, sizeof (e->ethernet_address));
  e->cpu_time_last_updated = clib_cpu_time_now ();
}

/* Either we drop the packet or we send a reply to the sender. */
typedef enum {
  ARP_INPUT_NEXT_DROP,
  ARP_INPUT_N_NEXT,
} arp_input_next_t;

#define foreach_ethernet_arp_error					\
  _ (replies_sent, "ARP replies sent")					\
  _ (l2_type_not_ethernet, "L2 type not ethernet")			\
  _ (l3_type_not_ip4, "L3 type not IP4")				\
  _ (l3_src_address_not_local, "IP4 source address not local to subnet") \
  _ (l3_dst_address_not_local, "IP4 destination address not local to subnet") \
  _ (l3_src_address_is_local, "IP4 source address matches local interface") \
  _ (replies_received, "ARP replies received")				\
  _ (opcode_not_request, "ARP opcode not request")

typedef enum {
#define _(sym,string) ETHERNET_ARP_ERROR_##sym,
  foreach_ethernet_arp_error
#undef _
  ETHERNET_ARP_N_ERROR,
} ethernet_arp_input_error_t;

static uword
arp_input (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  ethernet_arp_main_t * am = &ethernet_arp_main;
  ip4_main_t * im4 = &ip4_main;
  ethernet_main_t * em = &ethernet_main;
  u32 n_left_from, next_index, * from, * to_next;
  u32 n_replies_sent = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  if (node->flags & VLIB_NODE_FLAG_TRACE)
    vlib_trace_frame_buffers_only (vm, node, from, frame->n_vectors,
				   /* stride */ 1,
				   sizeof (ethernet_arp_input_trace_t));

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index,
			   to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  vlib_buffer_t * p0;
	  vlib_sw_interface_t * sw_if0;
	  ethernet_arp_header_t * arp0;
	  ethernet_header_t * eth0;
	  ethernet_interface_t * eth_if0;
	  ip_interface_address_t * ifa0;
	  ip4_address_t * if_addr0;
	  u32 pi0, error0, next0, sw_if_index0;
	  u8 is_request0, src_is_local0, dst_is_local0;

	  pi0 = from[0];
	  to_next[0] = pi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  p0 = vlib_get_buffer (vm, pi0);
	  arp0 = vlib_buffer_get_current (p0);

	  error0 = ETHERNET_ARP_ERROR_replies_sent;

	  error0 = (arp0->l2_type != clib_net_to_host_u16 (ETHERNET_ARP_HARDWARE_TYPE_ethernet)
		    ? ETHERNET_ARP_ERROR_l2_type_not_ethernet
		    : error0);
	  error0 = (arp0->l3_type != clib_net_to_host_u16 (ETHERNET_TYPE_IP4)
		    ? ETHERNET_ARP_ERROR_l3_type_not_ip4
		    : error0);

	  if (error0)
	    goto drop1;

	  /* Check that IP address is local and matches incoming interface. */
	  sw_if_index0 = p0->sw_if_index[VLIB_RX];
	  if_addr0 = ip4_interface_address_matching_destination (im4,
								 &arp0->ip4_over_ethernet[1].ip4,
								 sw_if_index0,
								 &ifa0);
	  if (! if_addr0)
	    {
	      error0 = ETHERNET_ARP_ERROR_l3_dst_address_not_local;
	      goto drop1;
	    }

	  /* Source must also be local to subnet of matching interface address. */
	  if (! ip4_destination_matches_interface (im4, &arp0->ip4_over_ethernet[0].ip4, ifa0))
	    {
	      error0 = ETHERNET_ARP_ERROR_l3_src_address_not_local;
	      goto drop1;
	    }

	  /* Reject requests/replies with our local interface address. */
	  src_is_local0 = if_addr0->as_u32 == arp0->ip4_over_ethernet[0].ip4.as_u32;
	  if (src_is_local0)
	    {
	      error0 = ETHERNET_ARP_ERROR_l3_src_address_is_local;
	      goto drop1;
	    }

	  dst_is_local0 = if_addr0->as_u32 == arp0->ip4_over_ethernet[1].ip4.as_u32;

	  /* Fill in ethernet header. */
	  eth0 = ethernet_buffer_get_header (p0);

	  is_request0 = arp0->opcode == clib_net_to_host_u16 (ETHERNET_ARP_OPCODE_request);

	  /* Learn or update sender's mapping only for requests or unicasts
	     that don't match local interface address. */
	  if (ethernet_address_cast (eth0->dst_address) == ETHERNET_ADDRESS_UNICAST
	      || is_request0)
	    arp_set_ip4_over_ethernet (vm, am, sw_if_index0, &arp0->ip4_over_ethernet[0]);

	  /* Only send a reply for requests sent which match a local interface. */
	  if (! (is_request0 && dst_is_local0))
	    {
	      error0 = (arp0->opcode == clib_net_to_host_u16 (ETHERNET_ARP_OPCODE_reply)
			? ETHERNET_ARP_ERROR_replies_received
			: ETHERNET_ARP_ERROR_opcode_not_request);
	      goto drop1;
	    }

	  /* Send a reply. */
	  sw_if0 = vlib_get_sup_sw_interface (vm, sw_if_index0);
	  ASSERT (sw_if0->type == VLIB_SW_INTERFACE_TYPE_HARDWARE);
	  next0 = vec_elt (am->arp_input_next_index_by_hw_if_index, sw_if0->hw_if_index);

	  eth_if0 = ethernet_get_interface (em, sw_if0->hw_if_index);
	  if (! eth_if0)
	    {
	      static ethernet_interface_t dummy;
	      eth_if0 = &dummy;
	    }

	  arp0->opcode = clib_host_to_net_u16 (ETHERNET_ARP_OPCODE_reply);

	  arp0->ip4_over_ethernet[1] = arp0->ip4_over_ethernet[0];

	  memcpy (arp0->ip4_over_ethernet[0].ethernet, eth_if0->address, 6);
	  clib_mem_unaligned (&arp0->ip4_over_ethernet[0].ip4.data_u32, u32) = if_addr0->data_u32;

	  p0->current_data -= sizeof (eth0[0]);
	  p0->current_length += sizeof (eth0[0]);

	  memcpy (eth0->dst_address, eth0->src_address, 6);
	  memcpy (eth0->src_address, eth_if0->address, 6);

	  if (next0 != next_index)
	    {
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);

	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index,
				   to_next, n_left_to_next);
	      to_next[0] = pi0;
	      n_left_to_next -= 1;
	    }
	  n_replies_sent += 1;
	  continue;

	drop1:
	  next0 = ARP_INPUT_NEXT_DROP;
	  p0->error = node->errors[error0];
	  if (next0 != next_index)
	    {
	      vlib_put_next_frame (vm, node, next_index, n_left_to_next + 1);
	      next_index = next0;
	      vlib_get_next_frame (vm, node, next_index,
				   to_next, n_left_to_next);
	      to_next[0] = pi0;
	      n_left_to_next -= 1;
	    }
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  vlib_error_count (vm, node->node_index,
		    ETHERNET_ARP_ERROR_replies_sent, n_replies_sent);

  return frame->n_vectors;
}

static char * ethernet_arp_error_strings[] = {
#define _(sym,string) string,
  foreach_ethernet_arp_error
#undef _
};

static clib_error_t *
ethernet_arp_hw_interface_link_up_down (vlib_main_t * vm,
					u32 hw_if_index,
					u32 flags);

static VLIB_REGISTER_NODE (arp_input_node) = {
  .function = arp_input,
  .name = "arp-input",
  .vector_size = sizeof (u32),

  .n_errors = ETHERNET_ARP_N_ERROR,
  .error_strings = ethernet_arp_error_strings,

  .n_next_nodes = ARP_INPUT_N_NEXT,
  .next_nodes = {
    [ARP_INPUT_NEXT_DROP] = "error-drop",
  },

  .format_buffer = format_ethernet_arp_header,
  .format_trace = format_ethernet_arp_input_trace,

  .hw_interface_link_up_down_function = ethernet_arp_hw_interface_link_up_down,
  .sw_interface_admin_up_down_function = ethernet_arp_sw_interface_up_down,
};

static clib_error_t *
ethernet_arp_hw_interface_link_up_down (vlib_main_t * vm,
					u32 hw_if_index,
					u32 flags)
{
  ethernet_arp_main_t * am = &ethernet_arp_main;
  vlib_hw_interface_t * hw_if;

  hw_if = vlib_get_hw_interface (vm, hw_if_index);

  /* Fill in lookup tables with default table (0). */
  vec_validate_init_empty (am->arp_input_next_index_by_hw_if_index, hw_if_index, ~0);
  am->arp_input_next_index_by_hw_if_index[hw_if_index]
    = vlib_node_add_next (vm, arp_input_node.index, hw_if->output_node_index);

  return 0;
}

static int
ip4_arp_entry_sort (vlib_main_t * vm,
		    ethernet_arp_ip4_entry_t * e1, ethernet_arp_ip4_entry_t * e2)
{
  int cmp;
  cmp = vlib_sw_interface_compare (vm, e1->key.sw_if_index, e2->key.sw_if_index);
  if (! cmp)
    cmp = ip4_address_compare (&e1->key.ip4_address, &e2->key.ip4_address);
  return cmp;
}

static clib_error_t *
show_ip4_arp (vlib_main_t * vm,
	      unformat_input_t * input,
	      vlib_cli_command_t * cmd)
{
  ethernet_arp_main_t * am = &ethernet_arp_main;
  ethernet_arp_ip4_entry_t * e, * es;
  clib_error_t * error = 0;
  u32 sw_if_index;

  /* Filter entries by interface if given. */
  sw_if_index = ~0;
  unformat_user (input, unformat_vlib_sw_interface, vm, &sw_if_index);

  es = 0;
  pool_foreach (e, am->ip4_entry_pool, ({ vec_add1 (es, e[0]); }));
  vec_sort (es, e1, e2, ip4_arp_entry_sort (vm, e1, e2));
  vlib_cli_output (vm, "%U", format_ethernet_arp_ip4_entry, vm, 0);
  vec_foreach (e, es) {
    if (sw_if_index != ~0 && e->key.sw_if_index != sw_if_index)
      continue;
    vlib_cli_output (vm, "%U", format_ethernet_arp_ip4_entry, vm, e);
  }
  vec_free (es);

  return error;
}

static VLIB_CLI_COMMAND (show_ip4_arp_command) = {
  .path = "show ip arp",
  .function = show_ip4_arp,
  .short_help = "Show ARP table",
};

typedef struct {
  pg_edit_t l2_type, l3_type;
  pg_edit_t n_l2_address_bytes, n_l3_address_bytes;
  pg_edit_t opcode;
  struct {
    pg_edit_t ethernet;
    pg_edit_t ip4;
  } ip4_over_ethernet[2];
} pg_ethernet_arp_header_t;

static inline void
pg_ethernet_arp_header_init (pg_ethernet_arp_header_t * p)
{
  /* Initialize fields that are not bit fields in the IP header. */
#define _(f) pg_edit_init (&p->f, ethernet_arp_header_t, f);
  _ (l2_type);
  _ (l3_type);
  _ (n_l2_address_bytes);
  _ (n_l3_address_bytes);
  _ (opcode);
  _ (ip4_over_ethernet[0].ethernet);
  _ (ip4_over_ethernet[0].ip4);
  _ (ip4_over_ethernet[1].ethernet);
  _ (ip4_over_ethernet[1].ip4);
#undef _
}

uword
unformat_pg_arp_header (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_ethernet_arp_header_t * p;
  u32 group_index;
  
  p = pg_create_edit_group (s, sizeof (p[0]), sizeof (ethernet_arp_header_t),
			    &group_index);
  pg_ethernet_arp_header_init (p);

  /* Defaults. */
  pg_edit_set_fixed (&p->l2_type, ETHERNET_ARP_HARDWARE_TYPE_ethernet);
  pg_edit_set_fixed (&p->l3_type, ETHERNET_TYPE_IP4);
  pg_edit_set_fixed (&p->n_l2_address_bytes, 6);
  pg_edit_set_fixed (&p->n_l3_address_bytes, 4);

  if (! unformat (input, "%U: %U/%U -> %U/%U",
		  unformat_pg_edit,
		  unformat_ethernet_arp_opcode_net_byte_order, &p->opcode,
		  unformat_pg_edit,
		  unformat_ethernet_address, &p->ip4_over_ethernet[0].ethernet,
		  unformat_pg_edit,
		  unformat_ip4_address, &p->ip4_over_ethernet[0].ip4,
		  unformat_pg_edit,
		  unformat_ethernet_address, &p->ip4_over_ethernet[1].ethernet,
		  unformat_pg_edit,
		  unformat_ip4_address, &p->ip4_over_ethernet[1].ip4))
    {
      /* Free up any edits we may have added. */
      pg_free_edit_group (s);
      return 0;
    }
  return 1;
}

static clib_error_t * ethernet_arp_init (vlib_main_t * vm)
{
  ethernet_arp_main_t * am = &ethernet_arp_main;
  pg_node_t * pn;

  ethernet_register_input_type (vm, ETHERNET_TYPE_ARP, arp_input_node.index);

  pn = pg_get_node (arp_input_node.index);
  pn->unformat_edit = unformat_pg_arp_header;

  am->opcode_by_name = hash_create_string (0, sizeof (uword));
#define _(o) hash_set_mem (am->opcode_by_name, #o, ETHERNET_ARP_OPCODE_##o);
  foreach_ethernet_arp_opcode;
#undef _

  mhash_init (&am->ip4_entry_by_key,
	      /* value size */ sizeof (uword),
	      /* key size */ sizeof (ethernet_arp_ip4_key_t));

  return 0;
}

VLIB_INIT_FUNCTION (ethernet_arp_init);
