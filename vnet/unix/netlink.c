/* 
 * unix/netlink.c - interact with linux kernel net stack
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

#include <sys/ioctl.h>

#include <vnet/unix/netlink.h>
#include <vlib/unix/unix.h>
#include <vnet/ip/ip.h>		/* for format_ip[46]_address */
#include <vnet/ethernet/ethernet.h> /* for format_ethernet_address */

/* This adds a both a nlmsghdr and a request header (e.g. ifinfomsg, ifaddrmsg, rtmsg, ...)
   to the end of the tx buffer. */
void * netlink_tx_add_request_with_flags (u32 type, u32 n_bytes, u32 flags)
{
  netlink_main_t * nm = &netlink_main;
  u8 * r;
  struct nlmsghdr * h;

  vec_reset_length (nm->tx_buffer);
  vec_add2 (nm->tx_buffer, r, sizeof (struct nlmsghdr) + NLMSG_ALIGN (n_bytes));
  h = (void *) r;
  h->nlmsg_len = sizeof (h[0]) + n_bytes;
  h->nlmsg_type = type;
  h->nlmsg_flags = NLM_F_REQUEST | flags;
  h->nlmsg_seq = nm->tx_sequence_number++;
  h->nlmsg_pid = 0;		/* port to be assigned by kernel */

  return r + NLMSG_ALIGN (sizeof (h[0]));
}

static void netlink_tx (netlink_main_t * nm)
{
  clib_fifo_add1 (nm->tx_fifo, nm->tx_buffer);
  nm->tx_buffer = 0;
  unix_file_set_data_available_to_write (nm->unix_file_index_for_socket, /* is_available */ 1);
}

void * netlink_tx_add_attr (u32 attr_type, u32 attr_len)
{
  netlink_main_t * nm = &netlink_main;
  struct nlmsghdr * h;
  struct nlattr * a;
  u32 l;

  l = vec_len (nm->tx_buffer);
  vec_resize (nm->tx_buffer, NLA_ALIGN (sizeof (a[0])) + NLA_ALIGN (attr_len));
  h = (void *) vec_elt_at_index (nm->tx_buffer, l);
  a = (void *) h + NLA_ALIGN (h->nlmsg_len);
  h->nlmsg_len += NLA_ALIGN (sizeof (a[0])) + NLA_ALIGN (attr_len);
  a->nla_type = attr_type;
  a->nla_len = attr_len;
  return (void *) (a + 1);
}

static void netlink_tx_gen_request (netlink_main_t * nm, u32 type, u32 family)
{
  struct rtgenmsg * g;
  g = netlink_tx_add_request_with_flags (type, sizeof (g[0]), NLM_F_DUMP);
  g->rtgen_family = family;
  netlink_tx (nm);
}

/* _ (SYMBOL, IS-GET) */
#define foreach_netlink_message_type		\
  _ (NLMSG_NOOP, 0)				\
  _ (NLMSG_ERROR, 0)				\
  _ (NLMSG_DONE, 0)				\
  _ (NLMSG_OVERRUN, 0)				\
  _ (RTM_NEWLINK, 0)				\
  _ (RTM_DELLINK, 0)				\
  _ (RTM_GETLINK, 1)				\
  _ (RTM_SETLINK, 0)				\
  _ (RTM_NEWADDR, 0)				\
  _ (RTM_DELADDR, 0)				\
  _ (RTM_GETADDR, 1)				\
  _ (RTM_NEWROUTE, 0)				\
  _ (RTM_DELROUTE, 0)				\
  _ (RTM_GETROUTE, 1)				\
  _ (RTM_NEWNEIGH, 0)				\
  _ (RTM_DELNEIGH, 0)				\
  _ (RTM_GETNEIGH, 1)				\
  _ (RTM_NEWRULE, 0)				\
  _ (RTM_DELRULE, 0)				\
  _ (RTM_GETRULE, 1)				\
  _ (RTM_NEWQDISC, 0)				\
  _ (RTM_DELQDISC, 0)				\
  _ (RTM_GETQDISC, 1)				\
  _ (RTM_NEWTCLASS, 0)				\
  _ (RTM_DELTCLASS, 0)				\
  _ (RTM_GETTCLASS, 1)				\
  _ (RTM_NEWTFILTER, 0)				\
  _ (RTM_DELTFILTER, 0)				\
  _ (RTM_GETTFILTER, 1)				\
  _ (RTM_NEWACTION, 0)				\
  _ (RTM_DELACTION, 0)				\
  _ (RTM_GETACTION, 1)				\
  _ (RTM_NEWPREFIX, 0)				\
  _ (RTM_GETMULTICAST, 1)			\
  _ (RTM_GETANYCAST, 1)				\
  _ (RTM_NEWNEIGHTBL, 0)			\
  _ (RTM_GETNEIGHTBL, 1)			\
  _ (RTM_SETNEIGHTBL, 0)			\
  _ (RTM_NEWNDUSEROPT, 0)			\
  _ (RTM_NEWADDRLABEL, 0)			\
  _ (RTM_DELADDRLABEL, 0)			\
  _ (RTM_GETADDRLABEL, 1)			\
  _ (RTM_GETDCB, 0)				\
  _ (RTM_SETDCB, 1)

