#ifndef __FIRMWARE_UPDATE_H
#define __FIRMWARE_UPDATE_H

#include <asm/types.h>

#define SZ_512K				0x00080000

#define SZ_1M				0x00100000
#define SZ_2M				0x00200000
#define SZ_4M				0x00400000

struct update_layout_entry {
	char *name;
	u64 offset;
	u64 size;
};

static struct update_layout_entry nand_layout[] = {
	{
		.name		= "uboot",
		.offset		= 0x00000000,
		.size		= SZ_1M,
	},
	{
		.name		= "UBI",
		.offset		= SZ_1M,
		.size		= SZ_1M * 511, 
	},
};

#endif /* __FIRMWARE_UPDATE_H */
