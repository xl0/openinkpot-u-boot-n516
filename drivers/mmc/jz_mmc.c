#include <config.h>
#include <common.h>
#include <command.h>
#include <mmc.h>
#include <part.h>
#include <malloc.h>
#include <mmc.h>
#include <jz_mmc.h>

#ifdef CONFIG_JZ4730
#include <asm-mips/jz4730.h>
#endif
#ifdef CONFIG_JZ4740
#include <asm-mips/jz4740.h>
#endif

#define MMC_DEBUG_LEVEL		0		/* Enable Debug: 0 - no debug */
#if MMC_DEBUG_LEVEL

#define DEBUG(n, args...)			\
    do {    \
	if (n <=  MMC_DEBUG_LEVEL) {	\
		printf(args);	\
	}    \
    } while(0)
#else
#define DEBUG(n, args...) do { } while(0)
#endif /* MMC_DEBUG_EN */


#define MMC_CLOCK_SLOW    400000
#define MMC_CLOCK_FAST  20000000      /* 20 MHz for maximum for normal operation */
#define SD_CLOCK_FAST   24000000      /* 24 MHz for SD Cards */


#define MMC_EVENT_NONE	        0x00	/* No events */
#define MMC_EVENT_RX_DATA_DONE	0x01	/* Rx data done */
#define MMC_EVENT_TX_DATA_DONE	0x02	/* Tx data done */
#define MMC_EVENT_PROG_DONE	0x04	/* Programming is done */

#define MMC_IRQ_MASK()				\
do {						\
	REG_MSC_IMASK = 0xffff;			\
	REG_MSC_IREG = 0xffff;			\
} while (0)

#define __msc_init_io()				\
do {						\
	__gpio_as_output(GPIO_SD_VCC_EN_N);	\
	__gpio_as_input(GPIO_SD_CD_N);		\
} while (0)

#define __msc_enable_power()			\
do {						\
	__gpio_clear_pin(GPIO_SD_VCC_EN_N);	\
} while (0)

#define __msc_disable_power()			\
do {						\
	__gpio_set_pin(GPIO_SD_VCC_EN_N);	\
} while (0)

#define __msc_card_detected()			\
({						\
	int detected = 1;			\
	__gpio_as_input(GPIO_SD_CD_N);		\
	if (__gpio_get_pin(GPIO_SD_CD_N))	\
		detected = 0;			\
	detected;				\
})

static int use_4bit;                    /* Use 4-bit data bus */
static int do_init;

/* Stop the MMC clock and wait while it happens */
static inline int jz_mmc_stop_clock(void)
{
	int timeout = 1000;

	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_STOP;

	while (timeout && (REG_MSC_STAT & MSC_STAT_CLK_EN)) {
		timeout--;
		if (timeout == 0) {
			return TIMEOUT;
		}
		udelay(1);
	}
        return 0;
}


/* Start the MMC clock and operation */
static inline int jz_mmc_start_clock(void)
{
	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_START | MSC_STRPCL_START_OP;
	return 0;
}

static inline u32 jz_mmc_calc_clkrt(int is_sd, u32 rate)
{
	u32 clkrt;
	u32 clk_src = is_sd ? 24000000: 16000000;

	clkrt = 0;
	while (rate < clk_src)
	{
		clkrt ++;
		clk_src >>= 1;
	}
	return clkrt;
}
/* Set the MMC clock frequency */
void jz_mmc_set_clock(int sd, u32 rate)
{
	DEBUG(3, "%s: clock=%u\n", __func__, rate);
	jz_mmc_stop_clock();

	/* Select clock source of MSC */
	__cpm_select_msc_clk(sd);

	/* Set clock divisor of MSC */
	REG_MSC_CLKRT = jz_mmc_calc_clkrt(sd, rate);
}

static int jz_mmc_check_status(struct mmc_cmd *cmd)
{
	u32 status = REG_MSC_STAT;
	DEBUG(3, "%s: %d: status=0x%08x\n", __func__, __LINE__, status);

	/* Checking for response or data timeout */
	if (status & (MSC_STAT_TIME_OUT_RES | MSC_STAT_TIME_OUT_READ)) {
		printf("MMC/SD timeout, MMC_STAT 0x%x CMD %d\n", status, cmd->cmdidx);
		return TIMEOUT;
	}

	/* Checking for CRC error */
	if (status & (MSC_STAT_CRC_READ_ERROR | MSC_STAT_CRC_WRITE_ERROR | MSC_STAT_CRC_RES_ERR)) {
		printf("MMC/CD CRC error, MMC_STAT 0x%x\n", status);
		return COMM_ERR;
	}

	return 0;
}

