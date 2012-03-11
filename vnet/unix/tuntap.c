/* 
 *------------------------------------------------------------------
 * tuntap.c - kernel stack (reverse) punt/inject path
 *
 * Copyright (c) 2009 Cisco Systems, Inc. 
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
 *------------------------------------------------------------------
 */

#include <fcntl.h>		/* for open */
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <sys/uio.h>		/* for iovec */
#include <netinet/in.h>

#include <linux/if_arp.h>
#include <linux/if_tun.h>

#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

#include <vnet/ip/ip.h>

typedef struct {
  u32 sw_if_index;
  u8 is_v6;
  u8 addr[16];
} subif_address_t;

typedef struct {
  /* Vector of iovecs for readv/writev calls. */
  struct iovec * iovecs;

  /* Vector of VLIB rx buffers to use.  We allocate them in blocks
     of VLIB_FRAME_SIZE (256). */
  u32 * rx_buffers;

  /* File descriptors for /dev/net/tun and provisioning socket. */
  int dev_net_tun_fd, dev_tap_fd;

  /* Interface MTU in bytes and # of default sized buffers. */
  u32 mtu_bytes, mtu_buffers;

  /* Linux interface name for tun device. */
  char * tun_name;

  /* Pool of subinterface addresses */
  subif_address_t *subifs;

  /* Hash for subif addresses */
  mhash_t subif_mhash;

  u32 unix_file_index;

  /* VLIB hardware/software interfaces for tuntap interface. */
  u32 hw_if_index, sw_if_index;
} tuntap_main_t;

static tuntap_main_t tuntap_main = {
  .tun_name = "vnet",

  /* Suitable defaults for an Ethernet-like tun/tap device */
  .mtu_bytes = 4096 + 256,
};

/*
 * tuntap_tx
 * Output node, writes the buffers comprising the incoming frame 
 * to the tun/tap device, aka hands them to the Linux kernel stack.
 * 
 */
static uword
tuntap_tx (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  u32 * buffers = vlib_frame_args (frame);
  uword n_packets = frame->n_vectors;
  tuntap_main_t * tm = &tuntap_main;
  int i;

  for (i = 0; i < n_packets; i++)
    {
      struct iovec * iov;
      vlib_buffer_t * b;
      uword l;

      b = vlib_get_buffer (vm, buffers[i]);

      /* Re-set iovecs if present. */
      if (tm->iovecs)
	_vec_len (tm->iovecs) = 0;

      /* VLIB buffer chain -> Unix iovec(s). */
      vec_add2 (tm->iovecs, iov, 1);
      iov->iov_base = b->data + b->current_data;
      iov->iov_len = l = b->current_length;

      if (PREDICT_FALSE (b->flags & VLIB_BUFFER_NEXT_PRESENT))
	{
	  do {
	    b = vlib_get_buffer (vm, b->next_buffer);

	    vec_add2 (tm->iovecs, iov, 1);

	    iov->iov_base = b->data + b->current_data;
	    iov->iov_len = b->current_length;
	    l += b->current_length;
	  } while (b->flags & VLIB_BUFFER_NEXT_PRESENT);
	}

      if (writev (tm->dev_net_tun_fd, tm->iovecs, vec_len (tm->iovecs)) < l)
	clib_unix_warning ("writev");
    }
    
  vlib_buffer_free (vm, buffers, n_packets);
    
  return n_packets;
}

static VLIB_REGISTER_NODE (tuntap_tx_node) = {
  .function = tuntap_tx,
  .name = "tuntap-tx",
  .type = VLIB_NODE_TYPE_INTERNAL,
  .vector_size = 4,
};

enum {
  TUNTAP_RX_NEXT_IP4_INPUT, 
  TUNTAP_RX_NEXT_IP6_INPUT, 
  TUNTAP_RX_NEXT_DROP,
  TUNTAP_RX_N_NEXT,
};

