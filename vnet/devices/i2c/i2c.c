/*
 * Converted from xog xserver.
 *
 * Copyright (C) 1998 Itai Nahshon, Michael Schimek
 *
 * The original code was derived from and inspired by 
 * the I2C driver from the Linux kernel.
 *      (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 */

#include <vnet/devices/i2c/i2c.h>
#include <vlib/vlib.h>

/*
 * Most drivers will register just with GetBits/PutBits functions.
 * The following functions implement a software I2C protocol
 * by using the primitive functions given by the driver.
 *
 * It is assumed that there is just one master on the I2C bus, therefore
 * there is no explicit test for conflits.
 */

static inline void i2c_delay (i2c_bus_t * b, f64 timeout)
{ b->delay (b, timeout); }

/*
 * Some devices will hold SCL low to slow down the bus or until 
 * ready for transmission.
 *
 * This condition will be noticed when the master tries to raise
 * the SCL line. You can set the timeout to zero if the slave device
 * does not support this clock synchronization.
 */
static int
i2c_raise_scl (i2c_bus_t * b, int sda, f64 timeout)
{
  int scl;
  f64 t;

  b->put_bits (b, 1, sda);
  i2c_delay (b, b->rise_fall_time);

  for (t = timeout; t > 0; t -= b->rise_fall_time)
    {
      b->get_bits (b, &scl, &sda);
      if (scl)
	break;
      i2c_delay(b, b->rise_fall_time);
    }

  return t <= 0;
}

/* Send a start signal on the I2C bus. The start signal notifies
 * devices that a new transaction is initiated by the bus master.
 *
 * The start signal is always followed by a slave address.
 * Slave addresses are 8+ bits. The first 7 bits identify the
 * device and the last bit signals if this is a read (1) or
 * write (0) operation.
 *
 * There may be more than one start signal on one transaction.
 * This happens for example on some devices that allow reading
 * of registers. First send a start bit followed by the device
 * address (with the last bit 0) and the register number. Then send
 * a new start bit with the device address (with the last bit 1)
 * and then read the value from the device.
 *
 * Note this is function does not implement a multiple master
 * arbitration procedure.
 */

static int
i2c_start (i2c_bus_t * b, f64 timeout)
{
  int timed_out = i2c_raise_scl (b, 1, timeout);

  if (! timed_out)
    {
      b->put_bits (b, 1, 0);
      i2c_delay (b, b->hold_time);
      b->put_bits (b, 0, 0);
      i2c_delay (b, b->hold_time);
    }

  return timed_out;
}

/* This is the default I2CStop function if not supplied by the driver.
 *
 * Signal devices on the I2C bus that a transaction on the
 * bus has finished. There may be more than one start signal
 * on a transaction but only one stop signal.
 */

static void
i2c_stop (i2c_bus_t * b)
{
  b->put_bits (b, 0, 0);
  i2c_delay (b, b->rise_fall_time);

  b->put_bits (b, 1, 0);
  i2c_delay (b, b->hold_time);

  b->put_bits (b, 1, 1);
  i2c_delay (b, b->hold_time);
}

/* Write/Read a single bit to/from a device.
 * Return FALSE if a timeout occurs.
 */
static int
i2c_write_bit (i2c_bus_t * b, int sda, f64 timeout)
{
  int timed_out;

  b->put_bits (b, 0, sda);
  i2c_delay (b, b->rise_fall_time);

  timed_out = i2c_raise_scl (b, sda, timeout);
  i2c_delay (b, b->hold_time);

  b->put_bits (b, 0, sda);
  i2c_delay (b, b->hold_time);

  return timed_out;
}

static int
i2c_read_bit (i2c_bus_t * b, int * sda, f64 timeout)
{
  int timed_out, scl;

  timed_out = i2c_raise_scl (b, 1, timeout);
  i2c_delay (b, b->hold_time);

  b->get_bits (b, &scl, sda);

  b->put_bits (b, 0, 1);
  i2c_delay (b, b->hold_time);

  return timed_out;
}

/* This is the default put_byte function if not supplied by the driver.
 *
 * A single byte is sent to the device.
 * The function returns FALSE if a timeout occurs, you should send 
 * a stop condition afterwards to reset the bus.
 *
 * A timeout occurs,
 * if the slave pulls SCL to slow down the bus more than byte_timeout usecs,
 * or slows down the bus for more than bit_timeout usecs for each bit,
 * or does not send an ACK bit (0) to acknowledge the transmission within
 * ack_timeout usecs, but a NACK (1) bit.
 *
 * ack_timeout must be at least b->hold_time, the other timeouts can be
 * zero according to the comment on i2c_raise_scl.
 */

static int
i2c_put_byte (i2c_bus_t * b, u8 data)
{
  int timed_out;
  int i, scl, sda;
  f64 t;

  for (i = 7; i >= 0; i--)
    {
      timed_out =
	i2c_write_bit (b, (data >> i) & 1,
		       i == 7 ? b->byte_timeout : b->bit_timeout);
      if (timed_out)
	goto done;
    }

  b->put_bits (b, 0, 1);
  i2c_delay (b, b->rise_fall_time);

  timed_out = i2c_raise_scl (b, 1, b->hold_time);
  if (timed_out)
    goto done;

  for (t = b->ack_timeout; t > 0; t -= b->hold_time)
    {
      i2c_delay (b, b->hold_time);
      b->get_bits (b, &scl, &sda);
      if (sda == 0)
	break;
    }

  timed_out = t <= 0;

  b->put_bits (b, 0, 1);
  i2c_delay (b, b->hold_time);

 done:
  return timed_out;
}

