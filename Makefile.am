AUTOMAKE_OPTIONS = foreign subdir-objects

AM_CFLAGS = -Wall

libvnet_a_SOURCES =

nobase_include_HEADERS =

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
