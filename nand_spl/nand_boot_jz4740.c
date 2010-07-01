/*
 * Copyright (C) 2007 Ingenic Semiconductor Inc.
 * Author: Peter <jlwei@ingenic.cn>
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
#include <nand.h>

#include <asm/io.h>
#include <asm/jz4740.h>

#define CONFIG_ECC_RS

/*
 * NAND flash definitions
 */

#define NAND_DATAPORT	0xb8000000
#define NAND_ADDRPORT	0xb8010000
#define NAND_COMMPORT	0xb8008000


#ifdef CONFIG_ECC_HM
#define ECC_BLOCK	256
#define ECC_POS		40
#define	PAR_SIZE	3
#else
#define ECC_BLOCK	512
#define ECC_POS		6
#define PAR_SIZE	9
#endif

#define __nand_read_hm_ecc()   (REG_EMC_NFECC & 0x00ffffff)

#define __nand_enable()		(REG_EMC_NFCSR |= EMC_NFCSR_NFE1 | EMC_NFCSR_NFCE1)
#define __nand_disable()	(REG_EMC_NFCSR &= ~(EMC_NFCSR_NFCE1))
#define __nand_ecc_rs_encoding() \
	(REG_EMC_NFECR = EMC_NFECR_ECCE | EMC_NFECR_ERST | EMC_NFECR_RS | EMC_NFECR_RS_ENCODING)
#define __nand_ecc_rs_decoding() \
	(REG_EMC_NFECR = EMC_NFECR_ECCE | EMC_NFECR_ERST | EMC_NFECR_RS | EMC_NFECR_RS_DECODING)
#define __nand_ecc_encode_sync() while (!(REG_EMC_NFINTS & EMC_NFINTS_ENCF))
#define __nand_ecc_decode_sync() while (!(REG_EMC_NFINTS & EMC_NFINTS_DECF))

#define __nand_ecc_enable()    (REG_EMC_NFECR = EMC_NFECR_ECCE | EMC_NFECR_ERST )
#define __nand_ecc_disable()   (REG_EMC_NFECR &= ~EMC_NFECR_ECCE)

#define __nand_select_hm_ecc() (REG_EMC_NFECR &= ~EMC_NFECR_RS )
#define __nand_select_rs_ecc() (REG_EMC_NFECR |= EMC_NFECR_RS)


static inline void __nand_dev_ready(void)
{
	unsigned int timeout = 10000;
	while ((REG_GPIO_PXPIN(2) & 0x40000000) && timeout--);
	while (!(REG_GPIO_PXPIN(2) & 0x40000000));
}

#define __nand_cmd(n)		(REG8(NAND_COMMPORT) = (n))
#define __nand_addr(n)		(REG8(NAND_ADDRPORT) = (n))
#define __nand_data8()		REG8(NAND_DATAPORT)
#define __nand_data16()		REG16(NAND_DATAPORT)

/*
 * NAND flash parameters
 */
static int bus_width = 8;
static int page_size = 2048;
static int oob_size = 64;
static int ecc_count = 4;
static int row_cycle = 3;
static int page_per_block = 64;
static int bad_block_pos = 0;
static int block_size = 131072;

static unsigned char oob_buf[128] = {0};

/*
 * External routines
 */
extern void flush_cache_all(void);
extern int serial_init(void);
extern void serial_puts(const char *s);
extern void sdram_init(void);
extern void pll_init(void);

/*
 * NAND flash routines
 */

static inline void nand_read_buf16(void *buf, int count)
{
	int i;
	u16 *p = (u16 *)buf;

	for (i = 0; i < count; i += 2)
		*p++ = __nand_data16();
}

static inline void nand_read_buf8(void *buf, int count)
{
	int i;
	u8 *p = (u8 *)buf;

	for (i = 0; i < count; i++)
		*p++ = __nand_data8();
}

static inline void nand_read_buf(void *buf, int count, int bw)
{
	if (bw == 8)
		nand_read_buf8(buf, count);
	else
		nand_read_buf16(buf, count);
}

