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
#include <firmware_update.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <linux/mtd/partitions.h>
#include <linux/list.h>
#include <ubi_uboot.h>
#include <jffs2/load_kernel.h>
#include <i2c.h>
#include <lcd.h>
#include <linux/time.h>

#ifdef crc32
#undef crc32
#endif

#define N516_KEY_C	0x1d
#define N516_KEY_MENU	0x0e
#define N516_KEY_POWER	0x1c
#define N516_KEY_1	0x04

#define KEY_RESERVED            0
#define KEY_ESC                 1
#define KEY_1                   2
#define KEY_2                   3
#define KEY_3                   4
#define KEY_4                   5
#define KEY_5                   6
#define KEY_6                   7
#define KEY_7                   8
#define KEY_8                   9
#define KEY_9                   10
#define KEY_0                   11
#define KEY_ENTER               28
#define KEY_SPACE               57
#define KEY_UP                  103
#define KEY_PAGEUP              104
#define KEY_LEFT                105
#define KEY_RIGHT               106
#define KEY_DOWN                108
#define KEY_PAGEDOWN            109
#define KEY_POWER               116
#define KEY_MENU                139
#define KEY_SLEEP               142
#define KEY_WAKEUP              143
#define KEY_DIRECTION           153
#define KEY_PLAYPAUSE           164
#define KEY_SEARCH              217


struct fw_update_head {
	char magic[4];
	u32 header_size;
} __attribute__((packed));

struct block_properties {
	unsigned int	raw:1;
	u32		crc32;
	char		*name;
	u64		offset;
	u64		size;
};

static struct ubi_device *ubi;

