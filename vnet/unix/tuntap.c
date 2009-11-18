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
#include <stropts.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#include <clib/byte_order.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

typedef struct {
    u32 unix_file_index;
    i8 *input_buffer;
    int dev_tap_fd;
    int dev_net_tun_fd;
    u32 punt_node_index;
    u32 mtu;
    i8 *tap_name;
    u8 mac_address[6];
} tuntap_main_t;

tuntap_main_t *tuntap_main;
static char *inject_node_name = "ethernet-input";

enum {
    /* Yes, this name is confusing. Sorry. */
    PUNT_INJECT, 
};

/*
 * tuntap_punt
 * Output node, writes the buffers comprising the incoming frame 
 * to the tun/tap device, aka hands them to the Linux kernel stack.
 * 
 * We dynamically register a single "next", the
 * graph node into which we *inject* packets. This is a hack which happens
 * to rhyme nicely with the dispatch macros.
 */
static uword
tuntap_punt (vlib_main_t * vm,
	     vlib_node_runtime_t * node,
	     vlib_frame_t * frame)
{
    u32 * buffers = vlib_frame_args (frame);
    uword n_buffers = frame->n_vectors;
    tuntap_main_t *ttm = tuntap_main;
    int i;
    vlib_buffer_t *b;
    i8 *datap;
    int n;
    int length, length_of_packet;

    for (i = 0; i < n_buffers; i++) {
	b = vlib_get_buffer (vm, buffers[i]);

        length_of_packet = vlib_buffer_n_bytes_in_chain (vm, buffers[i]);

	if (length_of_packet > ttm->mtu) {
	    clib_warning ("pkt length %d >  mtu %d, discarded",
			  b->current_length, ttm->mtu);
	    continue;
	}
	/* Manual scatter-gather. One write per pkt on tun/tap */
	if (PREDICT_FALSE (b->flags & VLIB_BUFFER_NEXT_PRESENT)) {
	    i8 *ocp = ttm->input_buffer;
	    int nbytes_left = length_of_packet;
	    int nbytes_this_buffer;
	    
	    length = nbytes_left;

	    while (nbytes_left > 0) {
		nbytes_this_buffer = clib_min (nbytes_left, b->current_length);
		memcpy (ocp, b->data + b->current_data, nbytes_this_buffer);
		ocp += nbytes_this_buffer;
		nbytes_left -= nbytes_this_buffer;
		b = vlib_get_buffer (vm, b->next_buffer);
	    }

	    datap = ttm->input_buffer;
	} else {
	    /* small pkt, write directly from the buffer */
	    length = b->current_length;
	    datap = (i8 *) (b->data + b->current_data);
	}

	n = write (ttm->dev_net_tun_fd, datap, length_of_packet);

	if (n < length_of_packet) {
	    clib_unix_warning ("write");
	}
    }
    
    vlib_buffer_free (vm, buffers, /* next buffer stride */ 1, n_buffers,
		      /* follow_buffer_next */ 1);
    
    return n_buffers;
}

VLIB_REGISTER_NODE (tuntap_punt_node) = {
  .function = tuntap_punt,
  .name = "tuntap_punt",
  .type = VLIB_NODE_TYPE_INTERNAL,
  .vector_size = 4,
};

/*
 * tuntap_read
 * Non-blocking read function which injects
 * one packet at a time into the forwarding graph
 */

static clib_error_t * tuntap_read (unix_file_t * uf)
{
    int n;
    tuntap_main_t *ttm = tuntap_main;
    vlib_main_t *vm = (vlib_main_t *) uf->private_data;
    static u32 *buffers = 0;
    int this_buffer_index;
    int nbytes_this_buffer;
    int nbuffers;
    int n_bytes_left;
    i8 *icp;
    vlib_buffer_t *b = 0;
    vlib_node_runtime_t *node_runtime;
    u32 * to_next, n_left;
    /* $$$$ this should be a #define, see also vlib_buffer_init() */
    int default_buffer_size = 512;

    n = read (ttm->dev_net_tun_fd, ttm->input_buffer, 
	      vec_len(ttm->input_buffer));

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

    icp = ttm->input_buffer;
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

#if DEBUG 
    {
        u8 * msg = vlib_validate_buffer (vm, buffers[0],
                                         /* follow_buffer_next */ 1);
        if (msg)
            clib_warning ("%v", msg);
    }
#endif

    node_runtime = vlib_node_get_runtime (vm, ttm->punt_node_index);

    vlib_get_next_frame (vm, node_runtime, PUNT_INJECT, to_next, n_left);
    to_next[0] = buffers[0];
    n_left -= 1;
    vlib_put_next_frame (vm, node_runtime, PUNT_INJECT, n_left);

    {
        uword n_trace = vlib_get_trace_count (vm, node_runtime);
        if (n_trace > 0) {
            vlib_trace_buffer (vm, node_runtime, PUNT_INJECT, b,
                               /* follow_chain */ 1);
            vlib_set_trace_count (vm, node_runtime, n_trace - 1);
	}
    }

    _vec_len (buffers) = 0;

    return 0;
}

