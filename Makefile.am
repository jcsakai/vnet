AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CFLAGS = -Wall

libvnet_la_SOURCES =

nobase_include_HEADERS =

########################################
# Generic stuff
########################################
libvnet_la_SOURCES +=					\
  vnet/vnet/config.c					\
  vnet/vnet/rewrite.c				

nobase_include_HEADERS +=			\
  vnet/vnet/config.h				\
  vnet/vnet/l3_types.h				\
  vnet/vnet/rewrite.h

########################################
# Layer 2 protocols go here
########################################

########################################
# Layer 2 protocol: Ethernet
########################################
libvnet_la_SOURCES +=				\
 vnet/ethernet/arp.c				\
 vnet/ethernet/cli.c				\
 vnet/ethernet/format.c				\
 vnet/ethernet/init.c				\
 vnet/ethernet/interface.c			\
 vnet/ethernet/node.c				\
 vnet/ethernet/pg.c				\
 vnet/ethernet/phy.c

nobase_include_HEADERS +=			\
 vnet/ethernet/error.def			\
 vnet/ethernet/ethernet.h			\
 vnet/ethernet/packet.h				\
 vnet/ethernet/phy.h				\
 vnet/ethernet/phy_reg.h			\
 vnet/ethernet/types.def

########################################
# Layer 2 protocol: PPP
########################################
libvnet_la_SOURCES +=				\
  vnet/ppp/node.c				\
  vnet/ppp/pg.c					\
  vnet/ppp/ppp.c

nobase_include_HEADERS +=			\
 vnet/ppp/error.def				\
 vnet/ppp/ppp.h					\
 vnet/ppp/packet.h

########################################
# Layer 2 protocol: HDLC
########################################
libvnet_la_SOURCES +=				\
  vnet/hdlc/node.c				\
  vnet/hdlc/pg.c				\
  vnet/hdlc/hdlc.c

nobase_include_HEADERS +=			\
 vnet/hdlc/error.def				\
 vnet/hdlc/hdlc.h				\
 vnet/hdlc/packet.h

########################################
# Layer 3 protocols go here
########################################

########################################
# Layer 3 protocol: IP v4 (& v6)
########################################
libvnet_la_SOURCES +=				\
 vnet/ip/format.c				\
 vnet/ip/icmp4.c				\
 vnet/ip/icmp6.c				\
 vnet/ip/ip46_cli.c				\
 vnet/ip/ip4_format.c				\
 vnet/ip/ip4_forward.c				\
 vnet/ip/ip4_input.c				\
 vnet/ip/ip4_pg.c				\
 vnet/ip/ip4_source_check.c			\
 vnet/ip/ip6_format.c				\
 vnet/ip/ip6_forward.c				\
 vnet/ip/ip6_input.c				\
 vnet/ip/ip6_pg.c				\
 vnet/ip/ip_checksum.c				\
 vnet/ip/ip.h					\
 vnet/ip/ip_init.c				\
 vnet/ip/lookup.c				\
 vnet/ip/tcp_format.c				\
 vnet/ip/tcp_init.c				\
 vnet/ip/tcp_pg.c				\
 vnet/ip/tcp_udp_lookup.c			\
 vnet/ip/udp_format.c				\
 vnet/ip/udp_init.c				\
 vnet/ip/udp_pg.c

nobase_include_HEADERS +=			\
 vnet/ip/format.h				\
 vnet/ip/icmp46_packet.h			\
 vnet/ip/ip.h					\
 vnet/ip/ip4.h					\
 vnet/ip/ip4_error.h				\
 vnet/ip/ip4_packet.h				\
 vnet/ip/ip6.h					\
 vnet/ip/ip6_error.h				\
 vnet/ip/ip6_packet.h				\
 vnet/ip/lookup.h				\
 vnet/ip/ip_packet.h				\
 vnet/ip/ports.def				\
 vnet/ip/protocols.def				\
 vnet/ip/tcp_packet.h				\
 vnet/ip/udp_packet.h

########################################
# Packet generator
########################################

libvnet_la_SOURCES +=				\
  vnet/pg/cli.c					\
  vnet/pg/edit.c				\
  vnet/pg/init.c				\
  vnet/pg/input.c				\
  vnet/pg/output.c				\
  vnet/pg/stream.c

nobase_include_HEADERS +=			\
  vnet/pg/pg.h					\
  vnet/pg/edit.h

########################################
# Hardware devices
########################################

libvnet_la_SOURCES +=				\
  vnet/devices/ethernet_phy_bcm.c

########################################
# Unix kernel related
########################################

libvnet_la_SOURCES +=				\
  vnet/unix/tuntap.c

nobase_include_HEADERS +=			\
  vnet/unix/tuntap.h

lib_LTLIBRARIES = libvnet.la

noinst_PROGRAMS = vnet_unix

vnet_unix_SOURCES = example/main_stub.c
vnet_unix_LDFLAGS = -static
vnet_unix_LDADD = libvnet.la -lvlib_unix -lvlib -lclib -lpthread -lm