static uword
tuntap_rx (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  tuntap_main_t * tm = &tuntap_main;
  vlib_buffer_t * b;
  u32 bi;
  const uword buffer_size = VLIB_BUFFER_DEFAULT_FREE_LIST_BYTES;

  /* Make sure we have some RX buffers. */
  {
    uword n_left = vec_len (tm->rx_buffers);
    uword n_alloc;

    if (n_left < VLIB_FRAME_SIZE / 2)
      {
	if (! tm->rx_buffers)
	  vec_alloc (tm->rx_buffers, VLIB_FRAME_SIZE);

	n_alloc = vlib_buffer_alloc (vm, tm->rx_buffers + n_left, VLIB_FRAME_SIZE - n_left);

	_vec_len (tm->rx_buffers) = n_left + n_alloc;
      }
  }

  /* Allocate RX buffers from end of rx_buffers.
     Turn them into iovecs to pass to readv. */
  {
    uword i_rx = vec_len (tm->rx_buffers) - 1;
    vlib_buffer_t * b;
    word i, n_bytes_left, n_bytes_in_packet;

    /* We should have enough buffers left for an MTU sized packet. */
    ASSERT (vec_len (tm->rx_buffers) >= tm->mtu_buffers);

    vec_validate (tm->iovecs, tm->mtu_buffers - 1);
    for (i = 0; i < tm->mtu_buffers; i++)
      {
	b = vlib_get_buffer (vm, tm->rx_buffers[i_rx - i]);
	tm->iovecs[i].iov_base = b->data;
	tm->iovecs[i].iov_len = buffer_size;
      }

    n_bytes_left = readv (tm->dev_net_tun_fd, tm->iovecs, tm->mtu_buffers);
    n_bytes_in_packet = n_bytes_left;
    if (n_bytes_left <= 0)
      {
        if (errno != EAGAIN)
          clib_unix_warning ("readv %d", n_bytes_left);
	return 0;
      }

    bi = tm->rx_buffers[i_rx];
    while (1)
      {
	b = vlib_get_buffer (vm, tm->rx_buffers[i_rx]);

	b->flags = 0;
	b->current_data = 0;
	b->current_length = n_bytes_left < buffer_size ? n_bytes_left : buffer_size;

	n_bytes_left -= buffer_size;

	if (n_bytes_left <= 0)
	  break;

	i_rx--;
	b->flags |= VLIB_BUFFER_NEXT_PRESENT;
	b->next_buffer = tm->rx_buffers[i_rx];
      }

    /* Interface counters for tuntap interface. */
    vlib_increment_combined_counter (vnet_main.interface_main.combined_sw_if_counters
				     + VNET_INTERFACE_COUNTER_RX,
				     tm->sw_if_index,
				     1, n_bytes_in_packet);

    _vec_len (tm->rx_buffers) = i_rx;
  }

  if (CLIB_DEBUG > 0)
    {
      u8 * msg = vlib_validate_buffer (vm, bi, /* follow_buffer_next */ 1);
      if (msg)
        ASSERT (0);
    }

  b = vlib_get_buffer (vm, bi);

  {
    u32 next_index;
    uword n_trace = vlib_get_trace_count (vm, node);

    vnet_buffer (b)->sw_if_index[VLIB_RX] = tm->sw_if_index;
    b->error = node->errors[0];

    switch (b->data[0] & 0xf0)
      {
      case 0x40:
        next_index = TUNTAP_RX_NEXT_IP4_INPUT;
        break;
      case 0x60:
        next_index = TUNTAP_RX_NEXT_IP6_INPUT;
        break;
      default:
        next_index = TUNTAP_RX_NEXT_DROP;
        break;
      }

    vlib_set_next_frame_buffer (vm, node, next_index, bi);

    if (n_trace > 0)
      {
        vlib_trace_buffer (vm, node, next_index,
                           b, /* follow_chain */ 1);
        vlib_set_trace_count (vm, node, n_trace - 1);
      }
  }

  return 1;
}