/*
 * tuntap_exit
 * Clean up the tun/tap device
 */

static clib_error_t *
tuntap_exit (vlib_main_t * vm)
{
    struct ifreq ifr;
    int sfd;

    if (tuntap_main == 0)
	return 0;

    sfd = socket (AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
	clib_unix_warning("provisioning socket");
    }

    memset(&ifr, 0, sizeof (ifr));
    strcpy (ifr.ifr_name, (char *) tuntap_main->tap_name);

    /* get flags, modify to bring down interface... */
    if (ioctl (sfd, SIOCGIFFLAGS, &ifr) < 0) {
	clib_unix_warning ("SIOCGIFFLAGS");
    }

    ifr.ifr_flags &= ~(IFF_UP | IFF_RUNNING);

    if (ioctl (sfd, SIOCSIFFLAGS, &ifr) < 0) {
	clib_unix_warning ("SIOCSIFFLAGS");
    }

    /* Turn off persistence */
    if (ioctl (tuntap_main->dev_net_tun_fd, TUNSETPERSIST, 0) < 0) {
	clib_unix_warning ("TUNSETPERSIST");
    }
    close(tuntap_main->dev_tap_fd);
    close(tuntap_main->dev_net_tun_fd);
    close (sfd);

    return 0;
}

VLIB_EXIT_FUNCTION (tuntap_exit);

int open_raw(char *iface)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_ll sll;
    struct packet_mreq mreq;

    /* Open a provisioning socket */
    if ((fd = socket(PF_PACKET, SOCK_RAW,
		     htons(ETH_P_ALL))) < 0 ) {
        clib_unix_warning("socket");
        return(-1);
    }

    /* Find the interface index */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0 ) {
        clib_unix_warning("SIOCGIFINDEX");
	close(fd);
        return(-1);
    }

    /* bind the provisioning socket to the interface */
    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    if(bind(fd, (struct sockaddr*) &sll, sizeof(sll)) < 0 ) {
        clib_unix_warning("bind");
	close(fd);
        return(-1);
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.mr_ifindex = ifr.ifr_ifindex;
    mreq.mr_type = PACKET_MR_PROMISC;
    mreq.mr_alen = 0; // sizeof (tuntap_main->mac_address);
    memcpy (mreq.mr_address, tuntap_main->mac_address, 
	    sizeof (tuntap_main->mac_address));

    if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
		   &mreq, sizeof(mreq)) < 0) {
	clib_unix_warning("PACKET_ADD_MEMBERSHIP");
	close(fd);
	return (-1);
    }

    return(fd);
}

