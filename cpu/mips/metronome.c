#include <common.h>
#include <linux/types.h>
#include <asm/errno.h>
#include <linux/unaligned/access_ok.h>
#include <nand.h>
#include <malloc.h>
#if defined(CONFIG_JZ4730)
#include <asm/jz4730.h>
#endif
#if defined(CONFIG_JZ4740)
#include <asm/jz4740.h>
#endif
#if defined(CONFIG_JZ5730)
#include <asm/jz5730.h>
#endif
#include "metronome.h"

#define mdelay(n) 		udelay((n)*1000)

static int sync_disabled = 0;
static int corrupted_waveforms = 0;

int chip8track_cmd_display(vidinfo_t *panel_info)
{
        int i;
        u16 *sp = (u16 *)panel_info->jz_fb.pfbcmdbegin;
        u16 cmd = ((sp[0] & 0x0f) + 1) & 0x0f;
        u16 bdrv = 0x0;
		u16 bdr = 0;

        cmd |= LCDCHIPDIS;

        sp[1] = (bdr << 3) | ((bdrv & 0x0f) << 4)
                | ((panel_info->jz_fb.wfm_fc -1) << 8);
        for (i = 2; i < 32; i++)
                sp[i] = 0;
        sp[32] = sp[1] + cmd;
        sp[0] = cmd;

        return 0;
}

/* the waveform structure that is coming from userspace firmware */
struct waveform_hdr {
	u8 stuff[32];

	u8 wmta[3];
	u8 fvsn;

	u8 luts;
	u8 mc;
	u8 trc;
	u8 stuff3;

	u8 endb;
	u8 swtb;
	u8 stuff2a[2];

	u8 stuff2b[3];
	u8 wfm_cs;
} __attribute__ ((packed));

/* main metronomefb functions */
static u8 calc_cksum(int start, int end, u8 *mem)
{
	u8 tmp = 0;
	int i;

	for (i = start; i < end; i++)
		tmp += mem[i];

	return tmp;
}

static u16 calc_img_cksum(u16 *start, int length)
{
	u16 tmp = 0;

	while (length--)
		tmp += *start++;

	return tmp;
}

static void wait_for_ready(void)
{
	while(1)
	{
		if(!(__gpio_get_pin(GPIO_LCDRDY)))
			break;
	}
	while(1)
	{
		if((__gpio_get_pin(GPIO_LCDRDY)))
			break;
	}
	return;
}

