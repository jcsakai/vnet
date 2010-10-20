/*
 * Converted from xog xserver.
 *
 * Copyright (C) 1998 Itai Nahshon, Michael Schimek
 *
 * The original code was derived from and inspired by 
 * the I2C driver from the Linux kernel.
 *      (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 */

#ifndef included_i2c_i2c_h
#define included_i2c_i2c_h

#include <clib/types.h>

typedef struct i2c_bus_t {
  uword private;

  void (* put_bits) (struct i2c_bus_t * b, int  scl, int  sda);
  void (* get_bits) (struct i2c_bus_t * b, int *scl, int *sda);
  void (* delay) (struct i2c_bus_t * b, f64 delay);

  /* All timeouts in seconds. */
  f64 hold_time; 	/* 1 / bus clock frequency, 5 or 2 usec */
  f64 rise_fall_time;

  f64 bit_timeout;
  f64 byte_timeout;
  f64 ack_timeout;
  f64 start_timeout;
} i2c_bus_t;

void i2c_init (i2c_bus_t * bus);

int i2c_probe_address (i2c_bus_t * bus, u32 address);

int i2c_write_read (i2c_bus_t * b,
		    u32 address,
		    void * write_buffer, int n_write,
		    void * read_buffer,  int n_read);

static inline int
i2c_read (i2c_bus_t * b, u32 address, void * buffer, int n)
{ return i2c_write_read (b, address, 0, 0, buffer, n); }

static inline int
i2c_write (i2c_bus_t * b, u32 address, void * buffer,  int n)
{ return i2c_write_read (b, address, buffer, n, 0, 0); }

#endif /* included_i2c_i2c_h */
