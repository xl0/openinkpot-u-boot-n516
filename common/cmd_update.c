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
#include <mmc.h>
#include <part.h>
#include <fat.h>
#include <malloc.h>
#include <firmware_update.h>
#include <linux/mtd/mtd.h>
#include <nand.h>

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

static int fw_load(char *filename, unsigned char *buf, unsigned long size, unsigned long offset)
{
	block_dev_desc_t *dev_desc;
	int part = 1;
	struct mmc *mmc;

	mmc = find_mmc_device(0);
	if (!mmc)
		return -1;

	mmc_init(mmc);

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

static int check_global_property(char *name, char *val, unsigned long len)
{
	if (!strcmp(name, "device")) {
		if (strcmp(val, CONFIG_BOARD_NAME))
			return -1;
	} else if (!strcmp(name, "hwrev")) {
		if (strcmp(val, CONFIG_BOARD_HWREV))
			return -1;
	} else if (!strcmp(name, "description")) {
		puts(val);
	} else if (!strcmp(name, "date")) {
		printf("Date of firmware: %u\n", __get_unaligned_le32((unsigned char *)val));
	} else if (!strcmp(name, "epoch")) {
		if (strcmp(val, CONFIG_FIRMWARE_EPOCH))
			return -1;
	}

	return 0;
}

static int check_block_property(char *block_name, char *name, char *val, unsigned long len)
{
	return 0;
}

static unsigned long process_all_properties(unsigned char *start, char *block_name,
		int dry_run, int *image_valid)
{
	u32 property_name_len;
	u32 property_val_len;
	char *property_name, *property_val;
	unsigned char *buf = start;
	int res;

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

		if (!block_name)
			res = check_global_property(property_name, property_val,
					property_val_len);
		else
			res = check_block_property(block_name, property_name, property_val,
					property_val_len);

		if (res != 0)
			*image_valid = 0;

		property_name_len = __get_unaligned_le32(buf);
		buf += 4;
		property_val_len = __get_unaligned_le32(buf);
		buf += 4;
	} 

	return buf - start;
}

static struct update_layout_entry *get_block_flash_layout(char *block_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nand_layout); i++) {
		if (!strcmp(nand_layout[i].name, block_name))
			return &nand_layout[i];
	}

	return NULL; 
}

static int flash_chunk(u64 offset, unsigned char *buf, size_t count)
{
	nand_info_t *nand;

#ifdef CONFIG_SYS_NAND_SELECT_DEVICE
	board_nand_select_device(nand_info[0].priv, 0);
#endif

	nand = &nand_info[0];
	count = roundup(count, nand->writesize);

	printf("Flashing chunk to offset 0x%08x, count 0x%x...\n", (u32)offset, count);

	return nand_write_skip_bad(nand, offset, &count, buf);
}

static int erase_flash(u64 offset, u64 size)
{
	nand_erase_options_t opts;
	nand_info_t *nand;

#ifdef CONFIG_SYS_NAND_SELECT_DEVICE
	board_nand_select_device(nand_info[0].priv, 0);
#endif

	nand = &nand_info[0];

	memset(&opts, 0, sizeof(opts));
	opts.offset = offset;
	opts.length = size;
	opts.jffs2 = 0;
	opts.quiet = 0;
	opts.scrub = 0;

	return nand_erase_opts(nand, &opts);
}

static int process_block(char *filename, char *block_name, u64 block_offset, u64 block_size, int dry_run)
{
	unsigned char *buf = (unsigned char *)CONFIG_UPDATE_TMPBUF;
	u64 bytes_read = 0, bytes_remain = block_size;
	struct update_layout_entry *layout;
	u64 flash_address;
	
	layout = get_block_flash_layout(block_name);
	if (!layout) {
		printf("Cannot to find layout for block '%s', skip it\n", block_name);
		return -1;
	}

	if (!dry_run)
		erase_flash(layout->offset, layout->size);
	else
		printf("Not erasing flash (dry run)\n");

	flash_address = layout->offset;

	while (bytes_remain) {
		unsigned long chunksize = min(CONFIG_UPDATE_CHUNKSIZE, bytes_remain);
		unsigned long res;

		if (bytes_remain < CONFIG_UPDATE_CHUNKSIZE)
			memset(buf, 0xff, CONFIG_UPDATE_CHUNKSIZE);

		res = fw_load(filename, buf, chunksize, block_offset + bytes_read);

		bytes_read += res;
		bytes_remain -= res;

		if (!dry_run)
			flash_chunk(flash_address, buf, res);
		else
			printf("Not flashing (dry run) chunk to offset 0x%08x...\n", (u32)flash_address);
		
		flash_address += res;
	}

	return 0;
}

static int process_update(int dry_run)
{
	char *filename;
	struct fw_update_head fw_head;
	unsigned char *header;
	unsigned char *p;
	u32 block_name_len;
	u64 block_offset, block_size;
	char *block_name;
	int image_valid = 1;

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
	p += process_all_properties(p, NULL, dry_run, &image_valid);
	if (!image_valid) {
		printf("Image is not valid for this device\n");
		dry_run = 1;
		image_valid = 1;
	}


	while (p < header + fw_head.header_size) {
		block_name_len = __get_unaligned_le32(p);
		p += 4;
		block_offset = __get_unaligned_le64(p);
		p += 8;
		block_size = __get_unaligned_le64(p);
		p += 8;

		block_name = (char *)p;
		p += block_name_len;

		printf("Block '%s', offset: %lu, size: %lu\n", block_name,
				(unsigned long)block_offset, (unsigned long)block_size);
		printf("Block properties:\n");
		
		p += process_all_properties(p, block_name, dry_run, &image_valid);

		if (image_valid && get_block_flash_layout(block_name)) {
			process_block(filename, block_name, block_offset, block_size, dry_run);
		} else {
			printf("Block '%s' is not valid for this device, skipping it\n", block_name);
			image_valid = 1;
		}
	}

	return 0;
}

int do_updateinfo(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	return process_update(1);
}

U_BOOT_CMD(
	updateinfo, 1, 0, do_updateinfo,
	"Show information about firmware update file",
	" - load firmware update file from SD card and parse it\n"
);

int do_update(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	return process_update(0);
}

U_BOOT_CMD(
	update, 1, 0, do_update,
	"Do firmware update",
	" - load firmware update file from SD card, parse and flash it\n"
);