#ifndef CONFIG_ECC_HM
/* Correct 1~9-bit errors in 512-bytes data */
static void rs_correct(unsigned char *dat, int idx, int mask)
{
	int i;

	idx--;

	i = idx + (idx >> 3);
	if (i >= 512)
		return;

	mask <<= (idx & 0x7);

	dat[i] ^= mask & 0xff;
	if (i < 511)
		dat[i+1] ^= (mask >> 8) & 0xff;
}
#else
static int calculate_hm_ecc(unsigned char* ecc_code)
{
	unsigned int calc_ecc;
	unsigned char *tmp;

	__nand_ecc_disable();

	calc_ecc = ~(__nand_read_hm_ecc()) | 0x00030000;

	tmp = (unsigned char *)&calc_ecc;
	//adjust eccbytes order for compatible with software ecc
	ecc_code[0] = tmp[1];
	ecc_code[1] = tmp[0];
	ecc_code[2] = tmp[2];

	return 0;
}

static int hm_correct_data(unsigned char *dat, unsigned char *read_ecc, unsigned char *calc_ecc)
{
	unsigned char a, b, c, d1, d2, d3, add, bit, i;

	/* Do error detection */
	d1 = calc_ecc[0] ^ read_ecc[0];
	d2 = calc_ecc[1] ^ read_ecc[1];
	d3 = calc_ecc[2] ^ read_ecc[2];

	if ((d1 | d2 | d3) == 0) {
		/* No errors */
		return 0;
	}
	else {
		a = (d1 ^ (d1 >> 1)) & 0x55;
		b = (d2 ^ (d2 >> 1)) & 0x55;
		c = (d3 ^ (d3 >> 1)) & 0x54;

		/* Found and will correct single bit error in the data */
		if ((a == 0x55) && (b == 0x55) && (c == 0x54)) {
			c = 0x80;
			add = 0;
			a = 0x80;
			for (i=0; i<4; i++) {
				if (d1 & c)
					add |= a;
				c >>= 2;
				a >>= 1;
			}
			c = 0x80;
			for (i=0; i<4; i++) {
				if (d2 & c)
					add |= a;
				c >>= 2;
				a >>= 1;
			}
			bit = 0;
			b = 0x04;
			c = 0x80;
			for (i=0; i<3; i++) {
				if (d3 & c)
					bit |= b;
				c >>= 2;
				b >>= 1;
			}
			b = 0x01;
			a = dat[add];
			a ^= (b << bit);
			dat[add] = a;
			return 1;
		}
		else {
			i = 0;
			while (d1) {
				if (d1 & 0x01)
					++i;
				d1 >>= 1;
			}
			while (d2) {
				if (d2 & 0x01)
					++i;
				d2 >>= 1;
			}
			while (d3) {
				if (d3 & 0x01)
					++i;
				d3 >>= 1;
			}
			if (i == 1) {
				/* ECC Code Error Correction */
				read_ecc[0] = calc_ecc[0];
				read_ecc[1] = calc_ecc[1];
				read_ecc[2] = calc_ecc[2];
				return 1;
			}
			else {
				/* Uncorrectable Error */
				return -1;
			}
		}
	}

	/* Should never happen */
	return -1;
}
#endif

static int nand_read_oob(int page_addr, uchar *buf, int size)
{
	int col_addr;

	if (page_size == 2048)
		col_addr = 2048;
	else
		col_addr = 0;

	if (page_size == 2048)
		/* Send READ0 command */
		__nand_cmd(NAND_CMD_READ0);
	else
		/* Send READOOB command */
		__nand_cmd(NAND_CMD_READOOB);

	/* Send column address */
	__nand_addr(col_addr & 0xff);
	if (page_size == 2048)
		__nand_addr((col_addr >> 8) & 0xff);

	/* Send page address */
	__nand_addr(page_addr & 0xff);
	__nand_addr((page_addr >> 8) & 0xff);
	if (row_cycle == 3)
		__nand_addr((page_addr >> 16) & 0xff);

	/* Send READSTART command for 2048 ps NAND */
	if (page_size == 2048)
		__nand_cmd(NAND_CMD_READSTART);

	/* Wait for device ready */
	__nand_dev_ready();

	/* Read oob data */
	nand_read_buf(buf, size, bus_width);

	return 0;
}