/* Obtain response to the command and store it to response buffer */
static void jz_mmc_get_response(struct mmc_cmd *cmd)
{
	int i;
	unsigned char buf[6], *p;
	u32 data, v, w1, w2;
	uint *resp = cmd->response;

	DEBUG(3, "fetch response for request %d, cmd %d\n", cmd->resp_type, cmd->cmdidx);

	if (cmd->resp_type == MMC_RSP_NONE)
		return;

	if (cmd->resp_type & MMC_RSP_136) {
		data = REG_MSC_RES;
		v = data & 0xffff;
		for (i = 0; i < 4; i++) {
			data = REG_MSC_RES;
			w1 = data & 0xffff;
			data = REG_MSC_RES;
			w2 = data & 0xffff;
			resp[i] = v << 24 | w1 << 8 | w2 >> 8;
			v = w2;
		}
#if (MMC_DEBUG_LEVEL >= 3)
		for (i = 0; i < 4; i++) {
			DEBUG(3, "%08x ", cmd->response[i]);
		}
		DEBUG(3, "\n");
#endif
	} else {
		data = REG_MSC_RES;
		buf[0] = (data >> 8) & 0xff;
		buf[1] = data & 0xff;
		data = REG_MSC_RES;
		buf[2] = (data >> 8) & 0xff;
		buf[3] = data & 0xff;
		data = REG_MSC_RES;
		buf[4] = data & 0xff;
		resp[0] = buf[1] << 24 | buf[2] << 16 | buf[3] << 8 | buf[4];

		DEBUG(3, "request %d, response 0x%08x\n", cmd->resp_type, resp[0]);
	}
}

static int jz_mmc_receive_data(struct mmc_data *mmc_data)
{
	u32  stat, timeout, data, cnt;
	u32 *buf = (u32 *)mmc_data->dest;
	u32 wblocklen = (u32)(mmc_data->blocksize + 3) >> 2; /* length in word */

	timeout = 0x3ffffff;

	while (timeout) {
		timeout--;
		stat = REG_MSC_STAT;

		if (stat & MSC_STAT_TIME_OUT_READ)
			return TIMEOUT;
		else if (stat & MSC_STAT_CRC_READ_ERROR)
			return COMM_ERR;
		else if (!(stat & MSC_STAT_DATA_FIFO_EMPTY) 
			 || (stat & MSC_STAT_DATA_FIFO_AFULL)) {
			/* Ready to read data */
			break;
		}
		udelay(1);
	}
	if (!timeout)
		return TIMEOUT;

	/* Read data from RXFIFO. It could be FULL or PARTIAL FULL */
	cnt = wblocklen;
	while (cnt) {
		data = REG_MSC_RXFIFO;
		*buf++ = data;
		cnt --;
		while (cnt && (REG_MSC_STAT & MSC_STAT_DATA_FIFO_EMPTY))
			;
	}
	return 0;
}

