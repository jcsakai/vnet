/* 
 * unix/netlink_msg.c - netlink message handling
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

#include <vnet/unix/netlink.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ip/ip.h>

#include <linux/if.h>		/* for IFF_* */
#include <linux/if_arp.h>	/* for ARPHRD_* */
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>

static uword
netlink_interface_tx (vlib_main_t * vm,
		      vlib_node_runtime_t * node,
		      vlib_frame_t * f)
{
  netlink_main_t * nm = &netlink_main;
  vnet_interface_output_runtime_t * rd = (void *) node->runtime_data;
  netlink_interface_t * ni = pool_elt_at_index (nm->interface_pool, rd->dev_instance);
  u32 * from, n_left_from, expected_n_bytes_written;
  struct sockaddr_ll sa;
  struct msghdr mh;

  from = vlib_frame_vector_args (f);
  n_left_from = f->n_vectors;
  memset (&sa, 0, sizeof (sa));  
  sa.sll_family = AF_PACKET;
  sa.sll_ifindex = ni->unix_if_index;

  memset (&mh, 0, sizeof (mh));
  mh.msg_name = &sa;
  mh.msg_namelen = sizeof (sa);
  vec_reset_length (nm->iovecs);
  expected_n_bytes_written = 0;

  while (n_left_from > 0)
    {
      vlib_buffer_t * b;
      struct iovec * iov;
      i32 n_written;

      b = vlib_get_buffer (vm, from[0]);
      from += 1;
      n_left_from -= 1;

      vec_add2 (nm->iovecs, iov, 1);
      iov->iov_base = b->data + b->current_data;
      iov->iov_len = b->current_length;
      expected_n_bytes_written += b->current_length;

      if (! (b->flags & VLIB_BUFFER_NEXT_PRESENT))
	{
	  mh.msg_iov = nm->iovecs;
	  mh.msg_iovlen = vec_len (nm->iovecs);
	  n_written = sendmsg (nm->packet_socket, &mh, /* flags */ 0);
	  if (n_written != expected_n_bytes_written)
	    clib_unix_error ("sendmsg");
	  expected_n_bytes_written = 0;
	  _vec_len (nm->iovecs) = 0;
	}
    }

  return f->n_vectors;
}

static clib_error_t *
netlink_interface_admin_up_down (vnet_main_t * vm, u32 hw_if_index, u32 flags)
{
  vnet_hw_interface_t * hif = vnet_get_hw_interface (vm, hw_if_index);
  uword is_up = (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP) != 0;
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni = pool_elt_at_index (nm->interface_pool, hif->dev_instance);

  if (is_up == ((ni->current_unix_flags & IFF_UP) != 0))
    return 0;

  /* FIXME send a netlink message to bring up/down the interface. */
  ASSERT (0);

  return /* no error */ 0;
}

static void netlink_clear_hw_interface_counters (u32 dev_instance)
{
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni = pool_elt_at_index (nm->interface_pool, dev_instance);
  ASSERT (ni == 0);
#if 0
  netlink_update_counters (xd);
  memcpy (xd->counters_last_clear, xd->counters, sizeof (xd->counters));
#endif
}

static u8 * format_netlink_device_name (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni = pool_elt_at_index (nm->interface_pool, dev_instance);
  return format (s, "unix-%v", ni->unix_name);
}

static u8 * format_netlink_device (u8 * s, va_list * args)
{
  u32 dev_instance = va_arg (*args, u32);
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni = pool_elt_at_index (nm->interface_pool, dev_instance);
  return format (s, "%v", ni->unix_name);
}

VNET_DEVICE_CLASS (netlink_device_class) = {
    .name = "netlink",
    .tx_function = netlink_interface_tx,
    .format_device_name = format_netlink_device_name,
    .format_device = format_netlink_device,
    .clear_counters = netlink_clear_hw_interface_counters,
    .admin_up_down_function = netlink_interface_admin_up_down,
};

