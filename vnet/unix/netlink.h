/* 
 * unix/netlink.h - interact with linux kernel net stack
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

#ifndef included_vnet_unix_netlink_h
#define included_vnet_unix_netlink_h

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <vnet/vnet.h>

typedef struct {
  /* Total number of messages added to history. */
  u32 n_messages;

  /* Circular buffer of messages. */
  u8 * messages[64];
} netlink_message_history_side_t;

typedef void (netlink_rx_message_handler_t) (void * opaque, struct nlmsghdr * msg);

typedef struct {
  netlink_rx_message_handler_t * handler;
  void * opaque;
} netlink_rx_message_handler_and_opaque_t;

typedef struct {
  /* Kernel netlink socket. */
  int socket;

  /* VLIB unix file index corresponding to socket. */
  u32 unix_file_index_for_socket;

  /* Lookup table of RX handlers indexed by message type. */
  netlink_rx_message_handler_and_opaque_t * rx_handler_by_message_type;

  /* Current message being created. */
  u8 * tx_buffer;

  /* Fifo of transmit buffers one for each message to be sent. */
  u8 ** tx_fifo;

  u32 tx_sequence_number;

  /* VLIB node index of netlink-process. */
  u32 netlink_process_node_index;

  netlink_message_history_side_t history_sides[VLIB_N_RX_TX];
} netlink_main_t;

netlink_main_t netlink_main;

always_inline void
netlink_add_to_message_history (netlink_main_t * nm, vlib_rx_or_tx_t side, u8 * msg)
{
  netlink_message_history_side_t * s = &nm->history_sides[side];
  u32 i = s->n_messages++ % ARRAY_LEN (s->messages);
  vec_free (s->messages[i]);
  s->messages[i] = msg;
}

always_inline void *
nlmsg_next (struct nlmsghdr * h)
{
  ASSERT (h->nlmsg_len > 0);
  return (void *) h + NLMSG_ALIGN (h->nlmsg_len);
}

always_inline void *
nlmsg_contents (struct nlmsghdr * h)
{ return (void *) h + NLMSG_ALIGN (sizeof (h[0])); }

always_inline struct nlattr *
nlattr_next (struct nlattr * a)
{
  ASSERT (a->nla_len > 0);
  return (void *) a + NLMSG_ALIGN (a->nla_len);
}

#define foreach_netlink_message_header(h,v)				\
  for ((h) = (void *) (v); (void *) (h) < (void *) vec_end (v); (h) = nlmsg_next (h))

#define foreach_netlink_message_attribute(a,h,p)				\
  for ((a) = (void *) ((p) + 1); (void *) (a) < (void *) h + NLMSG_ALIGN ((h)->nlmsg_len); (a) = nlattr_next (a))

#define foreach_netlink_sub_attribute(a,a_sup)				\
  for ((a) = (void *) ((a_sup) + 1); (void *) (a) < (void *) a_sup + NLMSG_ALIGN ((a_sup)->nla_len); (a) = nlattr_next (a))

void * netlink_tx_add_request_with_flags (u32 type, u32 n_bytes, u32 flags);
void * netlink_tx_add_attr (u32 attr_type, u32 attr_len);

always_inline void *
netlink_tx_add_request (u32 type, u32 n_bytes)
{ return netlink_tx_add_request_with_flags (type, n_bytes, /* flags */ 0); }

always_inline void
netlink_register_rx_handler (u32 type, netlink_rx_message_handler_t * handler, void * opaque)
{
  netlink_main_t * nm = &netlink_main;
  vec_validate (nm->rx_handler_by_message_type, type);
  nm->rx_handler_by_message_type[type].handler = handler;
  nm->rx_handler_by_message_type[type].opaque = opaque;
}

#endif /* included_vnet_unix_netlink_h */