static char * tuntap_rx_error_strings[] = {
  "unknown packet type",
};

static VLIB_REGISTER_NODE (tuntap_rx_node) = {
  .function = tuntap_rx,
  .name = "tuntap-rx",
  .type = VLIB_NODE_TYPE_INPUT,
  .state = VLIB_NODE_STATE_INTERRUPT,
  .vector_size = 4,
  .n_errors = 1,
  .error_strings = tuntap_rx_error_strings,

  .n_next_nodes = TUNTAP_RX_N_NEXT,
  .next_nodes = {
    [TUNTAP_RX_NEXT_IP4_INPUT] = "ip4-input-no-checksum",
    [TUNTAP_RX_NEXT_IP6_INPUT] = "ip6-input",
    [TUNTAP_RX_NEXT_DROP] = "error-drop",
  },
};

/* Gets called when file descriptor is ready from epoll. */
static clib_error_t * tuntap_read_ready (unix_file_t * uf)
{
  vlib_main_t * vm = &vlib_global_main;
  vlib_node_set_interrupt_pending (vm, tuntap_rx_node.index);
  return 0;
}

/*
 * tuntap_exit
 * Clean up the tun/tap device
 */

static clib_error_t *
tuntap_exit (vlib_main_t * vm)
{
  tuntap_main_t *tm = &tuntap_main;
  struct ifreq ifr;
  int sfd;

  /* Not present. */
  if (! tm->dev_net_tun_fd || tm->dev_net_tun_fd < 0)
    return 0;

  sfd = socket (AF_INET, SOCK_STREAM, 0);
  if (sfd < 0)
    clib_unix_warning("provisioning socket");

  memset(&ifr, 0, sizeof (ifr));
  strcpy (ifr.ifr_name, tm->tun_name);

  /* get flags, modify to bring down interface... */
  if (ioctl (sfd, SIOCGIFFLAGS, &ifr) < 0)
    clib_unix_warning ("SIOCGIFFLAGS");

  ifr.ifr_flags &= ~(IFF_UP | IFF_RUNNING);

  if (ioctl (sfd, SIOCSIFFLAGS, &ifr) < 0)
    clib_unix_warning ("SIOCSIFFLAGS");

  /* Turn off persistence */
  if (ioctl (tm->dev_net_tun_fd, TUNSETPERSIST, 0) < 0)
    clib_unix_warning ("TUNSETPERSIST");
  close(tm->dev_tap_fd);
  close(tm->dev_net_tun_fd);
  close (sfd);

  return 0;
}

VLIB_EXIT_FUNCTION (tuntap_exit);

