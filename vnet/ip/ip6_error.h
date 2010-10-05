/*
 * ip/ip6_error.h: ip6 fast path errors
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

#ifndef included_ip_ip6_error_h
#define included_ip_ip6_error_h

#define foreach_ip6_error				\
  /* Must be first. */					\
  _ (NONE, "valid ip6 packets")				\
							\
  /* Errors signalled by ip6-input */			\
  _ (TOO_SHORT, "ip6 length < 40 bytes")		\
  _ (BAD_LENGTH, "ip6 length > l2 length")		\
  _ (VERSION, "ip6 version != 4")			\
  _ (TIME_EXPIRED, "ip6 ttl <= 1")			\
							\
  /* Errors signalled by ip6-rewrite. */		\
  _ (MTU_EXCEEDED, "ip6 MTU exceeded")			\
  _ (DST_LOOKUP_MISS, "ip6 destination lookup miss")	\
  _ (SRC_LOOKUP_MISS, "ip6 source lookup miss")		\
  _ (ADJACENCY_DROP, "ip6 adjacency drop")		\
  _ (ADJACENCY_PUNT, "ip6 adjacency punt")		\
							\
  /* Errors signalled by ip6-local. */			\
  _ (UNKNOWN_PROTOCOL, "unknown ip protocol")		\
  _ (TCP_CHECKSUM, "bad tcp checksum")			\
  _ (UDP_CHECKSUM, "bad udp checksum")			\
  _ (UDP_LENGTH, "inconsistent udp/ip lengths")		\
							\
  /* Errors signalled by {tcp6,udp6}-lookup. */		\
  _ (UNKNOWN_UDP_PORT, "no listener for udp port")	\
  _ (UNKNOWN_TCP_PORT, "no listener for tcp port")

typedef enum {
#define _(sym,str) IP6_ERROR_##sym,
  foreach_ip6_error
#undef _
  IP6_N_ERROR,
} ip6_error_t;

#endif /* included_ip_ip6_error_h */