static u8 * format_netlink_message_type (u8 * s, va_list * va)
{
  u32 x = va_arg (*va, u32);
  char * t;

  switch (x)
    {
#define _(a,b) case a: t = #a; break;
      foreach_netlink_message_type
#undef _
    default:
      t = 0;
      break;
    }

  if (t)
    s = format (s, "%s", t);
  else
    s = format (s, "unknown 0x%x", x);

  return s;
}

static u8 * format_netlink_attribute_x (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  return format (s, "%U", format_hex_bytes, a + 1, a->nla_len - sizeof (a[0]));
}

static u8 * format_netlink_attribute_s (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  return format (s, "%s", a + 1);
}

static u8 * format_netlink_attribute_d (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  return format (s, "%d", *(u32 *) (a + 1));
}

#define foreach_netlink_route_table		\
  _ (unspec, 0)					\
  _ (compat, 252)				\
  _ (default, 253)				\
  _ (main, 254)					\
  _ (local, 255)

static u8 * format_netlink_route_table (u8 * s, va_list * va)
{
  u32 x = va_arg (*va, u32);
  char * t;
  switch (x)
    {
#define _(f,n) case n: t = #f; break;
      foreach_netlink_route_table
#undef _
    default:
      t = 0;
      break;
    }
  if (t)
    s = format (s, "%s", t);
  else
    s = format (s, "unknown 0x%x", x);
  return s;
}

static u8 * format_netlink_attribute_route_table (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  return format (s, "%U", format_netlink_route_table, *(u32 *) (a + 1));
}

static u8 * format_netlink_attribute_af (u8 * s, va_list * va)
{
  struct nlattr * a_sup = va_arg (*va, struct nlattr *);
  struct nlattr * a, * a_sub;
  uword indent = format_get_indent (s);
  uword n = 0;
  
  foreach_netlink_sub_attribute (a, a_sup)
    {
      if (n++ > 0)
	s = format (s, "\n%U", format_white_space, indent);
      s = format (s, "%U", format_address_family, a->nla_type);
      switch (a->nla_type)
	{
	case AF_INET:
	case AF_INET6:
	  foreach_netlink_sub_attribute (a_sub, a)
	    {
	      s = format (s, "\n%U%d %U, ",
			  format_white_space, indent + 2,
			  a_sub->nla_type, format_hex_bytes, a_sub + 1, a_sub->nla_len - sizeof (a_sub[0]));
	    }
	  break;

	default:
	  ASSERT (0);
	}
    }

  return s;
}

static u8 * format_netlink_attribute_family_address (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  u8 * addr = (void *) (a + 1);
  u32 addr_len = a->nla_len - sizeof (a[0]);
  switch (addr_len)
    {
    case 4: s = format (s, "%U", format_ip4_address, addr); break;
    case 16: s = format (s, "%U", format_ip6_address, addr); break;
    default: s = format (s, "%U", format_hex_bytes, addr, addr_len); break;
    }
  return s;
}

