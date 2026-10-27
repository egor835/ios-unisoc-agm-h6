/*
 * AGM H6 touch probe: Chipone ICNL9916 (TDDI) on SPI3, READ-ONLY commands.
 *
 * SPI3 controller (sprd,sc9860-spi @0x20150000), per mainline spi-sprd.c:
 * TXD 0x0, CLKD 0x4, CTL0 0x8 (bit13 SCK_REV, bit1 NG_TX, bit0 NG_RX, bits4:0<<2
 * word bits, bits11:8 CS), CTL1 0xc (bit13 TX mode, bit12 RX mode), CTL4 0x18
 * (bit9 START_RX, bits8:0 RX count), CTL5 0x1c word interval, INT_CLR 0x24,
 * INT_RAW 0x28 (bit8 TX end, bit9 RX end), STS2 0x34 (bit8 TX busy),
 * FIFO_RST 0x48, CTL7 0x4c (bits5:3 mode), CTL8/9 TX length, CTL10/11 RX
 * length; 32-entry FIFO, polled.
 * Clocks (mainline ums9230-clk.c): SPI3 enable = AP APB 0x20000000 bit 8, set
 * through 0x20001000; SPI3 source/divider = 0x20010058 (mux bits1:0, 0 = 26 MHz).
 *
 * Chipone TCS over SPI (Motorola chipone_tddi_mmi_v2 cts_tcs.c), mode 0:
 * transaction 1 sends {0xF1, cmd16, len16, crc16}; ~100 us later transaction 2
 * reads len data bytes + {ecode, cmd16 echo, crc16}. CRC-16 poly 0x8005, init 0,
 * MSB first, over everything before the CRC.
 */
#include <stdint.h>
#include "console.h"
#include "cts.h"

#define SPI		0x20150000UL
#define SPI_TXD		0x00
#define SPI_CLKD	0x04
#define SPI_CTL0	0x08
#define SPI_CTL1	0x0c
#define SPI_CTL4	0x18
#define SPI_CTL5	0x1c
#define SPI_INT_CLR	0x24
#define SPI_INT_RAW	0x28
#define SPI_STS2	0x34
#define SPI_FIFO_RST	0x48
#define SPI_CTL7	0x4c
#define SPI_CTL8	0x54
#define SPI_CTL9	0x58
#define SPI_CTL10	0x5c
#define SPI_CTL11	0x60

#define CTL0_SCK_REV	(1u << 13)
#define CTL0_NG_TX	(1u << 1)
#define CTL0_NG_RX	(1u << 0)
#define CTL0_CS0	(1u << 8)
#define CTL0_CSN	(0xfu << 8)
#define CTL1_TX		(1u << 13)
#define CTL1_RX		(1u << 12)
#define CTL4_START_RX	(1u << 9)
#define INT_TX_END	(1u << 8)
#define INT_RX_END	(1u << 9)
#define STS2_TX_BUSY	(1u << 8)
#define FIFO		32

#define APB_EB_SET	0x20001000UL
#define SPI3_EB		(1u << 8)
#define SPI3_CLK_CFG	0x20010058UL

#define TCS_RD_ADDR	0xf1
#define CMD_FW_ID	0x4003
#define CMD_FW_VER	0x4005
#define CMD_XY_INFO	0x4007

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint8_t rd8(uintptr_t a) { return *(volatile uint8_t *)a; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

static void udelay(unsigned us)
{
	uint64_t f, t0, t;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t0));
	do {
		__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
	} while (t - t0 < f / 1000000 * us);
}

static uint16_t crc16(const uint8_t *p, unsigned n)
{
	uint16_t crc = 0;

	while (n--) {
		crc ^= (uint16_t)(*p++) << 8;
		for (int i = 0; i < 8; i++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x8005) : (uint16_t)(crc << 1);
	}
	return crc;
}

static int wait_bit(uintptr_t reg, uint32_t bit, int set)
{
	for (int i = 0; i < 200000; i++)
		if (!!(rd32(reg) & bit) == set)
			return 0;
	return -1;
}

static void cs(int active)
{
	uint32_t v = rd32(SPI + SPI_CTL0);

	wr32(SPI + SPI_CTL0, active ? (v & ~CTL0_CS0) : (v | CTL0_CSN));
}