static clib_error_t *
tuntap_config (vlib_main_t * vm, unformat_input_t * input)
{
  tuntap_main_t *tm = &tuntap_main;
  clib_error_t * error = 0;
  struct ifreq ifr;
  int flags = IFF_TUN | IFF_NO_PI;
  int disabled = 0;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "mtu %d", &tm->mtu_bytes))
	;
      else if (unformat (input, "disable"))
        disabled = 1;

      else
	return clib_error_return (0, "unknown input `%U'",
				  format_unformat_error, input);
    }

  tm->dev_net_tun_fd = -1;
  tm->dev_tap_fd = -1;

  if (disabled)
    return 0;

  if (geteuid()) 
    {
      clib_warning ("tuntap disabled: must be superuser");
      return 0;
    }    

  if ((tm->dev_net_tun_fd = open ("/dev/net/tun", O_RDWR)) < 0)
    {
      error = clib_error_return_unix (0, "open /dev/net/tun");
      goto done;
    }

  memset (&ifr, 0, sizeof (ifr));
  strcpy(ifr.ifr_name, tm->tun_name);
  ifr.ifr_flags = flags;
  if (ioctl (tm->dev_net_tun_fd, TUNSETIFF, (void *)&ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl TUNSETIFF");
      goto done;
    }
    
  /* Make it persistent, at least until we split. */
  if (ioctl (tm->dev_net_tun_fd, TUNSETPERSIST, 1) < 0)
    {
      error = clib_error_return_unix (0, "TUNSETPERSIST");
      goto done;
    }

  /* Open a provisioning socket */
  if ((tm->dev_tap_fd = socket(PF_PACKET, SOCK_RAW,
			       htons(ETH_P_ALL))) < 0 )
    {
      error = clib_error_return_unix (0, "socket");
      goto done;
    }

  /* Find the interface index. */
  {
    struct ifreq ifr;
    struct sockaddr_ll sll;

    memset (&ifr, 0, sizeof(ifr));
    strcpy (ifr.ifr_name, tm->tun_name);
    if (ioctl (tm->dev_tap_fd, SIOCGIFINDEX, &ifr) < 0 )
      {
	error = clib_error_return_unix (0, "ioctl SIOCGIFINDEX");
	goto done;
      }

    /* Bind the provisioning socket to the interface. */
    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(tm->dev_tap_fd, (struct sockaddr*) &sll, sizeof(sll)) < 0)
      {
	error = clib_error_return_unix (0, "bind");
	goto done;
      }
  }

  /* non-blocking I/O on /dev/tapX */
  {
    int one = 1;
    if (ioctl (tm->dev_net_tun_fd, FIONBIO, &one) < 0)
      {
	error = clib_error_return_unix (0, "ioctl FIONBIO");
	goto done;
      }
  }

  tm->mtu_buffers = tm->mtu_bytes / VLIB_BUFFER_DEFAULT_FREE_LIST_BYTES;
  if (tm->mtu_bytes % VLIB_BUFFER_DEFAULT_FREE_LIST_BYTES)
    tm->mtu_buffers += 1;

  ifr.ifr_mtu = tm->mtu_bytes;
  if (ioctl (tm->dev_tap_fd, SIOCSIFMTU, &ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl SIOCSIFMTU");
      goto done;
    }

  /* get flags, modify to bring up interface... */
  if (ioctl (tm->dev_tap_fd, SIOCGIFFLAGS, &ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl SIOCGIFFLAGS");
      goto done;
    }

  ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);

  if (ioctl (tm->dev_tap_fd, SIOCSIFFLAGS, &ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl SIOCSIFFLAGS");
      goto done;
    }

  {
    unix_file_t template = {0};
    template.read_function = tuntap_read_ready;
    template.file_descriptor = tm->dev_net_tun_fd;
    tm->unix_file_index = unix_file_add (&unix_main, &template);
  }

 done:
  if (error)
    {
      if (tm->dev_net_tun_fd >= 0)
	close (tm->dev_net_tun_fd);
      if (tm->dev_tap_fd >= 0)
	close (tm->dev_tap_fd);
    }

  return error;
}

VLIB_CONFIG_FUNCTION (tuntap_config, "tuntap");

void
tuntap_ip4_add_del_interface_address (ip4_main_t * im,
				      uword opaque,
				      u32 sw_if_index,
				      ip4_address_t * address,
				      u32 address_length,
				      u32 if_address_index,
				      u32 is_delete)
{
  tuntap_main_t * tm = &tuntap_main;
  struct ifreq ifr;
  subif_address_t subif_addr, * ap;
  uword * p;

  /* Tuntap disabled. */
  if (tm->dev_tap_fd < 0)
    return;

  /* See if we already know about this subif */
  memset (&subif_addr, 0, sizeof (subif_addr));
  subif_addr.sw_if_index = sw_if_index;
  memcpy (&subif_addr.addr, address, sizeof (*address));
  
  p = mhash_get (&tm->subif_mhash, &subif_addr);

  if (p)
    ap = pool_elt_at_index (tm->subifs, p[0]);
  else
    {
      pool_get (tm->subifs, ap);
      *ap = subif_addr;
      mhash_set (&tm->subif_mhash, ap, ap - tm->subifs, 0);
    }

  /* Use subif pool index to select alias device. */
  memset (&ifr, 0, sizeof (ifr));
  sprintf (ifr.ifr_name, "%s:%d", tm->tun_name, (int)(ap - tm->subifs));

  if (! is_delete)
    {
      struct sockaddr_in * sin;

      sin = (struct sockaddr_in *)&ifr.ifr_addr;

      /* Set ipv4 address, netmask. */
      sin->sin_family = AF_INET;
      memcpy (&sin->sin_addr.s_addr, address, 4);
      if (ioctl (tm->dev_tap_fd, SIOCSIFADDR, &ifr) < 0)
	clib_unix_warning ("ioctl SIOCSIFADDR");
    
      sin->sin_addr.s_addr = im->fib_masks[address_length];
      if (ioctl (tm->dev_tap_fd, SIOCSIFNETMASK, &ifr) < 0)
	clib_unix_warning ("ioctl SIOCSIFNETMASK");
    }
  else
    {
      mhash_unset (&tm->subif_mhash, &subif_addr, 0 /* old value ptr */);
      pool_put (tm->subifs, ap);
    }

  /* get flags, modify to bring up interface... */
  if (ioctl (tm->dev_tap_fd, SIOCGIFFLAGS, &ifr) < 0)
    clib_unix_warning ("ioctl SIOCGIFFLAGS");

  if (is_delete)
    ifr.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
  else
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);

  if (ioctl (tm->dev_tap_fd, SIOCSIFFLAGS, &ifr) < 0)
    clib_unix_warning ("ioctl SIOCSIFFLAGS");
}