static int nand_read_page(int block, int page, uchar *dst, uchar *oobbuf)
{
	int page_addr = page + block * page_per_block;
	uchar *databuf = dst, *tmpbuf;
	int i, j;

	/*
	 * Read oob data
	 */
	nand_read_oob(page_addr, oobbuf, oob_size);

	/*
	 * Read page data
	 */

	/* Send READ0 command */
	__nand_cmd(NAND_CMD_READ0);

	/* Send column address */
	__nand_addr(0);
	if (page_size == 2048)
		__nand_addr(0);

	/* Send page address */
	__nand_addr(page_addr & 0xff);
	__nand_addr((page_addr >> 8) & 0xff);
	if (row_cycle == 3)
		__nand_addr((page_addr >> 16) & 0xff);

	/* Send READSTART command for 2048 ps NAND */
	if (page_size == 2048)
		__nand_cmd(NAND_CMD_READSTART);

	/* Wait for device ready */
	__nand_dev_ready();

	/* Read page data */
	tmpbuf = databuf;

	for (i = 0; i < ecc_count; i++) {
		volatile unsigned char *paraddr = (volatile unsigned char *)EMC_NFPAR0;
		unsigned int stat;
		unsigned char calc_ecc[3];

#ifdef CONFIG_ECC_HM
		__nand_ecc_enable();
		__nand_select_hm_ecc();

		nand_read_buf((void *)tmpbuf, ECC_BLOCK, bus_width);

		calculate_hm_ecc(calc_ecc);

		stat = hm_correct_data(tmpbuf, &oob_buf[ECC_POS + i * PAR_SIZE], calc_ecc);

		if (stat < 0)
			serial_puts("Uncorrectable ECC error on read\n");
		else if (stat == 1)
			serial_puts("One error corrected by ECC on read\n");
#else
		/* Enable RS decoding */
		REG_EMC_NFINTS = 0x0;
		__nand_ecc_rs_decoding();

		/* Read data */
		nand_read_buf((void *)tmpbuf, ECC_BLOCK, bus_width);

		/* Set PAR values */
		for (j = 0; j < PAR_SIZE; j++) {
			*paraddr++ = oobbuf[ECC_POS + i*PAR_SIZE + j];
		}

		/* Set PRDY */
		REG_EMC_NFECR |= EMC_NFECR_PRDY;

		/* Wait for completion */
		__nand_ecc_decode_sync();

		/* Disable decoding */
		__nand_ecc_disable();

		/* Check result of decoding */
		stat = REG_EMC_NFINTS;
		if (stat & EMC_NFINTS_ERR) {
			/* Error occurred */
			if (stat & EMC_NFINTS_UNCOR) {
				/* Uncorrectable error occurred */
			}
			else {
				unsigned int errcnt, index, mask;

				errcnt = (stat & EMC_NFINTS_ERRCNT_MASK) >> EMC_NFINTS_ERRCNT_BIT;
				switch (errcnt) {
				case 4:
					index = (REG_EMC_NFERR3 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT;
					mask = (REG_EMC_NFERR3 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT;
					rs_correct(tmpbuf, index, mask);
					/* FALL-THROUGH */
				case 3:
					index = (REG_EMC_NFERR2 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT;
					mask = (REG_EMC_NFERR2 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT;
					rs_correct(tmpbuf, index, mask);
					/* FALL-THROUGH */
				case 2:
					index = (REG_EMC_NFERR1 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT;
					mask = (REG_EMC_NFERR1 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT;
					rs_correct(tmpbuf, index, mask);
					/* FALL-THROUGH */
				case 1:
					index = (REG_EMC_NFERR0 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT;
					mask = (REG_EMC_NFERR0 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT;
					rs_correct(tmpbuf, index, mask);
					break;
				default:
					break;
				}
			}
		}
#endif
		tmpbuf += ECC_BLOCK;
	}

	return 0;
}

#ifndef CONFIG_SYS_NAND_BADBLOCK_PAGE
#define CONFIG_SYS_NAND_BADBLOCK_PAGE 0 /* NAND bad block was marked at this page in a block, starting from 0 */
#endif

static void nand_load(int offs, int uboot_size, uchar *dst)
{
	int block;
	int blockcopy_count;
	int page;

	__nand_enable();

	/*
	 * offs has to be aligned to a block address!
	 */
	block = offs / block_size;
	blockcopy_count = 0;

	while (blockcopy_count < (uboot_size / block_size)) {

		/* New block is required to check the bad block flag */
		nand_read_oob(block * page_per_block + CONFIG_SYS_NAND_BADBLOCK_PAGE, oob_buf, oob_size);

		if (oob_buf[bad_block_pos] != 0xff) {
			block++;
			
			/* Skip bad block */
			continue;
		}

		for (page = 0; page < page_per_block; page++) {

			/* Load this page to dst, do the ECC */
			nand_read_page(block, page, dst, oob_buf);

			dst += page_size;
		}

		block++;
		blockcopy_count++;
	}

	__nand_disable();
}

static void gpio_init(void)
{
	/*
	 * Initialize SDRAM pins
	 */
#if defined(CONFIG_JZ4720)
	__gpio_as_sdram_16bit_4720();
#elif defined(CONFIG_JZ4725)
	__gpio_as_sdram_16bit_4725();
#else
	__gpio_as_sdram_32bit();
#endif

	/*
	 * Initialize UART0 pins
	 */
	__gpio_as_uart0();
}

void nand_boot(void)
{
	int boot_sel;
	void __attribute__((noreturn)) (*uboot)(void);

	/*
	 * Init hardware
	 */
	gpio_init();
	serial_init();

	serial_puts("\n\nNAND Secondary Program Loader\n\n");

	pll_init();
	sdram_init();

	/*
	 * Decode boot select
	 */

	boot_sel = REG_EMC_BCR >> 30;

	if (boot_sel == 0x2)
		page_size = 512;
	else
		page_size = 2048;

#if (JZ4740_NANDBOOT_CFG == JZ4740_NANDBOOT_B8R3)
	bus_width = 8;
	row_cycle = 3;
#endif

#if (JZ4740_NANDBOOT_CFG == JZ4740_NANDBOOT_B8R2)
	bus_width = 8;
	row_cycle = 2;
#endif

#if (JZ4740_NANDBOOT_CFG == JZ4740_NANDBOOT_B16R3)
	bus_width = 16;
	row_cycle = 3;
#endif

#if (JZ4740_NANDBOOT_CFG == JZ4740_NANDBOOT_B16R2)
	bus_width = 16;
	row_cycle = 2;
#endif

	page_per_block = (page_size == 2048) ? CONFIG_SYS_NAND_BLOCK_SIZE / page_size : 32;
	bad_block_pos = (page_size == 2048) ? 0 : 5;
	oob_size = page_size / 32;
	block_size = page_size * page_per_block;
	ecc_count = page_size / ECC_BLOCK;

	/*
	 * Load U-Boot image from NAND into RAM
	 */
	nand_load(CONFIG_SYS_NAND_U_BOOT_OFFS, CONFIG_SYS_NAND_U_BOOT_SIZE,
		  (uchar *)CONFIG_SYS_NAND_U_BOOT_DST);

	uboot = (void (*)(void))CONFIG_SYS_NAND_U_BOOT_START;

	serial_puts("Starting U-Boot ...\n");

	/*
	 * Flush caches
	 */
	flush_cache_all();

	/*
	 * Jump to U-Boot image
	 */
	(*uboot)();
}