static void spi_init(void)
{
	wr32(APB_EB_SET, SPI3_EB);			/* clock gate on */
	wr32(SPI3_CLK_CFG, rd32(SPI3_CLK_CFG) & ~0x703u);	/* 26 MHz, no divider */
	udelay(10);
	uint32_t v = rd32(SPI + SPI_CTL0);
	v &= ~(CTL0_SCK_REV | CTL0_NG_TX | CTL0_NG_RX | (0x1fu << 2));
	v |= CTL0_NG_TX | (8u << 2);			/* mode 0, 8-bit words */
	wr32(SPI + SPI_CTL0, v | CTL0_CSN);
	wr32(SPI + SPI_CTL5, 1);			/* word interval 14 cycles */
	wr32(SPI + SPI_FIFO_RST, 1);
	wr32(SPI + SPI_FIFO_RST, 0);
	wr32(SPI + SPI_CTL7, rd32(SPI + SPI_CTL7) & ~(7u << 3));	/* 4-wire */
	wr32(SPI + SPI_CLKD, 2);			/* 26 MHz / 2 / 3 = 4.3 MHz */
}

static void set_mode(uint32_t mode)
{
	wr32(SPI + SPI_CTL1, (rd32(SPI + SPI_CTL1) & ~(CTL1_TX | CTL1_RX)) | mode);
}

static int spi_tx(const uint8_t *tx, unsigned len)
{
	set_mode(CTL1_TX);
	cs(1);
	while (len) {
		unsigned n = len > FIFO ? FIFO : len;
		wr32(SPI + SPI_CTL8, (rd32(SPI + SPI_CTL8) & ~0xfu) | (n >> 16));
		wr32(SPI + SPI_CTL9, n & 0xffff);
		for (unsigned i = 0; i < n; i++)
			wr8(SPI + SPI_TXD, tx[i]);
		if (wait_bit(SPI + SPI_INT_RAW, INT_TX_END, 1) || wait_bit(SPI + SPI_STS2, STS2_TX_BUSY, 0)) {
			cs(0);
			set_mode(0);
			return -1;
		}
		wr32(SPI + SPI_INT_CLR, INT_TX_END);
		tx += n;
		len -= n;
	}
	cs(0);
	set_mode(0);
	return 0;
}

static int spi_rx(uint8_t *rx, unsigned len)
{
	set_mode(CTL1_RX);
	cs(1);
	while (len) {
		unsigned n = len > FIFO ? FIFO : len;
		wr32(SPI + SPI_CTL10, (rd32(SPI + SPI_CTL10) & ~0xfu) | (n >> 16));
		wr32(SPI + SPI_CTL11, n & 0xffff);
		uint32_t v = rd32(SPI + SPI_CTL4) & ~(CTL4_START_RX | 0x1ffu);
		wr32(SPI + SPI_CTL4, v | n);
		wr32(SPI + SPI_CTL4, v | n | CTL4_START_RX);
		if (wait_bit(SPI + SPI_INT_RAW, INT_RX_END, 1)) {
			cs(0);
			set_mode(0);
			return -1;
		}
		wr32(SPI + SPI_INT_CLR, INT_RX_END);
		for (unsigned i = 0; i < n; i++)
			rx[i] = rd8(SPI + SPI_TXD);
		rx += n;
		len -= n;
	}
	cs(0);
	set_mode(0);
	return 0;
}

/* One TCS read: 0 ok, -1 SPI timeout, -2 CRC mismatch. rx holds data + 5-byte tail. */
static int tcs_read(uint16_t cmd, uint8_t *rx, unsigned datalen)
{
	uint8_t tx[7] = { TCS_RD_ADDR, cmd & 0xff, cmd >> 8, datalen & 0xff, datalen >> 8 };
	uint16_t c = crc16(tx, 5);

	tx[5] = c & 0xff;
	tx[6] = c >> 8;
	if (spi_tx(tx, sizeof(tx)))
		return -1;
	udelay(100);
	if (spi_rx(rx, datalen + 5))
		return -1;
	udelay(100);
	unsigned n = datalen + 5;
	if (crc16(rx, n - 2) != (uint16_t)(rx[n - 2] | rx[n - 1] << 8))
		return -2;
	return 0;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void cts_probe(void)
{
	uint8_t rx[64];
	int r;

	spi_init();
	con_hex2("SPI3", "ctl0=", rd32(SPI + SPI_CTL0), "clkcfg=", rd32(SPI3_CLK_CFG));

	r = tcs_read(CMD_FW_ID, rx, 4);
	con_hex2("CTS FWID", "rc=", (uint32_t)r, "raw=", le32(rx));
	con_hex2("CTS FWID", "tail=", le32(rx + 4), "crc=", rx[7] | rx[8] << 8);
	r = tcs_read(CMD_FW_VER, rx, 4);
	con_hex2("CTS FWVER", "rc=", (uint32_t)r, "raw=", le32(rx));
	r = tcs_read(CMD_XY_INFO, rx, 8);
	con_hex2("CTS XY", "rc=", (uint32_t)r, "raw=", le32(rx));
	con_hex2("CTS XY", "hi=", le32(rx + 4), "ecode=", rx[8]);
}