/*
 * $$$$ gross workaround for a known #include bug 
 * #include <linux/ipv6.h> causes multiple definitions if
 * netinet/in.h is also included.
 */
struct in6_ifreq {
	struct in6_addr	ifr6_addr;
        u32		ifr6_prefixlen;
	int		ifr6_ifindex; 
};

/* 
 * Both the v6 interface address API and the way ifconfig
 * displays subinterfaces differ from their v4 couterparts.
 * The code given here seems to work but YMMV.
 */
void
tuntap_ip6_add_del_interface_address (ip6_main_t * im,
				      uword opaque,
				      u32 sw_if_index,
				      ip6_address_t * address,
				      u32 address_length,
				      u32 if_address_index,
				      u32 is_delete)
{
  tuntap_main_t * tm = &tuntap_main;
  struct ifreq ifr;
  struct in6_ifreq ifr6;
  subif_address_t subif_addr, * ap;
  uword * p;

  /* Tuntap disabled. */
  if (tm->dev_tap_fd < 0)
    return;

  /* See if we already know about this subif */
  memset (&subif_addr, 0, sizeof (subif_addr));
  subif_addr.sw_if_index = sw_if_index;
  subif_addr.is_v6 = 1;
  memcpy (&subif_addr.addr, address, sizeof (*address));
  
  p = mhash_get (&tm->subif_mhash, &subif_addr);

  if (p)
    ap = pool_elt_at_index (tm->subifs, p[0]);
  else
    {
      pool_get (tm->subifs, ap);
      *ap = subif_addr;
      mhash_set (&tm->subif_mhash, ap, ap - tm->subifs, 0);
    }

  /* Use subif pool index to select alias device. */
  memset (&ifr, 0, sizeof (ifr));
  memset (&ifr6, 0, sizeof (ifr6));
  sprintf (ifr.ifr_name, "%s:%d", tm->tun_name, (int)(ap - tm->subifs));

  if (! is_delete)
    {
      int sockfd = socket (AF_INET6, SOCK_STREAM, 0);
      if (sockfd < 0)
        clib_unix_warning ("get ifindex socket");

      if (ioctl (sockfd, SIOGIFINDEX, &ifr) < 0)
        clib_unix_warning ("get ifindex");

      ifr6.ifr6_ifindex = ifr.ifr_ifindex;
      ifr6.ifr6_prefixlen = address_length;
      memcpy (&ifr6.ifr6_addr, address, 16);

      if (ioctl (sockfd, SIOCSIFADDR, &ifr6) < 0)
        clib_unix_warning ("set address");

      close (sockfd);
    }
  else
    {
      int sockfd = socket (AF_INET6, SOCK_STREAM, 0);
      if (sockfd < 0)
        clib_unix_warning ("get ifindex socket");

      if (ioctl (sockfd, SIOGIFINDEX, &ifr) < 0)
        clib_unix_warning ("get ifindex");

      ifr6.ifr6_ifindex = ifr.ifr_ifindex;
      ifr6.ifr6_prefixlen = address_length;
      memcpy (&ifr6.ifr6_addr, address, 16);

      if (ioctl (sockfd, SIOCDIFADDR, &ifr6) < 0)
        clib_unix_warning ("del address");

      close (sockfd);

      mhash_unset (&tm->subif_mhash, &subif_addr, 0 /* old value ptr */);
      pool_put (tm->subifs, ap);
    }
}

