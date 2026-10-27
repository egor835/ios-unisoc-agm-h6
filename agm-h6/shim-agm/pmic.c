/*
 * AGM H6: the SC2730 PMIC behind the UMS9230 ADI bus, and its watchdog.
 *
 * Register access follows the vendor Linux driver spi-sprd-adi.c
 * ("sprd,sharkl5pro-adi" variant, which "sprd,qogirl6-adi" matches):
 *   slave window = ADI base + 0x20000; a PMIC register reg is at window + reg
 *   read:  write that physical address to RD_CMD (+0x28), poll RD_DATA (+0x2c)
 *          until bit 31 (busy) clears; bits 15:0 are the value, bits 30:16
 *          echo (address & 0x1ffff) >> 2
 *   write: wait for an empty write FIFO (ARM_FIFO_STS +0x30, bit 10), then
 *          store the value into the window
 *
 * The watchdog (vendor sprd_pmic_wdt.c, SC2730 base 0x40) is armed by the
 * bootloader; on Android the CM4 takes it over when Linux asks over SIPC.
 * xnu never does, so it resets the phone after ~5 minutes. The shim reloads
 * it with a longer timeout, the same LOAD sequence the Linux drivers use, and
 * leaves it running: a hung phone still comes back by itself.
 */
#include <stdint.h>
#include "console.h"
#include "pmic.h"

#define ADI_BASE		0x64200000UL
#define ADI_RD_CMD		0x28
#define ADI_RD_DATA		0x2c
#define ADI_ARM_FIFO_STS	0x30
#define ADI_SLAVE		(ADI_BASE + 0x20000)

#define RD_BUSY			(1u << 31)
#define FIFO_EMPTY		(1u << 10)
#define FIFO_FULL		(1u << 11)

#define WDT			0x40	/* SC2730 watchdog */
#define WDT_LOAD_LOW		(WDT + 0x00)
#define WDT_LOAD_HIGH		(WDT + 0x04)
#define WDT_CTRL		(WDT + 0x08)
#define WDT_INT_RAW		(WDT + 0x10)
#define WDT_CNT_LOW		(WDT + 0x18)
#define WDT_CNT_HIGH		(WDT + 0x1c)
#define WDT_LOCK		(WDT + 0x20)
#define WDT_CTRL_RUN		(1u << 1)
#define WDT_INT_LD_BUSY		(1u << 4)
#define WDT_UNLOCK_KEY		0xe551
#define WDT_HZ			32768u

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* 0 on success; -1 on timeout, -2 when the echoed address does not match. */
int pmic_read(uint32_t reg, uint32_t *val)
{
	uint32_t paddr = (uint32_t)(ADI_SLAVE + reg), v = RD_BUSY;

	wr32(ADI_BASE + ADI_RD_CMD, paddr);
	for (int i = 0; i < 100000 && (v & RD_BUSY); i++)
		v = rd32(ADI_BASE + ADI_RD_DATA);
	if (v & RD_BUSY)
		return -1;
	if (((v >> 16) & 0x7fff) != ((paddr & 0x1ffff) >> 2))
		return -2;
	*val = v & 0xffff;
	return 0;
}

int pmic_write(uint32_t reg, uint32_t val)
{
	int i;

	for (i = 0; i < 100000 && !(rd32(ADI_BASE + ADI_ARM_FIFO_STS) & FIFO_EMPTY); i++)
		;
	if (rd32(ADI_BASE + ADI_ARM_FIFO_STS) & FIFO_FULL)
		return -1;
	wr32(ADI_SLAVE + reg, val & 0xffff);
	for (i = 0; i < 100000 && !(rd32(ADI_BASE + ADI_ARM_FIFO_STS) & FIFO_EMPTY); i++)
		;
	return 0;
}

static uint32_t wdt_count(void)
{
	uint32_t lo = 0, hi = 0;

	if (pmic_read(WDT_CNT_LOW, &lo) || pmic_read(WDT_CNT_HIGH, &hi))
		return 0xffffffff;
	return hi << 16 | lo;
}

/*
 * Show the watchdog state; if it runs, reload it with `seconds`.
 * Returns 0 when reloaded, 1 when it is not running (left alone), <0 on error.
 */
int pmic_wdt_extend(unsigned seconds, void (*delay_ms)(unsigned))
{
	uint32_t ctrl = 0, llo = 0, lhi = 0, raw = 0;
	int r;

	if ((r = pmic_read(WDT_CTRL, &ctrl)) || (r = pmic_read(WDT_LOAD_LOW, &llo)) ||
	    (r = pmic_read(WDT_LOAD_HIGH, &lhi))) {
		con_hex("PWDT err", (uint64_t)(int64_t)r);
		return r;
	}
	uint32_t c0 = wdt_count();
	delay_ms(1000);
	uint32_t c1 = wdt_count();
	con_hex2("PWDT", "ctrl=", ctrl, "load=", lhi << 16 | llo);
	con_hex2("PWDT", "cnt=", c0, "+1s=", c1);
	if (!(ctrl & WDT_CTRL_RUN)) {
		con_str("PWDT", "not running: left alone");
		return 1;
	}

	uint32_t load = seconds * WDT_HZ;
	pmic_write(WDT_LOCK, WDT_UNLOCK_KEY);
	for (int i = 0; i < 1000; i++) {
		if (pmic_read(WDT_INT_RAW, &raw) || !(raw & WDT_INT_LD_BUSY))
			break;
		delay_ms(1);
	}
	pmic_write(WDT_LOAD_HIGH, load >> 16);
	pmic_write(WDT_LOAD_LOW, load & 0xffff);
	pmic_write(WDT_LOCK, (~WDT_UNLOCK_KEY) & 0xffff);
	delay_ms(100);
	con_hex2("PWDT new", "load=", load, "cnt=", wdt_count());
	return 0;
}
