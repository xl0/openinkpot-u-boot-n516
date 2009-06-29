/*
 * Copyright (c) 2009, Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * I2C driver for JZSOCs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
*/

#include <common.h>
#include <config.h>
#include <i2c.h>
#include <asm/errno.h>
#if defined(CONFIG_JZ4730)
#include <asm/jz4730.h>
#endif
#if defined(CONFIG_JZ4740)
#include <asm/jz4740.h>
#endif
#if defined(CONFIG_JZ5730)
#include <asm/jz5730.h>
#endif

void i2c_init(int speed, int slaveaddr)
{
	__i2c_set_clk(CONFIG_SYS_EXTAL, speed); /* default 10 KHz */
}

#define I2C_READ 1
#define I2C_WRITE 0
#define I2C_TIMEOUT 100

int i2c_read(uchar chip, uint addr, int alen, uchar *buffer, int len)
{
	unsigned char *b = buffer;
	unsigned long timeout;
	int ret = 0;

	if (alen) {
		printf("I2C devices with registers are not supported\n");
		return -1;
	}

	__i2c_enable();
	__i2c_clear_drf();

	if (len)
		__i2c_send_ack();
	else
		__i2c_send_nack();

	__i2c_send_start();
	__i2c_write((chip << 1) | I2C_READ);

	timeout = I2C_TIMEOUT;
	__i2c_set_drf();

	while (__i2c_transmit_ended() && timeout--)
		udelay(1000);
	while (!__i2c_transmit_ended());

	if (!__i2c_received_ack()) {
		ret = -EINVAL;
		goto end;
	}

	while (len--) {
		timeout = I2C_TIMEOUT;
		while (!__i2c_check_drf() && timeout--)
				udelay(1000);

		if (!timeout) {
			debug( "DRF timeout, length = %d\n", len);
			debug("%s: ACKF: %s\n", __func__, __i2c_received_ack() ? "ACK" : "NACK");
			ret = -ETIMEDOUT;
			goto end;
		}

		if (len == 1) {
			__i2c_send_nack();
			__i2c_send_stop();
		}

		*b++ = __i2c_read();

		__i2c_clear_drf();
	}
end:
	__i2c_disable();

	return ret;
}

int i2c_write(uchar chip, uint addr, int alen, uchar *buffer, int len)
{
	int l = len + 1;
	int timeout;

	if (alen) {
		printf("I2C devices with registers are not supported\n");
		return -1;
	}

	__i2c_enable();
	__i2c_clear_drf();
	__i2c_send_start();

	while (l--) {

		if (l == len)
			__i2c_write(chip << 1);
		else
			__i2c_write(*buffer++);

		__i2c_set_drf();

		timeout = I2C_TIMEOUT;
		while (__i2c_check_drf() && timeout--)
			udelay(1000);

		if (!timeout) {
			debug("DRF timeout, length = %d\n", len);
//			__i2c_send_stop();
			__i2c_disable();
			return -ETIMEDOUT;
		}

		if (!__i2c_received_ack()) {
			debug("NAK has been received during write\n");
			__i2c_disable();
			return -EINVAL;
		}
	}

	__i2c_send_stop();
	while (!__i2c_transmit_ended());

	__i2c_disable();
	return 0;
}

