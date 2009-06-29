#include <config.h>
#include <common.h>
#include <command.h>
#include <stdarg.h>
#include <i2c.h>
#include <lcd.h>


static int lcd_test (cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	eputs("test test test ");
	return 0;
}


U_BOOT_CMD(lcdtest, 1, 1, lcd_test,
		"Test LCD console",
		NULL
	  );

static int read_key(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	uchar key;
	int ret;

	ret = i2c_read(CONFIG_LPC_I2C_ADDR, 0, 0, &key, 1);
	if (ret)
		printf("I2C read failure: %d\n", ret);
	else
		printf("Key: 0x%02x\n", key);

	return 0;
}

U_BOOT_CMD(read_key, 1, 1, read_key,
		"Read key from LPC",
		NULL
);

static int show_image(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	uchar *dst = lcd_base;
	uchar *src;
	int i;

	if (argc < 2) {
		printf("Usage: show_image <address>\n");
		return -1;
	}

	src = simple_strtoul(argv[1], NULL, 16);

	memcpy(dst, src, 480000);

	lcd_sync();
}

U_BOOT_CMD(show_image, 2, 1, show_image,
		"Show image on LCD",
		NULL
);