static u32 __get_unaligned_le32(unsigned char *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 __get_unaligned_le64(unsigned char *p)
{

	return (u64)p[0] | ((u64)p[1] << 8) | ((u64)p[2] << 16) | ((u64)p[3] << 24) |
		((u64)p[4] << 32) | ((u64)p[5] << 40) | ((u64)p[6] << 48) | ((u64)p[7] << 56);
}

#define log(msg, args...)	\
{				\
	eprintf(msg, ##args);	\
	printf(msg, ##args);	\
} while (0)

#define show_progress(msg, args...)	\
{					\
	printf("\r" msg "\n", ##args);	\
} while(0)

static int ubi_initialize(void)
{
	struct mtd_info *master;
	struct mtd_device *dev;
	struct mtd_partition mtd_part;
	struct part_info *part;
	char buffer[20];
	u8 pnum;
	int err;

	if (ubi_devices[0]) {
		ubi = ubi_devices[0];
		return 0;
//		ubi_exit();
//		del_mtd_partitions(&nand_info[0]);
	}

	if (mtdparts_init() != 0) {
		printf("Error initializing mtdparts!\n");
		return 1;
	}

	master = &nand_info[0];

	if (find_dev_and_part(CONFIG_UBI_PARTITION, &dev, &pnum, &part) != 0)
		return 1;

	sprintf(buffer, "mtd=%d", pnum);
	memset(&mtd_part, 0, sizeof(mtd_part));
	mtd_part.name = buffer;
	mtd_part.size = part->size;
	mtd_part.offset = part->offset;
	add_mtd_partitions(master, &mtd_part, 1);

	err = ubi_mtd_param_parse(buffer, NULL);
	if (err) {
		del_mtd_partitions(master);
		return err;
	}

	err = ubi_init();
	if (err) {
		del_mtd_partitions(master);
		return err;
	}

	ubi = ubi_devices[0];

	return 0;
}

static int init_fat(void)
{
	block_dev_desc_t *dev_desc;
	int part = 1;
	struct mmc *mmc;

	mmc = find_mmc_device(0);
	if (!mmc)
		return -1;

	if (mmc_init(mmc))
		return -1;


	dev_desc = get_dev("mmc", 0);
	if (dev_desc==NULL) {
		printf("\nERROR: Invalid mmc device. Please check your SD/MMC card.\n");
		return -1;
	}

	if (fat_register_device(dev_desc, part)!=0) {
		printf("\nERROR: Unable to use %s %d:%d for update. Please check or replace your card.\n", "mmc", 0, part);
		return -1;
	}

	return 0;
}

static int fw_load(char *filename, unsigned char *buf, unsigned long size, unsigned long offset)
{
	if (init_fat() < 0)
		return -1;

	printf("Reading file %s (0x%lx bytes from 0x%lx) offset\n", filename, size, offset);
	return file_fat_read(filename, buf, size, offset);
}

static int check_global_property(char *name, char *val, unsigned long len)
{
	long t;
	char date[32];

	if (!strcmp(name, "device")) {
		if (strcmp(val, CONFIG_BOARD_NAME))
			return -1;
	} else if (!strcmp(name, "hwrev")) {
		if (strcmp(val, CONFIG_BOARD_HWREV))
			return -1;
	} else if (!strcmp(name, "description")) {
		log("Description:\n %s\n", val);
	} else if (!strcmp(name, "date")) {
		t = simple_strtoul(val, NULL, 10);
		ctime_r(&t, date);
		log("Firmware date:\n %s\n", date);
	} else if (!strcmp(name, "epoch")) {
		if (strcmp(val, CONFIG_FIRMWARE_EPOCH))
			return -1;
	}

	return 0;
}

static int check_block_property(char *block_name, char *name, char *val,
		unsigned long len, struct block_properties *block_prop)
{
	if (!strcmp(name, "raw") && !strcmp(val, "yes"))
		block_prop->raw = 1;
	if (!strcmp(name, "crc32"))
		block_prop->crc32 = __get_unaligned_le32((unsigned char *)val);

	return 0;
}

static unsigned long process_all_properties(unsigned char *start, char *block_name,
		int dry_run, int *image_valid, struct block_properties *block_prop)
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
					property_val_len, block_prop);

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

static int process_block_raw(char *filename, struct block_properties *block_prop, int dry_run)
{
	unsigned char *buf = (unsigned char *)CONFIG_UPDATE_TMPBUF;
	u64 bytes_read = 0, bytes_remain = block_prop->size;
	struct update_layout_entry *layout;
	u64 flash_address;
	u32 block_crc32;

	layout = get_block_flash_layout(block_prop->name);
	if (!layout) {
		log("Cannot find layout for block '%s', skipping it\n", block_prop->name);
		return 1;
	}

	log("Flashing `%s'", block_prop->name);

	show_progress("Erasing flash...");

	if (!dry_run)
		erase_flash(layout->offset, layout->size);
	else
		printf("Not erasing flash (dry run)\n");

	flash_address = layout->offset;

	block_crc32 = 0;

	while (bytes_remain) {
		unsigned long chunksize = min(CONFIG_UPDATE_CHUNKSIZE, bytes_remain);
		long res;

		if (bytes_remain < CONFIG_UPDATE_CHUNKSIZE)
			memset(buf, 0xff, CONFIG_UPDATE_CHUNKSIZE);

		show_progress("Reading...\t(%u%%)", (unsigned int)(bytes_read * 100 / block_prop->size));
		res = fw_load(filename, buf, chunksize, block_prop->offset + bytes_read);
		if (res < 0)
		{
			log("\nFailed to read file %s\n", filename);
			return -1;
		}

		block_crc32 = crc32(block_crc32, buf, chunksize);
		bytes_read += res;
		bytes_remain -= res;

		show_progress("Flashing...\t(%u%%)", (unsigned int)(bytes_read * 100 / block_prop->size));
		if (!dry_run)
			flash_chunk(flash_address, buf, res);
		else
			printf("Not flashing (dry run) chunk to offset 0x%08x...\n", (u32)flash_address);

		flash_address += res;
	}
	log("\n");

	if (block_prop->crc32 != block_crc32)
		log("Invalid CRC for block %s\n", block_prop->name);

	return 0;
}
static int process_block_ubivol(char *filename, struct block_properties *block_prop, int dry_run)
{
	unsigned char *buf = (unsigned char *)CONFIG_UPDATE_TMPBUF;
	u64 bytes_read = 0, bytes_remain = block_prop->size;
	int i = 0, err = 0;
	int rsvd_bytes = 0;
	int found = 0;
	struct ubi_volume *vol;
	u32 block_crc32;

	log("Flashing firmware part `%s':\n", block_prop->name);

	if (!ubi) {
		err = ubi_initialize();
		if (err) {
			log("ERROR: UBI initialization failed\n");
			return err;
		}
	}

	for (i = 0; i < ubi->vtbl_slots; i++) {
		vol = ubi->volumes[i];
		if (vol && !strcmp(vol->name, block_prop->name)) {
			printf("Volume \"%s\" is found at volume id %d\n", block_prop->name, i);
			found = 1;
			break;
		}
	}
	if (!found) {
		log("ERROR: Volume \"%s\" is not found\n", block_prop->name);
		err = 1;
		goto out;
	}
	rsvd_bytes = vol->reserved_pebs * (ubi->leb_size - vol->data_pad);
	if (block_prop->size > rsvd_bytes) {
		printf("rsvd_bytes=%d vol->reserved_pebs=%d ubi->leb_size=%d\n",
		     rsvd_bytes, vol->reserved_pebs, ubi->leb_size);
		printf("vol->data_pad=%d\n", vol->data_pad);
		log("ERROR: Size of block is greater than volume size.\n");
		err = -1;
		goto out;
	}

	show_progress("Preparing...");
	if (!dry_run) {

		err = ubi_start_update(ubi, vol, block_prop->size);
		if (err < 0) {
			log("Cannot start volume update\n");
			goto out;
		}
	}

	block_crc32 = 0;
	while (bytes_remain) {
		unsigned long chunksize = min(CONFIG_UPDATE_CHUNKSIZE, bytes_remain);
		long res;

		show_progress("Reading...\t(%u%%)", (unsigned int)(bytes_read * 100 / block_prop->size));
		res = fw_load(filename, buf, chunksize, block_prop->offset + bytes_read);
		if (res < 0)
		{
			log("\nERROR: Failed to read file %s\n", filename);
			return -1;
		}

		block_crc32 = crc32(block_crc32, buf, res);
		bytes_read += res;
		bytes_remain -= res;

		show_progress("Flashing...\t(%u%%)", (unsigned int)(bytes_read * 100 / block_prop->size));
		if (!dry_run) {
			err = ubi_more_update_data(ubi, vol, buf, res);
			if (err < 0) {
				log("\nERROR: Failed to write data to UBI volume\n");
				goto out;
			}
		}
	}
	log("\n");

	if (block_prop->crc32 != block_crc32)
		log("ERROR: Invalid CRC for block %s\n", block_prop->name);

	if (err && !dry_run) {
		err = ubi_check_volume(ubi, vol->vol_id);
		if ( err < 0 )
			goto out;

		if (err) {
			ubi_warn("volume %d on UBI device %d is corrupted",
					vol->vol_id, ubi->ubi_num);
			vol->corrupted = 1;
		}

		vol->checked = 1;
		ubi_gluebi_updated(vol);
	}
out:

	return err;
}

static void ubi_cleanup(void)
{
	ubi_exit();
	del_mtd_partitions(&nand_info[0]);
}

static int process_update(char *filename, int dry_run)
{
	struct fw_update_head fw_head;
	unsigned char *header;
	unsigned char *p;
	u32 block_name_len;
	u64 block_offset, block_size;
	char *block_name;
	int image_valid = 1;
	int ret = 0;

	if (fw_load(filename, (unsigned char *)&fw_head, sizeof(fw_head), 0) < 0)
	{
		log("Failed to load file %s\n", filename);
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
		log("Failed to load file %s\n", filename);
		return 1;
	}

	p = header + sizeof(fw_head);

	printf("Global properties:\n");
	p += process_all_properties(p, NULL, dry_run, &image_valid, NULL);
	if (!image_valid) {
		log("Update image is not valid for this device\n");
		ret = -1;
		goto out;
	}

	log("\n"); /* Empty line for better output */

	while (p < header + fw_head.header_size) {
		struct block_properties block_prop;

		block_name_len = __get_unaligned_le32(p);
		p += 4;
		block_offset = __get_unaligned_le64(p);
		p += 8;
		block_size = __get_unaligned_le64(p);
		p += 8;

		if (!block_name_len)
			break;

		memset(&block_prop, 0x00, sizeof(block_prop));

		block_name = (char *)p;
		p += block_name_len;

		printf("Block '%s', offset: %lu, size: %lu\n", block_name,
				(unsigned long)block_offset, (unsigned long)block_size);
		printf("Block properties:\n");

		block_prop.name = block_name;
		block_prop.offset = block_offset;
		block_prop.size = block_size;

		p += process_all_properties(p, block_name, dry_run, &image_valid, &block_prop);

		if (image_valid) {
			if (block_prop.raw)
				ret = process_block_raw(filename, &block_prop, dry_run);
			else
				ret = process_block_ubivol(filename, &block_prop, dry_run);

			if (ret < 0) {
				log("Error occured during flashing block `%s'\n", block_name);
				goto out;
			}
		} else {
			printf("Block '%s' is not valid for this device, skipping it\n", block_name);
			image_valid = 1;
		}
	}

out:
//	if (ubi)
//		ubi_cleanup();
//	ubi = NULL;

	return ret;
}

int do_updatesim(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	return process_update(CONFIG_UPDATE_FILENAME, 1);
}

U_BOOT_CMD(
	updatesim, 1, 0, do_updatesim,
	"Simulate firmware update (dry run)",
	" - load firmware update file from SD card and parse it without flashing\n"
);

int do_update(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	return process_update(CONFIG_UPDATE_FILENAME, 0);
}

U_BOOT_CMD(
	update, 1, 0, do_update,
	"Do firmware update",
	" - load firmware update file from SD card, parse and flash it\n"
);

static const unsigned int keymap[][2] = {
	{0x05, KEY_0},
	{0x04, KEY_1},
	{0x03, KEY_2},
	{0x02, KEY_3},
	{0x01, KEY_4},
	{0x0b, KEY_5},
	{0x0a, KEY_6},
	{0x09, KEY_7},
	{0x08, KEY_8},
	{0x07, KEY_9},
	{0x1a, KEY_PAGEUP},
	{0x19, KEY_PAGEDOWN},
	{0x17, KEY_LEFT},
	{0x16, KEY_RIGHT},
	{0x14, KEY_UP},
	{0x15, KEY_DOWN},
	{0x13, KEY_ENTER},
	{0x11, KEY_SPACE},
	{0x0e, KEY_MENU},
	{0x10, KEY_DIRECTION},
	{0x0f, KEY_SEARCH},
	{0x0d, KEY_PLAYPAUSE},
	{0x1d, KEY_ESC},
	{0x1c, KEY_POWER},
	{0x1e, KEY_SLEEP},
	{0x1f, KEY_WAKEUP},
};

static int find_key(unsigned char code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keymap); i++) {
		if (keymap[i][0] == code) {
			return keymap[i][1];
		}
	}
	return -1;
}

static int key_to_number(int key)
{
	if ((key >= KEY_1) && (key <= KEY_0))
		return (key - KEY_1 + 1) % 10;
	else
		return -1;
}

#define KEYPRESS_TIMEOUT 5000000
#define BLINK_PERIOD 300000
#define I2C_DELAY 70000

static int check_for_menu_key(void)
{
	uchar code;
	int key;
	unsigned int t;

	log(" Press any key to enter update mode\n");

	/* Switch LPC to normal mode */
	code = 0x02;
	i2c_write(CONFIG_LPC_I2C_ADDR, 0, 0, &code, 1);

	t = 0;
	while (t < KEYPRESS_TIMEOUT) {
		__gpio_clear_pin(GPIO_LED_EN);
		udelay(BLINK_PERIOD / 2);
		__gpio_set_pin(GPIO_LED_EN);
		udelay(BLINK_PERIOD / 2 - I2C_DELAY);
		t += BLINK_PERIOD;

		key = -1;
		do {
			char buf[30];
			if (i2c_read(CONFIG_LPC_I2C_ADDR, 0, 0, &code, 1))
				break;

			if ((code >= 0x81) && (code <= 0x87)) {
				sprintf(buf, "n516-lpc.batt_level=%d", code - 0x81);
				setenv("batt_level_param", buf);
			}
			key = find_key(code);
		} while ((key < 0) && code);

		if (key > 0)
			break;
	}

	if (key == KEY_POWER) {
		lcd_clear();
		lcd_sync();
		code = 0x01;
		__gpio_set_pin(GPIO_LED_EN);

		while (1)
			i2c_write(CONFIG_LPC_I2C_ADDR, 0, 0, &code, 1);
	}

	if (key > 0)
		return 0;
	else
		return -1;
}

extern void metronome_disable_sync(void);
extern void metronome_enable_sync(void);

static struct list_head found_files;
struct file_entry {
	char filename[255];
	struct list_head link;
};

static void file_check(char *filename, struct file_stat *stat)
{
	struct file_entry *f, *cur;
    char *c;

	if (stat->is_directory)
        return;

    c = strstr(filename, CONFIG_UPDATE_FILEEXT);
    if (c && *(c + sizeof(CONFIG_UPDATE_FILEEXT) - 1) == '\0') {
		f = malloc(sizeof(*f));
		if (!f) {
			printf("Failed to allocate memory\n");
			return;
		}

		strncpy(f->filename, filename, 254);

		if (!list_empty(&found_files)) {
			list_for_each_entry(cur, &found_files, link) {
				if (strcmp(cur->filename, f->filename) <= 0)
					break;
			}

			list_add_tail(&f->link, &cur->link);
		} else {
			list_add_tail(&f->link, &found_files);
		}
	}
}

static int ask_user(char *buf, unsigned int len)
{
	uchar code;
	int i, n;
	int key, num;
	struct file_entry *cur;


	metronome_disable_sync();
	lcd_clear();
	eputs("Select firmware update file:\n\n");
	n = 0;
	list_for_each_entry(cur, &found_files, link) {
		n++;
		log("%d. %s\n", n, cur->filename);
		if (n > 9)
			break;
	}

	log("\nC. Exit and continue booting\n");

	metronome_enable_sync();
	lcd_sync();

	do {
		key = 0;
		num = -1;

		if (i2c_read(CONFIG_LPC_I2C_ADDR, 0, 0, &code, 1))
			continue;

		key = find_key(code);
		num = key_to_number(key);
	} while ((key != KEY_ESC) && (num < 1));

	if (num > 0) {
		i = 0;
		list_for_each_entry(cur, &found_files, link) {
			i++;
			if (i == num) {
				strncpy(buf, cur->filename, len - 1);
				break;
			}
		}

		if (i != num)
			return -1;
	}

	return num;
}

extern void _machine_restart(void);
static int do_checkupdate(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int choice;
	int res;
	int dry_run = 0;
	char filename[255];
	struct file_entry *cur, *tmp;

	if (!strcmp(argv[0], "check_and_updatesim")) {
		dry_run = 1;
		printf("Dry run mode\n");
	}

	INIT_LIST_HEAD(&found_files);

	if (init_fat() < 0)
		return 0;

	dir_fat_read("/", file_check);

	if (list_empty(&found_files))
		return 0;

	if (check_for_menu_key()) {
		res = 0;
		goto out;
	}

	choice = ask_user(filename, 255);
	if (choice < 0) {
		log("Continue booting...\n");
		res = 0;
		goto out;
	}

	saveenv();
	lcd_clear();
	log("\tStarting update...\n\n");

	res = process_update(filename, dry_run);
	if (!res) {
		log("\nUpdate completed succesfully.\nRebooting...\n");
		_machine_restart();
	}

out:
	list_for_each_entry_safe(cur, tmp, &found_files, link) {
		list_del(&cur->link);
		free(cur);
	}

	return res;
}

U_BOOT_CMD(
	check_and_update, 1, 0, do_checkupdate,
	"Check for firmware update, ask user and start\n",
	NULL
);

U_BOOT_CMD(
	check_and_updatesim, 1, 0, do_checkupdate,
	"Check for firmware update, ask user and start update simulation\n",
	NULL
);