static clib_error_t *
tuntap_config (vlib_main_t * vm, unformat_input_t * input)
{
    int ttfd = -1, sfd = -1;
    int one = 1;
    struct ifreq ifr;
    struct sockaddr_in *sin;
    u32 ipv4_addr = 0xC0A80101; /* 192.168.1.1 */
    unix_file_t template = {0};
    /* Suitable defaults for an Ethernet-like tun/tap device */
    int mtu = 4096 + 256;
    char *tap_name = "tap9";
    int flags = IFF_TAP | IFF_NO_PI;
    u8 mac_address [6] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
    tuntap_main_t *ttm;

    while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT) {
	/* $$$ configure tap vs. tun, flags, mac address, mtu */
	break;
    }

    ttfd = open ("/dev/net/tun", O_RDWR);

    if (ttfd < 0) {
	clib_unix_warning("open /dev/net/tun");
	return 0;
    }

    vec_validate(tuntap_main, 0);
    ttm = tuntap_main;

    ttm->tap_name = (i8 *)tap_name;
    memcpy (ttm->mac_address, mac_address, sizeof (ttm->mac_address));

    memset (&ifr, 0, sizeof (ifr));
    sin = (struct sockaddr_in *)&ifr.ifr_addr;

    /* Pick an arbitrary device number */
    strcpy(ifr.ifr_name, tap_name);
    ifr.ifr_flags = flags;
    if (ioctl(ttfd, TUNSETIFF, (void *)&ifr) < 0) {
	clib_unix_warning("TUNSETIFF");
    barf:
	close (ttfd);
	close (sfd);
	vec_free (tuntap_main);
	return 0;
    }
    
    /* make it persistent, at least until we split */
    if (ioctl (ttfd, TUNSETPERSIST, 1) < 0) {
	clib_unix_warning ("TUNSETPERSIST");
    }

    ttm->dev_net_tun_fd = ttfd;

    sfd = open_raw (tap_name);
    if (sfd < 0) {
	clib_unix_warning("socket");
	goto barf;
    }
    /* non-blocking I/O on /dev/tapX */
    if (ioctl (sfd, FIONBIO, &one) < 0) {
	clib_unix_warning ("FIONBIO");
	goto barf;
    }

    ttm->dev_tap_fd = sfd;

    /* Set ipv4 address, netmask, bring it up */
    memset (&ifr, 0, sizeof (ifr));
    strcpy (ifr.ifr_name, tap_name);
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl (ipv4_addr);
    
    if (ioctl (sfd, SIOCSIFADDR, &ifr) < 0) {
	clib_unix_warning("SIOCSIFADDR");
	goto barf;
    }
    
    /* /16 netmask */
    sin->sin_addr.s_addr = htonl (0xFFFF0000);
    
    if (ioctl (sfd, SIOCSIFNETMASK, &ifr) < 0) {
	clib_unix_warning("SIOCSIFNETMASK");
	goto barf;
    }

    ifr.ifr_mtu = mtu;
    ttm->mtu = mtu;
    if (ioctl (sfd, SIOCSIFMTU, &ifr) < 0) {
	clib_unix_warning("SIOCSIFMTU");
    }

    /* MAC address only makes sense with a tap device */
    if (flags & IFF_TAP) {
        ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        memcpy (ifr.ifr_hwaddr.sa_data, ttm->mac_address, 
                sizeof (ttm->mac_address));

        if (ioctl (sfd, SIOCSIFHWADDR, &ifr) < 0) {
            clib_unix_warning("SIOCSIFHWADDR");
        }
    }

    /* get flags, modify to bring up interface... */
    if (ioctl (sfd, SIOCGIFFLAGS, &ifr) < 0) {
	clib_unix_warning ("SIOCGIFFLAGS");
	goto barf;
    }

    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);

    if (ioctl (sfd, SIOCSIFFLAGS, &ifr) < 0) {
	clib_unix_warning ("SIOCSIFFLAGS");
	goto barf;
    }

    /* Graph topology setup */
    ttm->punt_node_index = tuntap_punt_node.index;
    if (vlib_node_add_named_next_with_slot (vm, ttm->punt_node_index,
                                            inject_node_name, PUNT_INJECT)
        != PUNT_INJECT) {
        vlib_panic_with_msg (vm, "tuntap inject point '%s' missing",
                             inject_node_name);
    }

    /* Set up the unix_file object... */
    vec_validate(ttm->input_buffer, mtu-1);

    template.read_function = tuntap_read;
    template.file_descriptor = ttm->dev_net_tun_fd;
    template.error_function = tuntap_read;
    template.private_data = (uword) vm;
    ttm->unix_file_index = unix_file_add (&unix_main, &template);

    return 0;
}

VLIB_CONFIG_FUNCTION (tuntap_config, "tuntap");

/* call in main() to force the linker to load this module... */
clib_error_t *
tuntap_init (vlib_main_t * vm)
{
  return 0;
}

VLIB_INIT_FUNCTION (tuntap_init);

/* 
 * register_tuntap_inject_node_name
 *
 * If you want pkts injected somewhere other than ethernet-input,
 * call this function to register a different inject point... 
 */

void register_tuntap_inject_node_name (char *name)
{
    inject_node_name = name;
}