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
 ethernet/cli.c					\
 ethernet/ethernet.h				\
 ethernet/error.def				\
 ethernet/format.c				\
 ethernet/init.c				\
 ethernet/interface.c				\
 ethernet/pg.c					\
 ethernet/phy.c					\
 ethernet/phy.h					\
 ethernet/phy_reg.h				\
 ethernet/types.def

# ethernet/node.c				

########################################
# Layer 3 protocols go here
########################################

########################################
# Layer 3 protocol: IP v4 (& v6)
########################################
# libvnet_a_SOURCES +=				\
#  ip/ip_checksum.c				\
#  ip/ip_format.c					\
#  ip/ip_init.c					\
#  ip/ip_lookup.h					\
#  ip/ip_lookup.c					\
#  ip/ip_format.h					\
#  ip/ip_main.h					\
#  ip/ip_packet.h					\
#  ip/protocols.def				\
#  ip/ports.def					\
#  ip/ip4.h					\
#  ip/ip4_error.def				\
#  ip/ip4_cli.c					\
#  ip/ip4_format.c				\
#  ip/ip4_forward.c				\
#  ip/ip4_input.c					\
#  ip/ip4_packet.h				\
#  ip/ip4_pg.c					\
#  ip/tcp_format.c				\
#  ip/tcp_init.c					\
#  ip/tcp_pg.c					\
#  ip/tcp_udp_lookup.c				\
#  ip/tcp_packet.h				\
#  ip/udp_format.c				\
#  ip/udp_init.c					\
#  ip/udp_packet.h				\
#  ip/udp_pg.c

########################################
# Packet generator
########################################

libvnet_a_SOURCES +=				\
  pg/pg.h					\
  pg/cli.c					\
  pg/edit.c					\
  pg/edit.h					\
  pg/init.c					\
  pg/input.c					\
  pg/output.c					\
  pg/stream.c

nobase_include_HEADERS +=			\
  pg/pg.h					\
  pg/edit.h

lib_LIBRARIES = libvnet.a

noinst_PROGRAMS = vnet_unix

vnet_unix_SOURCES = example/main_stub.c
vnet_unix_LDADD = -lvlib_unix -lvlib -lclib -lpthread -ldl
vnet_unix_LDFLAGS = -static
