/*
 * Copyright (c) 2009, Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
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
#include <command.h>
#include <net.h>
#include <ata.h>
#include <part.h>
#include <fat.h>
#include <malloc.h>

struct fw_update_head {
	char magic[4];
	u32 header_size;
} __attribute__((packed));

static u32 __get_unaligned_le32(unsigned char *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 __get_unaligned_le64(unsigned char *p)
{

	return (u64)p[0] | ((u64)p[1] << 8) | ((u64)p[2] << 16) | ((u64)p[3] << 24) |
		((u64)p[4] << 32) | ((u64)p[5] << 40) | ((u64)p[6] << 48) | ((u64)p[7] << 56);
}

static unsigned long print_all_properties(unsigned char *start_of_block)
{
	u32 property_name_len;
	u32 property_val_len;
	char *property_name, *property_val;
	unsigned char *buf = start_of_block;

	property_name_len = __get_unaligned_le32(buf);
	buf += 4;
	property_val_len = __get_unaligned_le32(buf);
	buf += 4;

	while (property_name_len) {
		property_name = (char *)buf;
		buf += property_name_len;
		property_val = (char *)buf;
		buf += property_val_len;

		if ((property_name[property_name_len - 1] == '\0') &&
				strncmp(property_name, "crc32", 5))
			printf("%s: %s\n", property_name, property_val);
		else {
			int i;

			printf("%s:", property_name);
			for (i = 0; i < property_val_len; i++)
				printf(" %02x", (u8)property_val[i]);
			puts("\n");
		}

		property_name_len = __get_unaligned_le32(buf);
		buf += 4;
		property_val_len = __get_unaligned_le32(buf);
		buf += 4;
	} 

	return buf - start_of_block;
}

static int fw_load(char *filename, unsigned char *buf, unsigned long size, unsigned long offset)
{
	block_dev_desc_t *dev_desc;
	int part = 1;

	dev_desc = get_dev("mmc", 0);
	if (dev_desc==NULL) {
		puts ("\n** Invalid boot device **\n");
		return -1;
	}

	if (fat_register_device(dev_desc, part)!=0) {
		printf ("\n** Unable to use %s %d:%d for update **\n", "mmc", 0, part);
		return -1;
	}

	return file_fat_read(filename, buf, size, offset);
}


int do_updateinfo(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	char *filename;
	struct fw_update_head fw_head;
	unsigned char *header;
	unsigned char *p;
	u32 block_name_len;
	u64 block_offset, block_size;
	char *block_name;

	filename = getenv("update_file");
	if (!filename) {
		puts("Missing update_file environment variable, please set it\n");
		return 1;
	}

	if (fw_load(filename, (unsigned char *)&fw_head, sizeof(fw_head), 0) < 0)
	{
		printf("Failed to load file %s\n", filename);
		return 1;
	}

	fw_head.header_size = __le32_to_cpu(fw_head.header_size);

	header = malloc(fw_head.header_size);
	if (!header) {
		puts("Failed to allocate memory for firmware update header\n");
		return 1;
	}

	if (fw_load(filename, header, fw_head.header_size, 0) < 0)
	{
		printf("Failed to load file %s\n", filename);
		return 1;
	}

	p = header + sizeof(fw_head);

	printf("Global properties:\n");
	p += print_all_properties(p);

	block_name_len = __get_unaligned_le32(p);
	p += 4;
	block_offset = __get_unaligned_le64(p);
	p += 8;
	block_size = __get_unaligned_le64(p);
	p += 8;

	while (block_name_len) {
		block_name = (char *)p;
		p += block_name_len;

		printf("Block '%s', offset: %lu, size: %lu\n", block_name,
				(unsigned long)block_offset, (unsigned long)block_size);
		printf("Block properties:\n");
		
		p += print_all_properties(p);
		
		block_name_len = __get_unaligned_le32(p);
		p += 4;
		block_offset = __get_unaligned_le64(p);
		p += 8;
		block_size = __get_unaligned_le64(p);
		p += 8;
	}

	return 0;
}

U_BOOT_CMD(
	updateinfo, 1, 0, do_updateinfo,
	"Show information about firmware update file",
	" - load firmware update file from SD card and parse it\n"
);
