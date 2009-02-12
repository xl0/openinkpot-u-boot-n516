/*
 * (C) Copyright 2005
 * Ingenic Semiconductor, <jlwei@ingenic.cn>
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

/*
 * This file contains the configuration parameters for the uranus board.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define CONFIG_MIPS32		1  /* MIPS32 CPU core */
#define CONFIG_JzRISC		1  /* JzRISC core */
#define CONFIG_JZSOC		1  /* Jz SoC */
#define CONFIG_JZ4730		1  /* Jz4730 SoC */
#define CONFIG_URANUS		1  /* uranus validation board */
#define CONFIG_FPGA		1  /* uranus is a FPGA board */

#define CONFIG_SYS_EXTAL		8000000		/* EXTAL freq: 8 MHz */
#define CONFIG_SYS_CPU_SPEED		CFG_EXTAL	/* CPU speed */
#define	CFG_HZ			(CFG_CPU_SPEED/(3*256)) /* incrementer freq */

#define CONFIG_SYS_UART_BASE  		UART0_BASE	/* Base of the UART channel */
#define CONFIG_BAUDRATE		9600
#define CONFIG_SYS_BAUDRATE_TABLE	{ 9600, 19200, 38400, 57600, 115200 }

/* allow to overwrite serial and ethaddr */
#define CONFIG_ENV_OVERWRITE

#define CONFIG_COMMANDS		((CONFIG_CMD_DFL | CONFIG_SYS_CMD_DHCP) & ~(CFG_CMD_ENV))
#define CONFIG_BOOTP_MASK	( CONFIG_BOOTP_DEFAUL )

/* this must be included AFTER the definition of CONFIG_COMMANDS (if any) */
#include <cmd_confdefs.h>

#define CONFIG_BOOTDELAY	3
#define CONFIG_BOOTFILE	        "uImage"	/* file to load */
#define CONFIG_BOOTARGS		"mem=64M console=ttyS0,9600n8 ip=bootp nfsroot=192.168.1.20:/nfsroot/miniroot rw"
#define CONFIG_BOOTCOMMAND	"bootp;setenv serverip 192.168.1.20;tftp;bootm"
#define CONFIG_SYS_AUTOLOAD		"n"		/* No autoload */

#define CONFIG_NET_MULTI
#define CONFIG_ETHADDR		00:50:c2:1e:af:fe    /* Ethernet address */

/*
 * Serial download configuration
 *
 */
#define CONFIG_LOADS_ECHO	1	/* echo on for serial download	*/
#define CONFIG_SYS_LOADS_BAUD_CHANGE	1	/* allow baudrate change	*/

/*
 * Miscellaneous configurable options
 */
#define	CFG_LONGHELP				/* undef to save memory      */
#define	CFG_PROMPT		"URANUS # "	/* Monitor Command Prompt    */
#define	CFG_CBSIZE		256		/* Console I/O Buffer Size   */
#define	CFG_PBSIZE (CFG_CBSIZE+sizeof(CFG_PROMPT)+16)  /* Print Buffer Size */
#define	CFG_MAXARGS		16		/* max number of command args*/

#define CONFIG_SYS_MALLOC_LEN		128*1024
#define CONFIG_SYS_BOOTPARAMS_LEN	128*1024

#define CONFIG_SYS_SDRAM_BASE		0x80000000     /* Cached addr */

#define CONFIG_SYS_INIT_SP_OFFSET	0x400000

#define	CFG_LOAD_ADDR		0x80600000     /* default load address	*/

#define CONFIG_SYS_MEMTEST_START	0x80100000
#define CONFIG_SYS_MEMTEST_END		0x80800000

#define CONFIG_SYS_RX_ETH_BUFFER	16	/* use 16 rx buffers on jz47xx eth */

/*-----------------------------------------------------------------------
 * FLASH and environment organization
 */
#define CONFIG_SYS_MAX_FLASH_BANKS	1	/* max number of memory banks */
#define CONFIG_SYS_MAX_FLASH_SECT	(128)	/* max number of sectors on one chip */

#define PHYS_FLASH_1		0xbfc00000 /* Flash Bank #1 */

/* The following #defines are needed to get flash environment right */
#define	CFG_MONITOR_BASE	TEXT_BASE
#define	CFG_MONITOR_LEN		(192 << 10)

#define CONFIG_SYS_FLASH_BASE		PHYS_FLASH_1

/* timeout values are in ticks */
#define CONFIG_SYS_FLASH_ERASE_TOUT	(2 * CONFIG_SYS_HZ) /* Timeout for Flash Erase */
#define CONFIG_SYS_FLASH_WRITE_TOUT	(2 * CONFIG_SYS_HZ) /* Timeout for Flash Write */

#define	CFG_ENV_IS_NOWHERE	1

/* Address and size of Primary Environment Sector	*/
#define CONFIG_SYS_ENV_ADDR		0xBFC40000
#define CONFIG_SYS_ENV_SIZE		0x20000

/*
 * SDRAM Info.
 */
#define CONFIG_NR_DRAM_BANKS	1

// SDRAM paramters
#define SDRAM_BW16		0	/* Data bus width: 0-32bit, 1-16bit */
#define SDRAM_BANK4		1	/* Banks each chip: 0-2bank, 1-4bank */
#define SDRAM_ROW		13	/* Row address: 11 to 13 */
#define SDRAM_COL		9	/* Column address: 8 to 12 */
#define SDRAM_CASL		2	/* CAS latency: 2 or 3 */

// SDRAM Timings, unit: ns
#define SDRAM_TRAS		45	/* RAS# Active Time */
#define SDRAM_RCD		20	/* RAS# to CAS# Delay */
#define SDRAM_TPC		20	/* RAS# Precharge Time */
#define SDRAM_TRWL		7	/* Write Latency Time */
#define SDRAM_TREF	        15625	/* Refresh period: 4096 refresh cycles/64ms */

/*-----------------------------------------------------------------------
 * Cache Configuration
 */
#define CONFIG_SYS_DCACHE_SIZE		16384
#define CONFIG_SYS_ICACHE_SIZE		16384
#define CONFIG_SYS_CACHELINE_SIZE	32

#endif	/* __CONFIG_H */