static u8 * format_netlink_attribute_link_address (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  u8 * addr = (void *) (a + 1);
  u32 addr_len = a->nla_len - sizeof (a[0]);
  switch (addr_len)
    {
    case 6: s = format (s, "%U", format_ethernet_address, addr); break;
    default: s = format (s, "%U", format_hex_bytes, addr, addr_len); break;
    }
  return s;
}

typedef struct {
  char * name;
  format_function_t * format;
} netlink_attribute_type_info_t;

static u8 * format_netlink_attribute (u8 * s, va_list * va)
{
  struct nlattr * a = va_arg (*va, struct nlattr *);
  netlink_attribute_type_info_t * info = va_arg (*va, netlink_attribute_type_info_t *);
  u32 n_info = va_arg (*va, u32);

  if (a->nla_type < n_info)
    s = format (s, "%s: ", info[a->nla_type].name);
  else
    s = format (s, "unknown 0x%x: ", a->nla_type);

  s = format (s, "%U", info[a->nla_type].format, a);

  return s;
}

#define foreach_netlink_ifinfo_attribute	\
  _ (unspec, x)					\
  _ (address, link_address)			\
  _ (broadcast, link_address)			\
  _ (name, s)					\
  _ (mtu, d)					\
  _ (link, x)					\
  _ (qdisc, s)					\
  _ (stats, x)					\
  _ (cost, x)					\
  _ (priority, x)				\
  _ (master, x)					\
  _ (wireless, x)				\
  _ (protinfo, x)				\
  _ (txqlen, d)					\
  _ (map, x)					\
  _ (weight, d)					\
  _ (operstate, x)				\
  _ (linkmode, x)				\
  _ (linkinfo, x)				\
  _ (net, x)					\
  _ (ifalias, x)				\
  _ (num, d)					\
  _ (vfinfo, x)					\
  _ (stats64, x)				\
  _ (vf, x)					\
  _ (port, x)					\
  _ (af, af)					\
  _ (group, d)					\
  _ (net, x)					\
  _ (ext, x)

static netlink_attribute_type_info_t ifinfo_attribute_info[] = {
#define _(a,b) { .name = #a, .format = format_netlink_attribute_##b, },
  foreach_netlink_ifinfo_attribute
#undef _
};

#define foreach_netlink_ifaddr_attribute	\
  _ (unspec, x)					\
  _ (address, family_address)			\
  _ (local, family_address)			\
  _ (name, s)					\
  _ (broadcast, family_address)			\
  _ (anycast, family_address)			\
  _ (cacheinfo, x)				\
  _ (multicast, family_address)

static netlink_attribute_type_info_t ifaddr_attribute_info[] = {
#define _(a,b) { .name = #a, .format = format_netlink_attribute_##b, },
  foreach_netlink_ifaddr_attribute
#undef _
};

#define foreach_netlink_ifaddr_flag		\
  _ (secondary)					\
  _ (no_dad)					\
  _ (optimistic)				\
  _ (dad_failed)				\
  _ (home_address)				\
  _ (deprecated)				\
  _ (tentative)					\
  _ (permanent)

static u8 * format_netlink_interface_addr_flags (u8 * s, va_list * va)
{
  u32 flags = va_arg (*va, u32);
  u32 i;
  static char * n[] = {
#define _(f) #f,
    foreach_netlink_ifaddr_flag
#undef _
  };
  if (flags == 0)
    s = format (s, "none");
  else foreach_set_bit (i, flags, ({
    if (i < ARRAY_LEN (n))
      s = format (s, "%s", n[i]);
    else
      s = format (s, "unknown %d", i);
    if (flags >> (i + 1))
      s = format (s, ", ");
  }));
  return s;
}

#define foreach_netlink_route_scope		\
  _ (universe, 0)				\
  _ (site, 200)					\
  _ (link, 253)					\
  _ (host, 254)					\
  _ (nowhere, 255)

