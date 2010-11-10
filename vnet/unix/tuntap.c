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
  /* Vector of iovecs for readv/writev calls. */
  struct iovec * iovecs;

  /* Vector of VLIB rx buffers to use.  We allocate them in blocks
     of VLIB_FRAME_SIZE (256). */
  u32 * rx_buffers;

  /* File descriptors for /dev/net/tun and provisioning socket. */
  int dev_net_tun_fd, dev_tap_fd;

  /* Interface MTU in bytes and # of default sized buffers. */
  u32 mtu_bytes, mtu_buffers;

  /* epoll call says we are ready to readv from socket. */
  u32 read_ready;

  /* Linux interface name for tun device. */
  char * tun_name;

  u32 unix_file_index;
} tuntap_main_t;

static tuntap_main_t tuntap_main = {
  .tun_name = "fabric",

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
    
  vlib_buffer_free (vm, buffers, /* next buffer stride */ 1, n_packets,
		    /* follow_buffer_next */ 1);
    
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

/* Gets called when file descriptor is ready from epoll. */
static clib_error_t * tuntap_read_ready (unix_file_t * uf)
{
  tuntap_main_t * tm = &tuntap_main;
  tm->read_ready = 1;
  return 0;
}

static uword
tuntap_rx (vlib_main_t * vm,
	   vlib_node_runtime_t * node,
	   vlib_frame_t * frame)
{
  tuntap_main_t * tm = &tuntap_main;
  vlib_buffer_t * b;
  u32 bi;
  const uword buffer_size = VLIB_BUFFER_DEFAULT_FREE_LIST_BYTES;

  /* Work to do? */
  if (! tm->read_ready)
    return 0;

  tm->read_ready = 0;

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
    word i, n_bytes_left;

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
    if (n_bytes_left <= 0)
      {
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

    _vec_len (tm->rx_buffers) = i_rx;
  }

  if (DEBUG > 0)
    {
      u8 * msg = vlib_validate_buffer (vm, bi, /* follow_buffer_next */ 1);
      if (msg)
	clib_warning ("%v", msg);
    }

  b = vlib_get_buffer (vm, bi);

  {
    u32 next_index;
    uword n_trace = vlib_get_trace_count (vm, node);

    b->error = node->errors[0];

    switch (b->data[0] & 0xf0)
      {
      case 0x40:
        next_index =  TUNTAP_RX_NEXT_IP4_INPUT;
        break;
      case 0x60:
        next_index =  TUNTAP_RX_NEXT_IP6_INPUT;
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
  struct sockaddr_in *sin;
  int flags = IFF_TUN | IFF_NO_PI;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "mtu %d", &tm->mtu_bytes))
	;

      else
	return clib_error_return (0, "unknown input `%U'",
				  format_unformat_error, input);
    }

  tm->dev_net_tun_fd = -1;
  tm->dev_tap_fd = -1;

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
  sin = (struct sockaddr_in *)&ifr.ifr_addr;
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
    if (ioctl (tm->dev_tap_fd, FIONBIO, &one) < 0)
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

static void
tuntap_ip4_add_del_interface_address (ip4_main_t * im,
				      uword opaque,
				      u32 sw_if_index,
				      ip4_address_t * address,
				      u32 address_length,
				      u32 is_delete)
{
  tuntap_main_t * tm = &tuntap_main;
  struct ifreq ifr;

  /* Tuntap disabled. */
  if (tm->dev_tap_fd < 0)
    return;

  /* Use VLIB sw_if_index to select alias device. */
  memset (&ifr, 0, sizeof (ifr));
  sprintf (ifr.ifr_name, "%s:%d", tm->tun_name, sw_if_index);

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

static void
tuntap_punt_frame (vlib_main_t * vm,
                   vlib_frame_t * frame)
{
  tuntap_tx (vm, /* node */ 0, frame);
}

static clib_error_t *
tuntap_init (vlib_main_t * vm)
{
  clib_error_t * error;
  ip4_main_t * im = &ip4_main;
  ip4_add_del_interface_address_callback_t cb;

  error = vlib_call_init_function (vm, ip4_init);
  if (error)
    return error;

  cb.function = tuntap_ip4_add_del_interface_address;
  cb.function_opaque = 0;
  vec_add1 (im->add_del_interface_address_callbacks, cb);

  vm->os_punt_frame = tuntap_punt_frame;

  return 0;
}

VLIB_INIT_FUNCTION (tuntap_init);