/* This is the default I2CGetByte function if not supplied by the driver.
 *
 * A single byte is read from the device.
 * The function returns FALSE if a timeout occurs, you should send
 * a stop condition afterwards to reset the bus.
 * 
 * A timeout occurs,
 * if the slave pulls SCL to slow down the bus more than byte_timeout usecs,
 * or slows down the bus for more than b->bit_timeout usecs for each bit.
 *
 * byte_timeout must be at least b->hold_time, the other timeouts can be
 * zero according to the comment on i2c_raise_scl.
 *
 * For the <last> byte in a sequence the acknowledge bit NACK (1), 
 * otherwise ACK (0) will be sent.
 */

static int
i2c_get_byte (i2c_bus_t * b, u8 * data_return, int last)
{
  int i, timed_out, sda;
  u8 data = 0;

  b->put_bits (b, 0, 1);
  i2c_delay (b, b->rise_fall_time);

  for (i = 7; i >= 0; i--)
    {
      timed_out = i2c_read_bit (b, &sda,
				i == 7 ? b->byte_timeout : b->bit_timeout);
      if (timed_out)
	goto done;

      data |= (sda != 0) << i;
    }

  timed_out = i2c_write_bit (b, last != 0, b->bit_timeout);
  *data_return = data;

 done:
  return timed_out;
}

/* This is the default I2CAddress function if not supplied by the driver.
 *
 * It creates the start condition, followed by the d->dev_address.
 * Higher level functions must call this routine rather than
 * I2CStart/PutByte because a hardware I2C master may not be able 
 * to send a slave address without a start condition.
 *
 * The same timeouts apply as with I2CPutByte and additional a
 * start_timeout, similar to the byte_timeout but for the start 
 * condition.
 *
 * In case of a timeout, the bus is left in a clean idle condition.
 * I. e. you *must not* send a Stop. If this function succeeds, you *must*.
 *
 * The slave address format is 16 bit, with the legacy _8_bit_ slave address
 * in the least significant byte. This is, the slave address must include the
 * R/_W flag as least significant bit.
 *
 * The most significant byte of the address will be sent _after_ the LSB, 
 * but only if the LSB indicates:
 * a) an 11 bit address, this is LSB = 1111 0xxx.
 * b) a 'general call address', this is LSB = 0000 000x - see the I2C specs
 *    for more.
 */

static int
i2c_address (i2c_bus_t * b, u32 addr)
{
  int timed_out;

  timed_out = i2c_start (b, b->start_timeout);
  if (timed_out)
    goto done;

  timed_out = i2c_put_byte (b, addr & 0xff);
  if (timed_out)
    {
      i2c_stop (b);
      goto done;
    }

  if ((addr & 0xf8) != 0xf0
      && (addr & 0xfe) != 0x00)
    goto done;

  timed_out = i2c_put_byte (b, (addr >> 8) & 0xff);
 done:
  return timed_out;
}

/* These are the hardware independent I2C helper functions.
 * ========================================================
 */

/* Function for probing. Just send the slave address 
 * and return true if the device responds. The slave address
 * must have the lsb set to reflect a read (1) or write (0) access.
 * Don't expect a read- or write-only device will respond otherwise.
 */
int
i2c_probe_address (i2c_bus_t * b, u32 address)
{
  int timed_out;

  timed_out = i2c_address (b, address);
  if (! timed_out)
    i2c_stop (b);

  return timed_out;
}

/* All functions below are related to devices and take the
 * slave address and timeout values from an I2CDevRec. They
 * return FALSE in case of an error (presumably a timeout).
 */

/* General purpose read and write function.
 *
 * 1st, if n_write > 0
 *   Send a start condition
 *   Send the slave address (1 or 2 bytes) with write flag
 *   Write n bytes from write_buffer
 * 2nd, if n_read > 0
 *   Send a start condition [again]
 *   Send the slave address (1 or 2 bytes) with read flag
 *   Read n bytes to read_buffer
 * 3rd, if a Start condition has been successfully sent,
 *   Send a Stop condition.
 *
 * The functions exits immediately when an error occures,
 * not proceeding any data left. However, step 3 will
 * be executed anyway to leave the bus in clean idle state. 
 */

int
i2c_write_read (i2c_bus_t * b, u32 address,
		void * write_buffer, int n_write,
		void * read_buffer, int n_read)
{
  int timed_out, s = 0;
  u8 * p;

  timed_out = 0;
  if (n_write > 0)
    {
      timed_out = i2c_address (b, address & ~1);
      if (timed_out)
	goto done;

      for (p = write_buffer; n_write > 0; n_write--, p++)
	{
	  timed_out = i2c_put_byte (b, p[0]);
	  if (timed_out)
	    goto done;
	  s++;
	}
    }

  if (n_read > 0)
    {
      timed_out = i2c_address (b, address | 1);
      if (timed_out)
	goto done;

      for (p = read_buffer; n_read > 0; n_read--, p++)
	{
	  timed_out = i2c_get_byte (b, p, /* is_last */ n_read == 1);
	  if (timed_out)
	    goto done;
	  s++;
	}
    }

  if (s)
    i2c_stop (b);

 done:
  return timed_out;
}

static void vlib_i2c_delay (i2c_bus_t * b, f64 delay)
{
  vlib_main_t * vm = &vlib_global_main;
  /* Suspend makes everything just too slow. */
  vlib_time_wait (vm, delay);
}

void i2c_init (i2c_bus_t * b)
{
  if (b->hold_time < 2e-6)
    b->hold_time = 5e-6;

  if (! b->delay)
    b->delay = vlib_i2c_delay;

#define _(f) if (b->f <= 0) b->f = b->hold_time
  _ (bit_timeout);
  _ (byte_timeout);
  _ (ack_timeout);
  _ (start_timeout);
#undef _

  if (b->rise_fall_time <= 0)
    b->rise_fall_time = 2e-6;
}