static u8 * format_netlink_route_scope (u8 * s, va_list * va)
{
  u32 scope = va_arg (*va, u32);
  char * t;
  switch (scope)
    {
#define _(f,n) case n: t = #f; break;
      foreach_netlink_route_scope
#undef _
    default:
      t = 0;
      break;
    }
  if (t)
    s = format (s, "%s", t);
  else
    s = format (s, "unknown 0x%x", scope);
  return s;
}

#define foreach_netlink_route_protocol		\
  _ (unspec, 0)					\
  _ (icmp_redirect, 1)				\
  _ (kernel, 2)					\
  _ (boot, 3)					\
  _ (static, 4)					\
  _ (gated, 8)					\
  _ (ip6_router_discovery, 9)			\
  _ (mrt, 10)					\
  _ (zebra, 11)					\
  _ (bird, 12)					\
  _ (decnet_routed, 13)				\
  _ (xorp, 14)					\
  _ (ntk, 15)					\
  _ (dhcp, 16)

static u8 * format_netlink_route_protocol (u8 * s, va_list * va)
{
  u32 x = va_arg (*va, u32);
  char * t;
  switch (x)
    {
#define _(f,n) case n: t = #f; break;
      foreach_netlink_route_protocol
#undef _
    default:
      t = 0;
      break;
    }
  if (t)
    s = format (s, "%s", t);
  else
    s = format (s, "unknown 0x%x", x);
  return s;
}

#define foreach_netlink_route_type		\
  _ (unspec,0)					\
  _ (unicast,1)					\
  _ (local,2)					\
  _ (broadcast,3)				\
  _ (anycast,4)					\
  _ (multicast,5)				\
  _ (drop,6)					\
  _ (unreachable,7)				\
  _ (prohibit,8)				\
  _ (throw,9)					\
  _ (nat,10)					\
  _ (external_resolver,11)

static u8 * format_netlink_route_type (u8 * s, va_list * va)
{
  u32 x = va_arg (*va, u32);
  char * t;
  switch (x)
    {
#define _(f,n) case n: t = #f; break;
      foreach_netlink_route_type
#undef _
    default:
      t = 0;
      break;
    }
  if (t)
    s = format (s, "%s", t);
  else
    s = format (s, "unknown 0x%x", x);
  return s;
}

#define foreach_netlink_route_flag		\
  _ (notify, 8)					\
  _ (cloned, 9)					\
  _ (equalize, 10)				\
  _ (prefix, 11)

static u8 * format_netlink_route_flags (u8 * s, va_list * va)
{
  u32 flags = va_arg (*va, u32);
  u32 i;
  static char * n[32] = {
#define _(f,n) [n] = #f,
    foreach_netlink_route_flag
#undef _
  };
  if (flags == 0)
    s = format (s, "none");
  else foreach_set_bit (i, flags, ({
    if (n[i])
      s = format (s, "%s", n[i]);
    else
      s = format (s, "unknown %d", i);
    if (flags >> (i + 1))
      s = format (s, ", ");
  }));
  return s;
}

#define foreach_netlink_route_attribute		\
  _ (unspec, x)					\
  _ (dst, family_address)			\
  _ (src, family_address)			\
  _ (rx_if, d)					\
  _ (tx_if, d)					\
  _ (gateway, family_address)			\
  _ (priority, d)				\
  _ (pref_src, family_address)			\
  _ (metrics, x)				\
  _ (multipath, x)				\
  _ (protoinfo, x)				\
  _ (flow, x)					\
  _ (cacheinfo, x)				\
  _ (session, x)				\
  _ (mp_algo, x)				\
  _ (table, route_table)			\
  _ (mark, x)

static netlink_attribute_type_info_t route_attribute_info[] = {
#define _(a,b) { .name = #a, .format = format_netlink_attribute_##b, },
  foreach_netlink_route_attribute
#undef _
};

#define NLM_F_ROOT	0x100	/* specify tree	root	*/
#define NLM_F_MATCH	0x200	/* return all matching	*/
#define NLM_F_ATOMIC	0x400	/* atomic GET		*/
#define NLM_F_DUMP	(NLM_F_ROOT|NLM_F_MATCH)

