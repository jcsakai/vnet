AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CFLAGS = -Wall

libvnet_a_SOURCES =

nobase_include_HEADERS =

########################################
# Layer 2 protocols go here
########################################

########################################
# Layer 2 protocol: Ethernet
########################################
libvnet_a_SOURCES +=				\
 vnet/ethernet/cli.c				\
 vnet/ethernet/error.def			\
 vnet/ethernet/format.c				\
 vnet/ethernet/init.c				\
 vnet/ethernet/interface.c			\
 vnet/ethernet/node.c				\
 vnet/ethernet/pg.c				\
 vnet/ethernet/phy.c				\
 vnet/ethernet/types.def

nobase_include_HEADERS +=			\
 vnet/ethernet/ethernet.h			\
 vnet/ethernet/phy.h				\
 vnet/ethernet/phy_reg.h

########################################
# Layer 3 protocols go here
########################################

########################################
# Layer 3 protocol: IP v4 (& v6)
########################################
# libvnet_a_SOURCES +=				\
#  vnet/ip/ip_checksum.c				\
#  vnet/ip/ip_format.c					\
#  vnet/ip/ip_init.c					\
#  vnet/ip/ip_lookup.h					\
#  vnet/ip/ip_lookup.c					\
#  vnet/ip/ip_format.h					\
#  vnet/ip/ip_main.h					\
#  vnet/ip/ip_packet.h					\
#  vnet/ip/protocols.def				\
#  vnet/ip/ports.def					\
#  vnet/ip/ip4.h					\
#  vnet/ip/ip4_error.def				\
#  vnet/ip/ip4_cli.c					\
#  vnet/ip/ip4_format.c				\
#  vnet/ip/ip4_forward.c				\
#  vnet/ip/ip4_input.c					\
#  vnet/ip/ip4_packet.h				\
#  vnet/ip/ip4_pg.c					\
#  vnet/ip/tcp_format.c				\
#  vnet/ip/tcp_init.c					\
#  vnet/ip/tcp_pg.c					\
#  vnet/ip/tcp_udp_lookup.c				\
#  vnet/ip/tcp_packet.h				\
#  vnet/ip/udp_format.c				\
#  vnet/ip/udp_init.c					\
#  vnet/ip/udp_packet.h				\
#  vnet/ip/udp_pg.c

########################################
# Packet generator
########################################

libvnet_a_SOURCES +=				\
  vnet/pg/cli.c					\
  vnet/pg/edit.c				\
  vnet/pg/init.c				\
  vnet/pg/input.c				\
  vnet/pg/output.c				\
  vnet/pg/stream.c

nobase_include_HEADERS +=			\
  vnet/pg/pg.h					\
  vnet/pg/edit.h

lib_LIBRARIES = libvnet.a

noinst_PROGRAMS = vnet_unix

vnet_unix_SOURCES = example/main_stub.c
vnet_unix_LDADD = -lvlib_unix -lvlib -lclib -lpthread -ldl
vnet_unix_LDFLAGS = -static
