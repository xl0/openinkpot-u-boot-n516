/*
 * Platform independend driver for JZ4730.
 *
 * Copyright (c) 2007 Ingenic Semiconductor Inc.
 * Author: <jlwei@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */
#include <common.h>

#if defined(CONFIG_CMD_NAND) && defined(CONFIG_JZ4740)

#include <nand.h>
#include <asm/jz4740.h>
#include <asm/io.h>


static struct nand_ecclayout nand_oob_rs = {
	.eccbytes = 36,
	.eccpos = {
		6,  7,  8,  9,  10, 11, 12, 13,
		14, 15, 16, 17, 18, 19, 20, 21,
		22, 23, 24, 25, 26, 27, 28, 29,
		30, 31, 32, 33, 34, 35, 36, 37,
		38, 39, 40, 41},
	.oobfree = { {42, 22} }
};



#define PAR_SIZE 9
#define __nand_ecc_enable()    (REG_EMC_NFECR = EMC_NFECR_ECCE | EMC_NFECR_ERST )
#define __nand_ecc_disable()   (REG_EMC_NFECR &= ~EMC_NFECR_ECCE)

#define __nand_select_rs_ecc() (REG_EMC_NFECR |= EMC_NFECR_RS)

#define __nand_rs_ecc_encoding()	(REG_EMC_NFECR |= EMC_NFECR_RS_ENCODING)
#define __nand_rs_ecc_decoding()	(REG_EMC_NFECR |= EMC_NFECR_RS_DECODING)
#define __nand_ecc_encode_sync() while (!(REG_EMC_NFINTS & EMC_NFINTS_ENCF))
#define __nand_ecc_decode_sync() while (!(REG_EMC_NFINTS & EMC_NFINTS_DECF))

static void jz_hwcontrol(struct mtd_info *mtd, int dat, 
			 unsigned int ctrl)
{
	struct nand_chip *this = (struct nand_chip *)(mtd->priv);
	unsigned int nandaddr = (unsigned int)this->IO_ADDR_W;

	if (ctrl & NAND_CTRL_CHANGE) {
		if ( ctrl & NAND_ALE )
			nandaddr = (unsigned int)((unsigned long)(this->IO_ADDR_W) | 0x00010000);
		else
			nandaddr = (unsigned int)((unsigned long)(this->IO_ADDR_W) & ~0x00010000);

		if ( ctrl & NAND_CLE )
			nandaddr = nandaddr | 0x00008000;
		else
			nandaddr = nandaddr & ~0x00008000;
		if ( ctrl & NAND_NCE )
			REG_EMC_NFCSR |= EMC_NFCSR_NFCE1;
		else
			REG_EMC_NFCSR &= ~EMC_NFCSR_NFCE1;
	}

	this->IO_ADDR_W = (void __iomem *)nandaddr;
	if (dat != NAND_CMD_NONE)
		writeb(dat, this->IO_ADDR_W);
}

static int jz_device_ready(struct mtd_info *mtd)
{
	int ready;
	udelay(20);	/* FIXME: add 20us delay */
	ready = (REG_GPIO_PXPIN(2) & 0x40000000) ? 1 : 0;
	return ready;
}

/*
 * EMC setup
 */
static void jz_device_setup(void)
{
	/* Set NFE bit */
	REG_EMC_NFCSR |= EMC_NFCSR_NFE1;
	REG_EMC_SMCR3 = 0x04444400;
}

void board_nand_select_device(struct nand_chip *nand, int chip)
{
	/*
	 * Don't use "chip" to address the NAND device,
	 * generate the cs from the address where it is encoded.
	 */
}

static int jzsoc_nand_calculate_rs_ecc(struct mtd_info* mtd, const u_char* dat,
				u_char* ecc_code)
{
	volatile u8 *paraddr = (volatile u8 *)EMC_NFPAR0;
	short i;

	__nand_ecc_encode_sync();
	__nand_ecc_disable();

	for(i = 0; i < PAR_SIZE; i++)
		ecc_code[i] = *paraddr++;

	return 0;
}

static void jzsoc_nand_enable_rs_hwecc(struct mtd_info* mtd, int mode)
{
	REG_EMC_NFINTS = 0x0;

	__nand_ecc_enable();
	__nand_select_rs_ecc();

	if (NAND_ECC_READ == mode){
		__nand_rs_ecc_decoding();
	}
	if (NAND_ECC_WRITE == mode){
		__nand_rs_ecc_encoding();
	}
}

/* Correct 1~9-bit errors in 512-bytes data */
static void jzsoc_rs_correct(unsigned char *dat, int idx, int mask)
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