static void
tuntap_punt_frame (vlib_main_t * vm,
                   vlib_node_runtime_t * node,
                   vlib_frame_t * frame)
{
  tuntap_tx (vm, node, frame);
  vlib_frame_free (vm, node, frame);
}

static VNET_HW_INTERFACE_CLASS (tuntap_interface_class) = {
  .name = "Linux punt/inject (tuntap)",
};

static u8 * format_tuntap_interface_name (u8 * s, va_list * args)
{
  /* Calling it "tuntap" makes 2 nodes called tuntap-tx. */
  s = format (s, "tuntap-0");
  return s;
}

static uword
tuntap_dummy_tx (vlib_main_t * vm,
		 vlib_node_runtime_t * node,
		 vlib_frame_t * frame)
{
  u32 * buffers = vlib_frame_args (frame);
  uword n_buffers = frame->n_vectors;
  vlib_buffer_free (vm, buffers, n_buffers);
  return n_buffers;
}

static VNET_DEVICE_CLASS (tuntap_dev_class) = {
  .name = "tuntap",
  .tx_function = tuntap_dummy_tx,
  .format_device_name = format_tuntap_interface_name,
};

static clib_error_t *
tuntap_init (vlib_main_t * vm)
{
  clib_error_t * error;
  vnet_main_t * vnm = &vnet_main;
  ip4_main_t * im4 = &ip4_main;
  ip6_main_t * im6 = &ip6_main;
  ip4_add_del_interface_address_callback_t cb4;
  ip6_add_del_interface_address_callback_t cb6;
  tuntap_main_t * tm = &tuntap_main;

  error = vlib_call_init_function (vm, ip4_init);
  if (error)
    return error;

  mhash_init (&tm->subif_mhash, sizeof (u32), sizeof(subif_address_t));

  cb4.function = tuntap_ip4_add_del_interface_address;
  cb4.function_opaque = 0;
  vec_add1 (im4->add_del_interface_address_callbacks, cb4);

  cb6.function = tuntap_ip6_add_del_interface_address;
  cb6.function_opaque = 0;
  vec_add1 (im6->add_del_interface_address_callbacks, cb6);

  vm->os_punt_frame = tuntap_punt_frame;

  {
    vnet_hw_interface_t * hi;

    tm->hw_if_index = vnet_register_interface
      (vnm,
       tuntap_dev_class.index, 0,
       tuntap_interface_class.index, 0);
    hi = vnet_get_hw_interface (vnm, tm->hw_if_index);
    tm->sw_if_index = hi->sw_if_index;

    /* Interface is always up. */
    vnet_hw_interface_set_flags (vnm, tm->hw_if_index, VNET_HW_INTERFACE_FLAG_LINK_UP);
    vnet_sw_interface_set_flags (vnm, tm->sw_if_index, VNET_SW_INTERFACE_FLAG_ADMIN_UP);
  }

  return 0;
}

VLIB_INIT_FUNCTION (tuntap_init);
