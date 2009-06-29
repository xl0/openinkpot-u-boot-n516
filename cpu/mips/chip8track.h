#ifndef __CHIP8TRACK_H__
#define __CHIP8TRACK_H__

/*
 *  Copyright (C) 2009 Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#define LCDRESETHIGH    __gpio_set_pin(GPIO_RST_L)
#define LCDRESETLOW     __gpio_clear_pin(GPIO_RST_L)
#define LCDSTBYHIGH     __gpio_set_pin(GPIO_STBY)
#define LCDSTBYLOW      __gpio_clear_pin(GPIO_STBY)

#define LCDISRDY	__gpio_get_pin(GPIO_LCDRDY)


#define LCDCHIPCONFIG (u16) 0xcc10
#define LCDCHIPINIT   (u16) 0xcc20
#define LCDCHIPPWRUP  (u16) 0x1234
#define LCDCHIPDIS    (u16) 0xcc40

#define CHIPPWRDOWN     0x00
#define CHIPCONFIG      0x01
#define CHIPINIT        0x02

#define CHIPSUSPEND     0x10
#define CHIPRESUME      0x11

#define CHIPLOGO        0x20

#define CHIPREADY       0x30


#define LCD_PWRUP(pfbbegin) ({ \
        LCDRESETHIGH; \
	LCDSTBYHIGH; \
})

// 3412 0008 0008 0008 	//;cp power_up.bin
#define LCD_PWRDOWN(pfbbegin) ({ \
        u16 *pfb = (u16 *) pfbbegin; \
	u16 i; \
	LCDSTBYLOW; \
	LCDRESETLOW; \
	for(i=0; i < 32; i++) \
	{ \
		*(pfb + i) = 0x0000; \
	} \
	*(pfb + 1) = 0x0800; \
	*(pfb + 2) = 0x0800; \
	*(pfb + 3) = 0x0800; \
	*(pfb + 0) = LCDCHIPPWRUP; \
})

// 1011 0002 2600 1200 9001 0000 feca feca feca ; cp config_1a
#if 0
#define LCD_CONFIG(pfbbegin) ({ \
	u16 *pfb = (u16 *) pfbbegin; \
	*(pfb + 1) = (u16) 0x0200; \
	*(pfb + 2) = (u16) 0x0326; \
	*(pfb + 3) = (u16) 0x0012; \
	*(pfb + 4) = (u16) 0x0190; \
	*(pfb + 5) = (u16) 0x0000; \
	*(pfb + 6) = (u16) 0xcafe; \
	*(pfb + 7) = (u16) 0xcafe; \
	*(pfb + 8) = (u16) 0xcafe; \
        *(pfb + 0) = (u16) LCDCHIPCONFIG; \
})
#else
#define LCD_CONFIG(pfbbegin) ({ \
	u16 *pfb = (u16 *) pfbbegin; \
	*(pfb + 1) = (u16) 0x020f; \
	*(pfb + 2) = (u16) 0x032a; \
	*(pfb + 3) = (u16) 0x0012; \
	*(pfb + 4) = (u16) 0x0257; \
	*(pfb + 5) = (u16) 0x0000; \
	*(pfb + 6) = (u16) 0xcafe; \
	*(pfb + 7) = (u16) 0xcafe; \
	*(pfb + 8) = (u16) 0xcafe; \
        *(pfb + 0) = (u16) LCDCHIPCONFIG; \
})
#endif

// 2022 0000 0000 0000                 //; cp init_1a
#define LCD_INIT(pfbbegin) ({ \
	u16 *pfb = (u16 *) pfbbegin; \
	*(pfb + 1) = (u16) 0x0000; \
	*(pfb + 2) = (u16) 0x0000; \
	*(pfb + 3) = (u16) 0x0000; \
        *(pfb + 0) = (u16) LCDCHIPINIT; \
})

#define LCD_UPDATA(pfbbegin) ({ \
	u16 *pfb = (u16 *) pfbbegin; \
        u16 cmd = ((pfb[0] & 0x0f) + 1) & 0x0f; \
        cmd |= LCDCHIPDIS; \
        pfb[0] = cmd; \
})

#endif /* __CHIP8TRACK_H__ */