static void netlink_rx_add_del_link (struct nlmsghdr * h, uword is_del)
{
  vnet_main_t * vnm = &vnet_main;
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni;
  vnet_hw_interface_t * hi;
  struct ifinfomsg * i = nlmsg_contents (h);
  struct nlattr * a;
  u8 * name = 0;
  u8 * address = 0;
  u32 * mtu = 0;

  foreach_netlink_message_attribute (a, h, i)
    {
      void * c = a + 1;
      switch (a->nla_type)
	{
	case IFLA_ADDRESS:
	  address = c;
	  break;

	case IFLA_IFNAME:
	  name = c;
	  break;

	case IFLA_MTU:
	  mtu = c;
	  break;
	}
    }

  /* All interfaces must have a name. */
  ASSERT (name != 0);

  /* Get or create interface. */
  vec_validate_init_empty (nm->device_instance_by_unix_if_index, i->ifi_index, ~0);
  if (nm->device_instance_by_unix_if_index[i->ifi_index] == ~0)
    {
      pool_get (nm->interface_pool, ni);
      ni->unix_name = format (0, "%s", name);
      ni->unix_if_index = i->ifi_index;
      ni->vnet_hw_if_index = ~0;
      ni->vnet_sw_if_index = ~0;
      nm->device_instance_by_unix_if_index[i->ifi_index] = ni - nm->interface_pool;

      switch (i->ifi_type)
	{
	case ARPHRD_ETHER:
	  {
	    clib_error_t * error;

	    error = ethernet_register_interface
	      (vnm,
	       netlink_device_class.index,
	       /* device_instance */ ni - nm->interface_pool,
	       address,
	       /* phy */ 0,
	       &ni->vnet_hw_if_index);

	    ASSERT (! error);	/* can't happen if phy = 0. */
	    clib_error_free (error);
	  }
	  break;

	default:
	  ni->vnet_hw_if_index = ~0;
	  break;
	}
    }
  else
    ni = pool_elt_at_index (nm->interface_pool, nm->device_instance_by_unix_if_index[i->ifi_index]);

  ni->current_unix_flags = i->ifi_flags;

  /* Interfaces we don't know about. */
  if (ni->vnet_hw_if_index == ~0)
    return;

  hi = vnet_get_hw_interface (vnm, ni->vnet_hw_if_index);
  ni->vnet_sw_if_index = hi->sw_if_index;

  /* Set admin/link state. */
  vnet_sw_interface_set_flags (vnm, hi->sw_if_index,
			       (i->ifi_flags & IFF_UP) ? VNET_SW_INTERFACE_FLAG_ADMIN_UP : 0);
  vnet_hw_interface_set_flags (vnm, hi->hw_if_index,
			       (i->ifi_flags & IFF_RUNNING) ? VNET_HW_INTERFACE_FLAG_LINK_UP : 0);

  if (mtu)
    hi->max_l3_packet_bytes[VLIB_TX] = *mtu;
}

static void netlink_rx_add_del_addr (struct nlmsghdr * h, uword is_del)
{
  vlib_main_t * vm = &vlib_global_main;
  netlink_main_t * nm = &netlink_main;
  netlink_interface_t * ni;
  struct ifaddrmsg * i = nlmsg_contents (h);
  struct nlattr * a;
  void * address = 0;

  ni = netlink_interface_by_unix_index (nm, i->ifa_index);
  if (ni->vnet_hw_if_index == ~0)
    return;

  foreach_netlink_message_attribute (a, h, i)
    {
      void * c = a + 1;
      switch (a->nla_type)
	{
	case IFA_ADDRESS:
	  address = c;
	  break;
	}
    }

  ASSERT (address != 0);
  switch (i->ifa_family)
    {
    case AF_INET:
      ip4_add_del_interface_address (vm, ni->vnet_sw_if_index, address, i->ifa_prefixlen, is_del);
      break;

    case AF_INET6:
      ip6_add_del_interface_address (vm, ni->vnet_sw_if_index, address, i->ifa_prefixlen, is_del);
      break;

    default:
      ASSERT (0);
    }
}

static void netlink_rx_add_del_route (struct nlmsghdr * h, uword is_del)
{
}

static void netlink_rx_add_del_neighbor (struct nlmsghdr * h, uword is_del)
{
}

static clib_error_t *
netlink_msg_init (vlib_main_t * vm)
{
  netlink_main_t * nm = &netlink_main;

  netlink_register_rx_handler (RTM_NEWLINK, netlink_rx_add_del_link, /* is_del */ 0);
  netlink_register_rx_handler (RTM_DELLINK, netlink_rx_add_del_link, /* is_del */ 1);

  netlink_register_rx_handler (RTM_NEWADDR, netlink_rx_add_del_addr, /* is_del */ 0);
  netlink_register_rx_handler (RTM_DELADDR, netlink_rx_add_del_addr, /* is_del */ 1);

  netlink_register_rx_handler (RTM_NEWROUTE, netlink_rx_add_del_route, /* is_del */ 0);
  netlink_register_rx_handler (RTM_DELROUTE, netlink_rx_add_del_route, /* is_del */ 1);

  netlink_register_rx_handler (RTM_NEWNEIGH, netlink_rx_add_del_neighbor, /* is_del */ 0);
  netlink_register_rx_handler (RTM_DELNEIGH, netlink_rx_add_del_neighbor, /* is_del */ 1);

  nm->packet_socket = socket (AF_PACKET, SOCK_RAW, htons (ETH_P_ALL));

  return /* no error */ 0;
}

VLIB_INIT_FUNCTION (netlink_msg_init);
