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

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_arp.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>

#include <clib/byte_order.h>

#include <fcntl.h>		/* for open */
#include <sys/stat.h>
#include <sys/uio.h>		/* for iovec */

#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

typedef struct {
  u8 *input_buffer;

  /* File descriptors for /dev/net/tun and provisioning socket. */
  int dev_net_tun_fd, dev_tap_fd;

  /* Interface MTU. */
  u32 mtu;

  u32 read_ready;

  /* Linux interface name for tun device. */
  char * tun_name;

  u32 ip4_address;
  u32 ip4_address_length;

  u32 unix_file_index;
} tuntap_main_t;

static tuntap_main_t tuntap_main = {
  .tun_name = "fabric",

  /* Suitable defaults for an Ethernet-like tun/tap device */
  .mtu = 4096 + 256,
  
  /* 192.168.1.1/16 */
  .ip4_address = 0xC0A80101,
  .ip4_address_length = 16,
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
  uword n_buffers = frame->n_vectors;
  tuntap_main_t *ttm = &tuntap_main;
  int i;
  vlib_buffer_t *b;
  u8 *packet;
  int length_of_packet;

  for (i = 0; i < n_buffers; i++)
    {
      u32 bi = buffers[i];
      b = vlib_get_buffer (vm, bi);

      length_of_packet = vlib_buffer_n_bytes_in_chain (vm, bi);
      if (length_of_packet > ttm->mtu)
	{
	  clib_warning ("pkt length %d >  mtu %d, discarded",
			b->current_length, ttm->mtu);
	  continue;
	}

      /* Manual scatter-gather. One write per pkt on tun/tap */
      if (PREDICT_FALSE (b->flags & VLIB_BUFFER_NEXT_PRESENT))
	{
	  ASSERT (vec_len (ttm->input_buffer) >= ttm->mtu);
	  length_of_packet = vlib_buffer_contents (vm, bi, ttm->input_buffer);
	  packet = ttm->input_buffer;
	}
      else
	{
	  /* small pkt, write directly from the buffer */
	  length_of_packet = b->current_length;
	  packet = (b->data + b->current_data);
	}

      if (write (ttm->dev_net_tun_fd, packet, length_of_packet) < length_of_packet)
	clib_unix_warning ("write");
    }
    
  vlib_buffer_free (vm, buffers, /* next buffer stride */ 1, n_buffers,
		    /* follow_buffer_next */ 1);
    
  return n_buffers;
}

static VLIB_REGISTER_NODE (tuntap_tx_node) = {
  .function = tuntap_tx,
  .name = "tuntap-tx",
  .type = VLIB_NODE_TYPE_INTERNAL,
  .vector_size = 4,
};

enum {
  TUNTAP_PUNT_NEXT_ETHERNET_INPUT, 
};

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
  int n;
  tuntap_main_t * tm = &tuntap_main;
  static u32 *buffers = 0;
  int this_buffer_index;
  int nbytes_this_buffer;
  int nbuffers;
  int n_bytes_left;
  u8 *icp;
  vlib_buffer_t *b = 0;
  u32 * to_next;
  /* $$$$ this should be a #define, see also vlib_buffer_init() */
  int default_buffer_size = 512;

  if (! tm->read_ready)
    return 0;
  tm->read_ready = 0;

  n = read (tm->dev_net_tun_fd, tm->input_buffer, 
	    vec_len(tm->input_buffer));

  if (n <= 0) {
    clib_unix_warning ("read returned %d", n);
    return 0;
  }

  nbuffers = (n + (default_buffer_size - 1)) / default_buffer_size;
  vec_validate (buffers, nbuffers - 1);

  if (vlib_buffer_alloc (vm, buffers, nbuffers) != nbuffers) {
    clib_warning ("buffer allocation failure");
    return 0;
  }

  icp = tm->input_buffer;
  this_buffer_index = 0;
  n_bytes_left = n;

  while (n_bytes_left > 0) {
    nbytes_this_buffer = clib_min (n_bytes_left, default_buffer_size);

    b = vlib_get_buffer (vm, buffers[this_buffer_index]);
	
    memcpy (b->data + b->current_data, icp, nbytes_this_buffer);
    n_bytes_left -= nbytes_this_buffer;
    icp += nbytes_this_buffer;

    b->current_length = nbytes_this_buffer;

    if (this_buffer_index < nbuffers - 1) {
      b->flags |= VLIB_BUFFER_NEXT_PRESENT;
      b->next_buffer = buffers[this_buffer_index+1];
    } else {
      b->flags &= ~VLIB_BUFFER_NEXT_PRESENT;
      b->next_buffer = 0xdeadbeef; 
    }

    this_buffer_index++;
  }

  if (DEBUG > 0)
    {
      u8 * msg = vlib_validate_buffer (vm, buffers[0],
				       /* follow_buffer_next */ 1);
      if (msg)
	clib_warning ("%v", msg);
    }

  /* 
   * If it's a TUN device, add (space for) dst + src MAC address, 
   * to the left of the protocol number. Linux shim hdr: 
   * (u16 flags, u16 protocol-id). Overwrite flags; use 10 octets of
   * the pre-data area.
   */
  {
    b = vlib_get_buffer (vm, buffers[0]);
    b->current_data -= 10;
    b->current_length += 10;
  }

  to_next = vlib_set_next_frame (vm, node, TUNTAP_PUNT_NEXT_ETHERNET_INPUT);
  to_next[0] = buffers[0];

  {
    uword n_trace = vlib_get_trace_count (vm, node);
    if (n_trace > 0) {
      vlib_trace_buffer (vm, node, TUNTAP_PUNT_NEXT_ETHERNET_INPUT,
			 b, /* follow_chain */ 1);
      vlib_set_trace_count (vm, node, n_trace - 1);
    }
  }

  _vec_len (buffers) = 0;

  return 1;
}

static VLIB_REGISTER_NODE (tuntap_rx_node) = {
  .function = tuntap_rx,
  .name = "tuntap-rx",
  .type = VLIB_NODE_TYPE_INPUT,
  .vector_size = 4,
  .n_next_nodes = 1,
  .next_nodes = {
    [TUNTAP_PUNT_NEXT_ETHERNET_INPUT] = "ethernet-input",
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
  if (! tm->dev_net_tun_fd)
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
  int flags = IFF_TUN;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      break;
    }

  tm->dev_net_tun_fd = -1;
  tm->dev_tap_fd = -1;

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

  /* Set ipv4 address, netmask, bring it up */
  memset (&ifr, 0, sizeof (ifr));
  strcpy (ifr.ifr_name, tm->tun_name);
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = htonl (tm->ip4_address);
  if (ioctl (tm->dev_tap_fd, SIOCSIFADDR, &ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl SIOCSIFADDR");
      goto done;
    }
    
  sin->sin_addr.s_addr = htonl (~ pow2_mask (tm->ip4_address_length));
  if (ioctl (tm->dev_tap_fd, SIOCSIFNETMASK, &ifr) < 0)
    {
      error = clib_error_return_unix (0, "ioctl SIOCSIFNETMASK");
      goto done;
    }

  ifr.ifr_mtu = tm->mtu;
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

  /* Set up the unix_file object... */
  vec_resize (tm->input_buffer, tm->mtu);

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

/* call in main() to force the linker to load this module... */
static clib_error_t *
tuntap_init (vlib_main_t * vm)
{
  return 0;
}

VLIB_INIT_FUNCTION (tuntap_init);