/* Modifiers to NEW request */
#define NLM_F_REPLACE	0x100	/* Override existing		*/
#define NLM_F_EXCL	0x200	/* Do not touch, if it exists	*/
#define NLM_F_CREATE	0x400	/* Create, if it does not exist	*/
#define NLM_F_APPEND	0x800	/* Add to end of list		*/

static u8 * format_netlink_message_flags (u8 * s, va_list * va)
{
  u32 flags = va_arg (*va, u32);
  u32 is_get = va_arg (*va, u32);

  if (flags == 0)
    return format (s, "none");

  if (flags &  NLM_F_REQUEST)
    s = format (s, "request ");
  if (flags &  NLM_F_MULTI)
    s = format (s, "multi ");
  if (flags &  NLM_F_ACK)
    s = format (s, "ack ");
  if (flags &  NLM_F_ECHO)
    s = format (s, "echo ");

  if (is_get)
    {
      if (flags & NLM_F_ROOT)
	s = format (s, "root ");
      if (flags & NLM_F_MATCH)
	s = format (s, "all-matching ");
      if (flags & NLM_F_ATOMIC)
	s = format (s, "atomic ");
    }

  else
    {
      if (flags & NLM_F_REPLACE)
	s = format (s, "replace ");
      if (flags & NLM_F_EXCL)
	s = format (s, "exclusive ");
      if (flags & NLM_F_CREATE)
	s = format (s, "create-if-non-existing ");
      if (flags & NLM_F_APPEND)
	s = format (s, "append-to-end-of-list ");
    }

  return s;
}

static int netlink_message_is_get (struct nlmsghdr * h)
{
  static char t[] = {
#define _(a,is_get) [a] = is_get,
    foreach_netlink_message_type
#undef _
  };
  return h->nlmsg_type < ARRAY_LEN (t) ? t[h->nlmsg_type] : 0;
}

static u8 * format_netlink_message (u8 * s, va_list * va)
{
  struct nlmsghdr * h = va_arg (*va, struct nlmsghdr *);
  int decode = va_arg (*va, int);
  int is_get;
  uword indent = format_get_indent (s);

  is_get = netlink_message_is_get (h);

  s = format (s, "%U: len %d, sequence %d, pid %d, flags %U",
	      format_netlink_message_type, h->nlmsg_type,
	      h->nlmsg_len,
	      h->nlmsg_seq, h->nlmsg_seq,
	      format_netlink_message_flags, h->nlmsg_flags, is_get);

  /* For gets there is not much to decode. */
  if (is_get)
    {
      struct rtgenmsg * g = (void *) (h + 1);
      s = format (s, "\n%Ufamily %U",
		  format_white_space, indent + 2,
		  format_address_family, g->rtgen_family);
      return s;
    }

  if (! decode)
    return s;

  switch (h->nlmsg_type)
    {
    case RTM_NEWLINK: case RTM_DELLINK:
    case RTM_GETLINK: case RTM_SETLINK:
      {
	struct ifinfomsg * i = nlmsg_contents (h);
	struct nlattr * a;
	s = format (s, "\n%Ufamily %U, type %U, index %d, flags %U (change %U)",
		    format_white_space, indent + 2,
		    format_address_family, i->ifi_family,
		    format_unix_arphrd, i->ifi_type,
		    i->ifi_index,
		    format_unix_interface_flags, i->ifi_flags,
		    format_unix_interface_flags, i->ifi_change);
	foreach_netlink_message_attribute (a, h, i)
	  {
	    s = format (s, "\n%U%U",
			format_white_space, indent + 2,
			format_netlink_attribute, a, ifinfo_attribute_info, ARRAY_LEN (ifinfo_attribute_info));
	  }
      }
      break;

    case RTM_NEWADDR: case RTM_DELADDR:
    case RTM_GETADDR:
      {
	struct ifaddrmsg * i = nlmsg_contents (h);
	struct nlattr * a;
	s = format (s, "\n%Ufamily %U, index %d, prefix len %d, scope %U, flags %U",
		    format_white_space, indent + 2,
		    format_address_family, i->ifa_family,
		    i->ifa_index,
		    i->ifa_prefixlen,
		    format_netlink_route_scope, i->ifa_scope,
		    format_netlink_interface_addr_flags, i->ifa_flags);
	foreach_netlink_message_attribute (a, h, i)
	  {
	    s = format (s, "\n%U%U",
			format_white_space, indent + 2,
			format_netlink_attribute, a, ifaddr_attribute_info, ARRAY_LEN (ifaddr_attribute_info));
	  }
      }
      break;

    case RTM_NEWROUTE: case RTM_DELROUTE:
    case RTM_GETROUTE:
      {
	struct rtmsg * r = nlmsg_contents (h);
	struct nlattr * a;
	s = format (s, "\n%Ufamily %U, dst len %d, src len %d, tos %d, type %U, scope %U, table %U, protocol %U, flags %U",
		    format_white_space, indent + 2,
		    format_address_family, r->rtm_family,
		    r->rtm_dst_len, r->rtm_src_len, r->rtm_tos,
		    format_netlink_route_type, r->rtm_type,
		    format_netlink_route_scope, r->rtm_scope,
		    format_netlink_route_table, r->rtm_table,
		    format_netlink_route_protocol, r->rtm_protocol,
		    format_netlink_route_flags, r->rtm_flags);
	foreach_netlink_message_attribute (a, h, r)
	  {
	    s = format (s, "\n%U%U",
			format_white_space, indent + 2,
			format_netlink_attribute, a, route_attribute_info, ARRAY_LEN (route_attribute_info));
	  }
      }
      break;

    case NLMSG_ERROR:
      {
	struct nlmsgerr * e = nlmsg_contents (h);
	s = format (s, "\n%Uerrored message: %U, error: %s",
		    format_white_space, indent + 2,
		    format_netlink_message, &e->msg, /* decode */ 0,
		    e->error < 0 ? strerror (-e->error) : "unknown");
      }
      break;

    default:
      break;
    }

  return s;
}

