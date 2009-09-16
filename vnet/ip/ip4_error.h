/*
 * ip/ip4_error.h: ip4 fast path errors
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

#ifndef included_ip_ip4_error_h
#define included_ip_ip4_error_h

#define foreach_ip4_error				\
  /* Must be first. */					\
  _ (NONE, "no error")					\
							\
  /* Errors signalled by ip4-input */			\
  _ (TOO_SHORT, "length < 20 bytes")			\
  _ (BAD_LENGTH, "l3 length > l2 length")		\
  _ (BAD_CHECKSUM, "bad checksum")			\
  _ (VERSION, "version != 4")				\
  _ (OPTIONS, "options present")			\
  _ (FRAGMENT_OFFSET_ONE, "fragment offset == 1")	\
  _ (TIME_EXPIRED, "ttl <= 1")				\
							\
  /* Errors signalled by ip4-rewrite. */		\
  _ (MTU_EXCEEDED, "rewritten packet larger than MTU")	\
  _ (LOOKUP_MISS, "lookup miss")			\
  _ (ADJACENCY_DROP, "adjacency drop")			\
  _ (ADJACENCY_PUNT, "adjacency punt")			\
							\
  /* Errors signalled by ip4-local. */			\
  _ (UNKNOWN_PROTOCOL, "unknown ip protocol")		\
  _ (TCP_CHECKSUM, "bad tcp checksum")			\
  _ (UDP_CHECKSUM, "bad udp checksum")			\
  _ (UDP_LENGTH, "inconsistent udp/ip lengths")		\
							\
  /* Errors signalled by {tcp4,udp4}-lookup. */		\
  _ (UNKNOWN_UDP_PORT, "no listener for udp port")	\
  _ (UNKNOWN_TCP_PORT, "no listener for tcp port")

typedef enum {
#define _(sym,str) IP4_ERROR_##sym,
  foreach_ip4_error
#undef _
  IP4_N_ERROR,
} ip4_error_t;

#endif /* included_ip_ip4_error_h */