/* here we decode the incoming waveform file and populate metromem */
int load_waveform(vidinfo_t *panel_info, u8 *wfmem, u8 *mem, size_t size, int m, int t)
{
	int tta;
	int wmta;
	int trn = 0;
	int i;
	unsigned char v;
	u8 cksum;
	int cksum_idx;
	int wfm_idx, owfm_idx;
	int mem_idx = 0;
	struct waveform_hdr *wfm_hdr;
	u8 *metromem = wfmem;
	u8 mc, trc;
	u16 *p;
	u16 img_cksum;

	debug("Loading waveforms, mode %d, temperature %d\n", m, t);

	wfm_hdr = (struct waveform_hdr *) mem;

	if (wfm_hdr->fvsn != 1) {
		printf("Error: bad fvsn %x\n", wfm_hdr->fvsn);
		return -EINVAL;
	}
	if (wfm_hdr->luts != 0) {
		printf("Error: bad luts %x\n", wfm_hdr->luts);
		return -EINVAL;
	}
	cksum = calc_cksum(32, 47, mem);
	if (cksum != wfm_hdr->wfm_cs) {
		printf("Error: bad cksum %x != %x\n", cksum,
					wfm_hdr->wfm_cs);
		return -EINVAL;
	}
	mc = wfm_hdr->mc + 1;
	trc = wfm_hdr->trc + 1;

	for (i = 0; i < 5; i++) {
		if (*(wfm_hdr->stuff2a + i) != 0) {
			printf("Error: unexpected value in padding\n");
			return -EINVAL;
		}
	}

	/* calculating trn. trn is something used to index into
	the waveform. presumably selecting the right one for the
	desired temperature. it works out the offset of the first
	v that exceeds the specified temperature */
	if ((sizeof(*wfm_hdr) + trc) > size)
		return -EINVAL;

	for (i = sizeof(*wfm_hdr); i <= sizeof(*wfm_hdr) + trc; i++) {
		if (mem[i] > t) {
			trn = i - sizeof(*wfm_hdr) - 1;
			break;
		}
	}

	/* check temperature range table checksum */
	cksum_idx = sizeof(*wfm_hdr) + trc + 1;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(sizeof(*wfm_hdr), cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		printf("Error: bad temperature range table cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* check waveform mode table address checksum */
	wmta = get_unaligned_le32(wfm_hdr->wmta) & 0x00FFFFFF;
	cksum_idx = wmta + m*4 + 3;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(cksum_idx - 3, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		printf("Error: bad mode table address cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* check waveform temperature table address checksum */
	tta = get_unaligned_le32(mem + wmta + m * 4) & 0x00FFFFFF;
	cksum_idx = tta + trn*4 + 3;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(cksum_idx - 3, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		printf("Error: bad temperature table address cksum"
			" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* here we do the real work of putting the waveform into the
	metromem buffer. this does runlength decoding of the waveform */
	wfm_idx = get_unaligned_le32(mem + tta + trn * 4) & 0x00FFFFFF;
	owfm_idx = wfm_idx;
	if (wfm_idx > size)
		return -EINVAL;
	while (wfm_idx < size) {
		unsigned char rl;
		v = mem[wfm_idx++];
		if (v == wfm_hdr->swtb) {
			while (((v = mem[wfm_idx++]) != wfm_hdr->swtb) &&
				wfm_idx < size)
				metromem[mem_idx++] = v;

			continue;
		}

		if (v == wfm_hdr->endb)
			break;

		rl = mem[wfm_idx++];
		for (i = 0; i <= rl; i++)
			metromem[mem_idx++] = v;
	}

	cksum_idx = wfm_idx;
	if (cksum_idx > size)
		return -EINVAL;
	debug("mem_idx = %u\n", mem_idx);
	cksum = calc_cksum(owfm_idx, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		printf("Error: bad waveform data cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	p = (u16 *)wfmem;
	img_cksum = calc_img_cksum(p, 16384 / 2);
	p[16384 / 2] = __cpu_to_le16(img_cksum);

	panel_info->jz_fb.wfm_fc = mem_idx/64; /* frame count */
	return 0;
}

static void  __gpio_init_chip8track(void)
{
	__gpio_as_output(GPIO_RST_L);
	__gpio_as_output(GPIO_STBY);
	__gpio_as_input(GPIO_LCDRDY);
	__gpio_as_input(GPIO_ERR);

	__gpio_clear_pin(GPIO_RST_L);
	__gpio_clear_pin(GPIO_STBY);

	mdelay(10);//it's neccessary, othrewise display will have some wrong
}

extern void flush_dcache_all(void);
extern int run_command (const char *cmd, int flag);
void chip8track_init(vidinfo_t *panel_info)
{
	u8 *buf;
	nand_info_t *nand;
	size_t length = CONFIG_METRONOME_WF_LEN;
	char cmd[70];
	
	buf = malloc(CONFIG_METRONOME_WF_LEN);
#if 0
#ifdef CONFIG_SYS_NAND_SELECT_DEVICE
	board_nand_select_device(nand_info[0].priv, 0);
#endif

	nand = &nand_info[0];


	if (nand_read_skip_bad(nand, CONFIG_METRONOME_WF_NAND_OFFSET,
				&length, buf))
		printf("Cannot read waveforms from flash!\n");
#endif
	sprintf(cmd, "ubi part nand " CONFIG_UBI_PARTITION "; ubi read 0x%p " CONFIG_UBI_WF_VOLUME " 0x%x",
			buf, CONFIG_METRONOME_WF_LEN);
	run_command(cmd, 0);

	__gpio_init_chip8track();

//	REG_LCD_DA1 = (unsigned long)wfmdata;
	
	if (load_waveform(panel_info, (u8 *)panel_info->jz_fb.pfbwfmbegin, buf, CONFIG_METRONOME_WF_LEN, 0, 25)) {
		printf("Waveform loading error!\n");
		corrupted_waveforms = 1;
	}
	memset(panel_info->jz_fb.pfbdatabegin, 0xff, 800*600);

	LCD_PWRDOWN (panel_info->jz_fb.pfbcmdbegin);	// lcd reset stdy down
	LCD_PWRUP (panel_info->jz_fb.pfbcmdbegin);	//lcd reset stby high
	flush_cache_all();
	wait_for_ready();

	LCD_CONFIG (panel_info->jz_fb.pfbcmdbegin);
	flush_cache_all();
	wait_for_ready();

	LCD_INIT (panel_info->jz_fb.pfbcmdbegin);
	flush_cache_all();
	wait_for_ready();

	chip8track_sync(panel_info);

	if (load_waveform(panel_info, (u8 *)panel_info->jz_fb.pfbwfmbegin, buf, CONFIG_METRONOME_WF_LEN, 3, 25)) {
		printf("Waveform loading error!\n");
		corrupted_waveforms = 1;
	}

/*	sprintf(cmd, "ubi read 0x%p " CONFIG_UBI_BOOTSPLASH_VOLUME " 0x%x", panel_info->jz_fb.pfbdatabegin, CONFIG_METRONOME_BOOTSPLASH_LEN);
	run_command(cmd, 0);
	chip8track_sync(panel_info);
*/
	free(buf);

	return;
}

void chip8track_sync(vidinfo_t *panel_info)
{
	if (!sync_disabled && !corrupted_waveforms) {
		chip8track_cmd_display(panel_info);
		flush_cache_all();
		wait_for_ready();
	}
}

void metronome_disable_sync(void)
{
	sync_disabled = 1;
}

void metronome_enable_sync(void)
{
	sync_disabled = 0;
}