static u8 * format_netlink_message_vector (u8 * s, va_list * va)
{
  u8 * vector = va_arg (*va, u8 *);
  int decode = va_arg (*va, int);
  struct nlmsghdr * h;
  uword indent = format_get_indent (s);
  uword n = 0;

  foreach_netlink_message_header (h, vector)
    {
      if (n > 0)
	s = format (s, "\n%U", format_white_space, indent);
      s = format (s, "%U", format_netlink_message, h, decode);
      n++;
    }
  s = format (s, "\n");
  return s;
}

static u8 * format_netlink_message_history_side (u8 * s, va_list * va)
{
  netlink_message_history_side_t * d = va_arg (*va, netlink_message_history_side_t *);
  int decode = va_arg (*va, int);
  uword indent = format_get_indent (s);
  uword i, i_min, i_max;

  if (d->n_messages == 0)
    return s;

  if (d->n_messages <= ARRAY_LEN (d->messages))
    {
      i_min = 0;
      i_max = d->n_messages - 1;
    }
  else
    {
      i_min = d->n_messages - ARRAY_LEN (d->messages);
      i_max = d->n_messages;
    }

  for (i = i_min; i <= i_max; i++)
    {
      if (i > i_min)
	s = format (s, "\n%U", format_white_space, indent);
      s = format (s, "%U", format_netlink_message_vector, d->messages[i % ARRAY_LEN (d->messages)], decode);
    }

  return s;
}

static clib_error_t *
unix_read_from_file_to_vector (int fd, u8 ** vector, u32 read_size)
{
  clib_error_t * error = 0;
  u8 * v = *vector;
  u32 l;
  int n;

  while (1)
    {
      l = vec_len (v);
      vec_resize (v, read_size);
      n = read (fd, v + l, read_size);
      if (n <= 0)
	{
	  if (unix_error_is_fatal (errno))
	    error = clib_error_return_unix (0, "read");
	  _vec_len (v) -= read_size;
	  goto done;
	}
      _vec_len (v) += n - read_size;
    }

 done:
  *vector = v;
  return error;
}

