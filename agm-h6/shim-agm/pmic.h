#pragma once
#include <stdint.h>

int pmic_read(uint32_t reg, uint32_t *val);
int pmic_write(uint32_t reg, uint32_t val);
int pmic_wdt_extend(unsigned seconds, void (*delay_ms)(unsigned));
