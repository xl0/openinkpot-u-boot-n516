#ifndef __FIRMWARE_UPDATE_H
#define __FIRMWARE_UPDATE_H

#include <asm/types.h>

struct update_layout_entry {
	char *name;
	u64 offset;
	u64 size;
};

static struct update_layout_entry nand_layout[] = {
	{
		.name		= "uImage",
		.offset		= 0x00100000,
		.size		= 0x300000,
	},
	{
		.name		= "uboot",
		.offset		= 0x00000000,
		.size		= 0x100000,
	},
	{
		.name		= "UBI0",
		.offset		= 0x00500000,
		.size		= 0x1fb00000, 
	},
};

#define INVALID_FLASH_ADDRESS (0xffffffffffffffffULL)

#endif /* __FIRMWARE_UPDATE_H */