/* Gets called when file descriptor is read ready from epoll. */
static clib_error_t * netlink_read_ready (unix_file_t * uf)
{
  vlib_main_t * vm = &vlib_global_main;
  netlink_main_t * nm = &netlink_main;
  clib_error_t * error;
  u8 * rx_buffer = 0;
  error = unix_read_from_file_to_vector (nm->socket, &rx_buffer, 4096);

  if (! error)
    {
      /* Process level will free allocated buffer. */
      vlib_process_signal_event_pointer (vm, nm->netlink_process_node_index, 0, rx_buffer);
    }
  else
    vec_free (rx_buffer);

  return error;
}

static clib_error_t * netlink_write_ready (unix_file_t * uf)
{
  netlink_main_t * nm = &netlink_main;
  clib_error_t * error = 0;
  u8 * b;
  int n_write;

  ASSERT (clib_fifo_elts (nm->tx_fifo) > 0);
  b = *clib_fifo_head (nm->tx_fifo);
  n_write = write (nm->socket, b, vec_len (b));
  if (n_write < 0)
    {
      if (unix_error_is_fatal (errno))
	error = clib_error_return_unix (0, "write");
      n_write = 0;
    }
  else if (n_write > 0)
    ASSERT (n_write == vec_len (b));

  if (n_write > 0)
    {
      clib_fifo_advance_head (nm->tx_fifo, 1);
      netlink_add_to_message_history (nm, VLIB_TX, b);
    }

  unix_file_set_data_available_to_write (nm->unix_file_index_for_socket,
					 clib_fifo_elts (nm->tx_fifo) > 0);

  return error;
}

static void netlink_rx_error_message (struct nlmsghdr * h, uword opaque)
{
  /* FIXME add to error history and continue. */
  clib_error ("%U", format_netlink_message, h, /* decode */ 1);
}

static void netlink_rx_ignore_message (struct nlmsghdr * h, uword opaque)
{ }

static void netlink_rx_message (netlink_main_t * nm, struct nlmsghdr * h)
{
  /* Reigster handlers for standard messages. */
  if (NLMSG_DONE >= vec_len (nm->rx_handler_by_message_type)
      || ! nm->rx_handler_by_message_type[NLMSG_DONE].handler)
    {
      netlink_register_rx_handler (NLMSG_DONE, netlink_rx_ignore_message, /* opaque */ 0);
      netlink_register_rx_handler (NLMSG_NOOP, netlink_rx_ignore_message, /* opaque */ 0);
      netlink_register_rx_handler (NLMSG_OVERRUN, netlink_rx_ignore_message, /* opaque */ 0);
      netlink_register_rx_handler (NLMSG_ERROR, netlink_rx_error_message, /* opaque */ 0);
    }

  {
    netlink_rx_message_handler_and_opaque_t * x;

    x = &nm->rx_handler_by_message_type[h->nlmsg_type];
    if (h->nlmsg_type < vec_len (nm->rx_handler_by_message_type) && x->handler)
      x->handler (h, x->opaque);
    else
      clib_warning ("unhandled message: %U", format_netlink_message, h, /* decode */ 0);
  }
}

static void netlink_rx_buffer (netlink_main_t * nm, u8 * rx_vector)
{
  struct nlmsghdr * h;

  foreach_netlink_message_header (h, rx_vector)
    netlink_rx_message (nm, h);

  /* rx_vector will be freed when history wraps. */
  netlink_add_to_message_history (nm, VLIB_RX, rx_vector);
}

static uword
netlink_process (vlib_main_t * vm,
		 vlib_node_runtime_t * rt,
		 vlib_frame_t * f)
{
  netlink_main_t * nm = &netlink_main;
  uword event_type;
  void * event_data;
    
  while (1)
    {
      vlib_process_wait_for_event (vm);

      event_data = vlib_process_get_event_data (vm, &event_type);
      switch (event_type)
	{
	case 0:
	  {
	    u8 ** rx_buffers = event_data;
	    uword i;

	    for (i = 0; i < vec_len (rx_buffers); i++)
	      netlink_rx_buffer (nm, rx_buffers[i]);
	  }
        break;
        
      default:
	ASSERT (0);
      }

      vlib_process_put_event_data (vm, event_data);
    }
	    
  return 0;
}