static int jz_mmc_transmit_data(struct mmc_data *mmc_data)
{
	uint nob = mmc_data->blocks;
	uint wblocklen = (uint)(mmc_data->blocksize + 3) >> 2; /* length in word */
	const char *buf = mmc_data->src;
	u32 *wbuf = (u32 *)buf;
	u32 waligned = (((u32)buf & 0x3) == 0); /* word aligned ? */
	u32 stat, timeout, data, cnt;

	for (; nob >= 1; nob--) {
		timeout = 0x3FFFFFF;

		while (timeout) {
			timeout--;
			stat = REG_MSC_STAT;

			if (stat & (MSC_STAT_CRC_WRITE_ERROR | MSC_STAT_CRC_WRITE_ERROR_NOSTS))
				return COMM_ERR;
			else if (!(stat & MSC_STAT_DATA_FIFO_FULL)) {
				/* Ready to write data */
				break;
			}

			udelay(1);
		}

		if (!timeout)
			return TIMEOUT;

		/* Write data to TXFIFO */
		cnt = wblocklen;
		while (cnt) {
			while (REG_MSC_STAT & MSC_STAT_DATA_FIFO_FULL)
				;

			if (waligned) {
				REG_MSC_TXFIFO = *wbuf++;
			}
			else {
				data = *(buf++);
				data |= (*(buf++) << 8);
				data |= (*(buf++) << 16);
				data |= (*(buf++) << 24);
				REG_MSC_TXFIFO = data;
			}

			cnt--;
		}
	}

	return 0;
}
static int jz_mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd, struct mmc_data *data)
{
	u32 cmdat = 0;
	int retval, timeout = 0x3fffff;

	DEBUG(3, "%s:%d: use_4bit=%d \n", __func__, __LINE__, use_4bit);
	/* stop clock */
	jz_mmc_stop_clock();

	/* mask all interrupts */
	REG_MSC_IMASK = 0xffff;

	/* clear status */
	REG_MSC_IREG = 0xffff;

	/* use 4-bit bus width when possible */
	if (use_4bit)
		cmdat |= MSC_CMDAT_BUS_WIDTH_4BIT;

	if (do_init) {
		do_init = 0;
		cmdat |= MSC_CMDAT_INIT;
	}

	if (data) {
		if (data->flags & MMC_DATA_READ)
			cmdat |= MSC_CMDAT_DATA_EN | MSC_CMDAT_READ;
		if (data->flags & MMC_DATA_WRITE)
			cmdat |= MSC_CMDAT_DATA_EN | MSC_CMDAT_WRITE;
	}

	/* Set response type */
	switch (cmd->resp_type) {
	case MMC_RSP_NONE:
		break;
	case MMC_RSP_R1b:
		cmdat |= MSC_CMDAT_BUSY;
		/*FALLTHRU*/
	case MMC_RSP_R1:
		cmdat |= MSC_CMDAT_RESPONSE_R1;
		break;
	case MMC_RSP_R2:
		cmdat |= MSC_CMDAT_RESPONSE_R2;
		break;
	case MMC_RSP_R3:
		cmdat |= MSC_CMDAT_RESPONSE_R3;
		break;
	default:
		break;
	}

	DEBUG(3, "%s:%d: CMD=%u, ARG=%u\n", __func__, __LINE__, cmd->cmdidx, cmd->cmdarg);

	REG_MSC_CMD = cmd->cmdidx;
	REG_MSC_ARG = cmd->cmdarg;

	if (data) {
		DEBUG(3, " data: blocksize=%u, blocks=%u\n", data->blocksize, data->blocks); 
		REG_MSC_BLKLEN = data->blocksize;
		REG_MSC_NOB = data->blocks;
	}

	/* Set command */
	REG_MSC_CMDAT = cmdat;

	/* Start MMC/SD clock and send command to card */
	jz_mmc_start_clock();

	/* Wait for command completion */
	while (timeout-- && !(REG_MSC_STAT & MSC_STAT_END_CMD_RES))
		;

	if (timeout == 0)
		return TIMEOUT;

	REG_MSC_IREG = MSC_IREG_END_CMD_RES; /* clear flag */

	/* Check for status */
	retval = jz_mmc_check_status(cmd);
	if (retval) {
		return retval;
	}

	if (cmd->resp_type == MMC_RSP_NONE)
		return 0;

	/* Get response */
	jz_mmc_get_response(cmd);

	/* Start data operation */
	if (data) {
		if (data->flags & MMC_DATA_READ) {
			retval = jz_mmc_receive_data(data);
		}

		if (data->flags & MMC_DATA_WRITE) {
			retval = jz_mmc_transmit_data(data);
		}

		if (retval) {
			printf("Data transfer error: %d\n", retval);
			return retval;
		}

		/* Wait for Data Done */
		while (!(REG_MSC_IREG & MSC_IREG_DATA_TRAN_DONE))
			;
		REG_MSC_IREG = MSC_IREG_DATA_TRAN_DONE; /* clear status */
	}

	/* Wait for Prog Done event */
	if (cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION) {
		while (!(REG_MSC_IREG & MSC_IREG_PRG_DONE))
			;
		REG_MSC_IREG = MSC_IREG_PRG_DONE; /* clear status */
	}

	return 0;
}

static void jz_mmc_set_ios(struct mmc *mmc)
{
	DEBUG(3, "%s: %d\n", __func__, __LINE__);
	if (mmc->clock)
		jz_mmc_set_clock(1, mmc->clock);

	if (mmc->bus_width)
		use_4bit = 1;
	else
		use_4bit = 0;
}

static int jz_mmc_init(struct mmc *mmc)
{
	DEBUG(3, "%s: %d\n", __func__, __LINE__);
	if (!__msc_card_detected())
		return NO_CARD_ERR;

	printf("MMC card found\n");
	// Step-1: init GPIO
	__gpio_as_msc();

	__msc_init_io();

	// Step-2: turn on power of card
	__msc_enable_power();

	// Step-3: Reset MSC Controller.
	__msc_reset();

	// Step-3: mask all IRQs.
	MMC_IRQ_MASK();

	// Step-4: stop MMC/SD clock
	jz_mmc_stop_clock();

	jz_mmc_set_clock(1, 400000);

	do_init = 1;
	use_4bit = 0;

	return 0;
}

int jz_mmc_initialize(bd_t *bis)
{
	struct mmc *mmc;

	DEBUG(3, "%s\n", __func__);

	mmc = malloc(sizeof(*mmc));
	if (!mmc)
		printf("Unable to malloc memory for mmc struct\n");

	sprintf(mmc->name, "JZ_MMC");
	mmc->send_cmd = jz_mmc_send_cmd;
	mmc->set_ios = jz_mmc_set_ios;
	mmc->init = jz_mmc_init;
	mmc->f_min = MMC_CLOCK_SLOW;
	mmc->f_max = SD_CLOCK_FAST;
	mmc->host_caps =
		MMC_MODE_4BIT | MMC_MODE_HS
		| MMC_MODE_HS_52MHz;
	mmc->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;

	mmc_register(mmc);

	return 0;
}