static int jzsoc_nand_rs_correct_data(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	volatile u8 *paraddr = (volatile u8 *)EMC_NFPAR0;
	short k;
	u32 stat;

	/* Set PAR values */
	for (k = 0; k < PAR_SIZE; k++) {
		*paraddr++ = read_ecc[k];
	}

	/* Set PRDY */
	REG_EMC_NFECR |= EMC_NFECR_PRDY;

	/* Wait for completion */
	__nand_ecc_decode_sync();
	__nand_ecc_disable();

	/* Check decoding */
	stat = REG_EMC_NFINTS;
	if (stat & EMC_NFINTS_ERR) {
		if (stat & EMC_NFINTS_UNCOR) {
			printk("Uncorrectable error occurred\n");
			return -1;
		}
		else {
			u32 errcnt = (stat & EMC_NFINTS_ERRCNT_MASK) >> EMC_NFINTS_ERRCNT_BIT;
			switch (errcnt) {
			case 4:
				jzsoc_rs_correct(dat, (REG_EMC_NFERR3 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT, (REG_EMC_NFERR3 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT);
			case 3:
				jzsoc_rs_correct(dat, (REG_EMC_NFERR2 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT, (REG_EMC_NFERR2 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT);
			case 2:
				jzsoc_rs_correct(dat, (REG_EMC_NFERR1 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT, (REG_EMC_NFERR1 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT);
			case 1:
				jzsoc_rs_correct(dat, (REG_EMC_NFERR0 & EMC_NFERR_INDEX_MASK) >> EMC_NFERR_INDEX_BIT, (REG_EMC_NFERR0 & EMC_NFERR_MASK_MASK) >> EMC_NFERR_MASK_BIT);
				return 0;
			default:
				break;
			}
		}
	}
	//no error need to be correct 
	return 0;
}

static int nand_read_page_hwecc_rs(struct mtd_info *mtd, struct nand_chip *chip,
				   uint8_t *buf)
{
	int i, j;
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *p = buf;
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	uint8_t *ecc_code = chip->buffers->ecccode;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	uint32_t page;
	uint8_t flag = 0;

	page = (buf[3]<<24) + (buf[2]<<16) + (buf[1]<<8) + buf[0];

	chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
	for (i = 0; i < chip->ecc.total; i++) {
		ecc_code[i] = chip->oob_poi[eccpos[i]];
//		if (ecc_code[i] != 0xff) flag = 1;
	}

	eccsteps = chip->ecc.steps;
	p = buf;

	chip->cmdfunc(mtd, NAND_CMD_RNDOUT, 0x00, -1);
	for (i = 0 ; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		int stat;

		flag = 0;
		for (j = 0; j < eccbytes; j++)
			if (ecc_code[i + j] != 0xff) {
				flag = 1;
				break;
			}

		if (flag) {
			chip->ecc.hwctl(mtd, NAND_ECC_READ);
			chip->read_buf(mtd, p, eccsize);
			stat = chip->ecc.correct(mtd, p, &ecc_code[i], &ecc_calc[i]);
			if (stat < 0)
				mtd->ecc_stats.failed++;
			else
				mtd->ecc_stats.corrected += stat;
		}
		else {
			chip->ecc.hwctl(mtd, NAND_ECC_READ);
			chip->read_buf(mtd, p, eccsize);
		}
	}
	return 0;
}

const uint8_t empty_ecc[] = {0xcd, 0x9d, 0x90, 0x58, 0xf4, 0x8b, 0xff, 0xb7, 0x6f};
static void nand_write_page_hwecc_rs(struct mtd_info *mtd, struct nand_chip *chip,
				  const uint8_t *buf)
{
	int i, j, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	const uint8_t *p = buf;
	uint32_t *eccpos = chip->ecc.layout->eccpos;
	int page_maybe_empty;

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		chip->ecc.hwctl(mtd, NAND_ECC_WRITE);
		chip->write_buf(mtd, p, eccsize);
		chip->ecc.calculate(mtd, p, &ecc_calc[i]);

		page_maybe_empty = 1;
		for (j = 0; j < eccbytes; j++)
			if (ecc_calc[i + j] != empty_ecc[j]) {
				page_maybe_empty = 0;
				break;
			}

		if (page_maybe_empty)
			for (j = 0; j < eccsize; j++)
				if (p[j] != 0xff) {
					page_maybe_empty = 0;
					break;
				}
		if (page_maybe_empty)
			memset(&ecc_calc[i], 0xff, eccbytes);
	}

	for (i = 0; i < chip->ecc.total; i++)
		chip->oob_poi[eccpos[i]] = ecc_calc[i];

	/* BootROM loader requires this */
	chip->oob_poi[2] = 0;
	chip->oob_poi[3] = 0;
	chip->oob_poi[3] = 0;

	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
}

/*
 * Main initialization routine
 */
int board_nand_init(struct nand_chip *nand)
{
	jz_device_setup();

        nand->cmd_ctrl = jz_hwcontrol;
        nand->dev_ready = jz_device_ready;

	nand->ecc.mode		= NAND_ECC_HW;
	nand->ecc.size		= 512;
	nand->ecc.bytes		= 9;
	nand->ecc.layout	= &nand_oob_rs;
	nand->ecc.correct	= jzsoc_nand_rs_correct_data;
	nand->ecc.hwctl		= jzsoc_nand_enable_rs_hwecc;
	nand->ecc.calculate	= jzsoc_nand_calculate_rs_ecc;
	nand->ecc.read_page	= nand_read_page_hwecc_rs;
	nand->ecc.write_page	= nand_write_page_hwecc_rs;

        /* Set address of NAND IO lines */
        nand->IO_ADDR_R = (void __iomem *) CONFIG_SYS_NAND_BASE;
        nand->IO_ADDR_W = (void __iomem *) CONFIG_SYS_NAND_BASE;

        /* 20 us command delay time */
        nand->chip_delay = 20;

	return 0;
}
#endif /* defined(CONFIG_CMD_NAND) */