static vlib_node_registration_t netlink_process_node = {
  .function = netlink_process,
  .type = VLIB_NODE_TYPE_PROCESS,
  .name = "netlink-process",
};

static clib_error_t *
show_netlink_history (vlib_main_t * vm, unformat_input_t * input, vlib_cli_command_t * cmd)
{
  netlink_main_t * nm = &netlink_main;
  
  vlib_cli_output (vm, "Sent messages:\n\n%U\n",
		   format_netlink_message_history_side, &nm->history_sides[VLIB_TX], /* decode */ 1);
  vlib_cli_output (vm, "Received messages:\n\n%U",
		   format_netlink_message_history_side, &nm->history_sides[VLIB_RX], /* decode */ 1);

  return /* no error */ 0;
}

static VLIB_CLI_COMMAND (netlink_show_history_command) = {
  .path = "show netlink history",
  .short_help = "Show recent netlink messages received/sent.",
  .function = show_netlink_history,
};

static clib_error_t *
netlink_init (vlib_main_t * vm)
{
  netlink_main_t * nm = &netlink_main;
  clib_error_t * error = 0;

  nm->socket = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (nm->socket < 0)
    {
      error = clib_error_return_unix (0, "socket AF_NETLINK");
      goto done;
    }

  {
    struct sockaddr_nl sa;

    memset (&sa, 0, sizeof (sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = (RTMGRP_LINK | RTMGRP_NEIGH
		    | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV4_MROUTE
		    | RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE | RTMGRP_IPV6_MROUTE);

    if (bind (nm->socket, (struct sockaddr *) &sa, sizeof (sa)) < 0)
      {
	error = clib_error_return_unix (0, "bind");
	goto done;
      }
  }

  /* Non-blocking I/O on socket. */
  {
    int one = 1;
    if (ioctl (nm->socket, FIONBIO, &one) < 0)
      {
	error = clib_error_return_unix (0, "ioctl FIONBIO");
	goto done;
      }
  }

  {
    int rx_buffer_size = 128*1024;
    if (setsockopt (nm->socket, SOL_SOCKET, SO_RCVBUF, &rx_buffer_size, sizeof (rx_buffer_size)) < 0)
      {
	error = clib_error_return_unix (0, "setsockopt SO_RCVBUF");
	goto done;
      }
  }

  {
    int tx_buffer_size = 128*1024;
    if (setsockopt (nm->socket, SOL_SOCKET, SO_SNDBUF, &tx_buffer_size, sizeof (tx_buffer_size)) < 0)
      {
	error = clib_error_return_unix (0, "setsockopt SO_SNDBUF");
	goto done;
      }
  }

  {
    unix_file_t template = {0};
    template.read_function = netlink_read_ready;
    template.write_function = netlink_write_ready;
    template.file_descriptor = nm->socket;
    nm->unix_file_index_for_socket = unix_file_add (&unix_main, &template);
  }

  /* Query kernel databases we care about. */
  netlink_tx_gen_request (nm, RTM_GETLINK, AF_PACKET);
  netlink_tx_gen_request (nm, RTM_GETADDR, AF_INET);
  netlink_tx_gen_request (nm, RTM_GETROUTE, AF_INET);
  netlink_tx_gen_request (nm, RTM_GETADDR, AF_INET6);
  netlink_tx_gen_request (nm, RTM_GETROUTE, AF_INET6);

  vlib_register_node (vm, &netlink_process_node);
  nm->netlink_process_node_index = netlink_process_node.index;

  if ((error = vlib_call_init_function (vm, netlink_msg_init)))
    return error;

 done:
  if (error)
    {
      if (nm->socket >= 0)
	close (nm->socket);
    }

  return error;
}

VLIB_INIT_FUNCTION (netlink_init);
