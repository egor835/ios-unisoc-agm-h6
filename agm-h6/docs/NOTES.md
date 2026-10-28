# AGM H6 (Unisoc T606 / UMS9230) — данные для порта XNU

Источник: живой DTB (`fdt.dtb` / `fdt.dts`), снят 2026-10-23 с GSI + Magisk.

| Что | Значение |
|---|---|
| CPU | cpu0–5 Cortex-A55 r2p0 (MPIDR 0x000–0x500), cpu6–7 Cortex-A75 r3p1 (0x600–0x700); Aff1, shift 8 |
| ISA | atomics (LSE), lrcpc, dcpop, asimddp — ARMv8.2, эмуляция атомиков не нужна |
| PSCI | 0.2, method = smc |
| GIC | v3: dist 0x10000000 (0x20000), redist 0x10040000 (0x100000, stride 0x20000) |
| RAM | 8 ГБ: 0x80000000, size 0x1_FFFFF000; много reserved (modem 0x89600000, TOS 0xb0060000, widevine 0xdf400000, …) |
| Хранилище | UFS, `sprd,ufshc` @ 0x20200000 (+ ap_ahb 0x20400000, aon_apb 0x64000000, analog 0x64560000) |
| Экран | ICNL9916 (huayin), 720×1612, DSI 4 lane, `dsi-work-mode = 1`; DPU 0x31000000, DSI 0x31100000 |
| Фреймбуфер u-boot | 0x9e000000 (reserved 0xab0000), `logo_bpix=24` |
| Тач | Chipone ICNL9916 TDDI, SPI3 @ 0x20150000, irq-gpio 0x90, rst-gpio 0x91 (модуль `icnl9916-tddi.ko`) |
| UART | ttyS1 @ 0x200b0000, earlycon в cmdline |
| Boot | boot image v4 (Image без сжатия, ~38 МБ) + vendor_boot + init_boot; есть раздел `hypervsior_*` |
| GPU | Mali-G57 |

## Загрузка своего кода (проверено 2026-10-23)
- `fastboot boot` **не работает**: в таблице команд u-boot нет `boot`, он отвечает OKAY и грузит ядро с флеша (проверено подменой `uname -v`).
- Рабочий путь: `fastboot flash boot_b <img>` + `fastboot set_active b` (после этого u-boot сам перезагружается). Образ = стоковый boot целиком (с AVB-футером), подменены только байты ядра; Image-заголовок с `text_offset = 0x80000`.
- Проба `probe/mkprobe.py` (фото `../IMG_20261023_185134.jpg`):
  - фреймбуфер 0x9e000000 сканируется, **4 Б/пиксель** (XRGB8888, stride 2880; `logo_bpix=24` = 24 бита цвета). Вывод «3 Б» по пробе 1 был ошибочным, пробу 2 разобрал по профилю яркости;
  - на boot CPU (A55, MIDR part 0xd05) `ID_AA64MMFR0_EL1[31:20] = 0x001` → **16K-гранула есть**, 4K/64K тоже;
  - **CurrentEL = EL2** — гипервизора над нами нет, вход как в Linux.
  - проба 2: CNTFRQ = 26 МГц (0x018CBA80), счётчик идёт; PSCI_VERSION = 1.0; PSCI_FEATURES(SYSTEM_RESET) = 0 (поддерживается).
    Почему проба 1 не перезагрузилась — не выяснено (возможно, перезагрузка была, и слот B грузился по кругу).
  - Восстановление после зависшей пробы: держать Power ~7 с, после этого u-boot откатывается на слот A.

## XNU на AGM H6 — первая загрузка (2026-10-25, фото `../IMG_20261025_010843.jpg`, `../IMG_20261025_010956.jpg`)
- Ядро: ios-topaz e477291 + `xnu/patches/0960-agm-board.patch` (ветка `agm`), сборка на Mac `SWEET_AGM=1 tools/build-local.sh` → `xnu-out/agm-0960/`.
- ADT `mkadt.py --agm`, payload `mkpayload.py` (cmdline `debug=0xc serial=3 cpus=1 keepsyms=1 -v sweetfb_native=1 wdt=-1`), shim `shim-agm/build-agm.sh <payload>` → `tools/run-b-once.sh`.
- Результат: ранняя консоль, IOKit, platform expert, AKS, WDT, IOSurface, HID, AMFI, батарея (static), `SWEET CPU: 1/1 CPUs initialized, SGI1 verified`, `AppleCLCD 720x1612 stride 2880 at 0x9e000000`, HFS, сеть → **`Waiting on IOMedia Apple_HFS`** (корня ещё нет — ожидаемо).
- Корень: `sweet_ufs` берёт GPT-раздел по имени (`ufsroot=`, по умолчанию userdata). Образ `mkiosroot.sh` ≈16 ГБ HFS+ (iOS 16.7.16 iPhone X).

## Корень iOS и launchd (2026-10-25)
- GPT: `userdata` укорочен до ~103,5 ГиБ, новый раздел `iosroot` (слот 69, 28925440..62479871, 128 ГиБ, тип Apple HFS+). Бэкап старой таблицы `backup/gpt-sda-{head,tail}.bin`, инструменты `tools/gptshow.py`, `tools/gptsplit.py`.
- Корень: iOS 16.7.16 20H392 (iPhone X) → `ios/ios16-root.img` (16 ГБ HFS+, `mkiosroot.sh` + `sweet-root-tweaks.py`) → `tools/flash-iosroot.sh` (sha256 сверен). `research/ipsw-x/d22ap.adt` из `DeviceTree.d22ap.im4p` (LZFSE распакован `compression_tool` на Mac).
- Загрузка `rd=ufsroot0 ufsroot=iosroot ufsrw=1 rootrw=1`: корень монтируется, `exec launchd`, дальше стоп.
- Лог ядра пишется в последние 4100 КиБ раздела корня: снимать `dd skip=33553407 count=1025` из `iosroot` → `ios-topaz/tools/sweet-log.py` → `logs/`.
- Причина остановки (патч 0961 — сэмплер с состоянием потока в ядре): launchd в `mach_vm_allocate_trap` (dyld+0x11990, dyld по 0x102b18000), `state 0x4` (RUN), стек ядра `sleh_synchronous → arm_fast_fault → kernel_preempt_check` — вечная ошибка доступа при copyout. **PAN**: A55/A75 его имеют, ядро topaz (0081) PAN не снимает, а `SCTLR_EL1.SPAN=0` включает PAN при каждом входе в EL1. Исправление — патч 0962 (`SPAN=1` под `SWEET_AGM`).
- Сторожевые таймеры Unisoc (сбрасывают через ~5 мин): PMIC `sprd,sc2730-wdt` (через ADI `spi@64200000`) и AP `sprd,wdt-r2p0-fiq` @0x644e0000.
- **✅ 2026-10-26: 8/8 ядер** (0972 + shim, `logs/boot-0972b.txt`): прошивка не берёт точку входа выше 4 ГБ (rc -9), поэтому shim кладёт трамплин ниже 4 ГБ (`start.S xnu_secondary_tramp`, 0x800809c0; EL2→EL1h, прыжок на адрес из слота за 0x40 байт до него) и пишет `agm_tramp=` в boot-args; SweetCPU пишет в слот PA `sweet_secondary_entry` и будит CPU_ON на трамплин. Все 7 вторичных rc=0, «came alive; SGI1 OK». Выключение через «slide to power off» работает (PE_halt_restart 0966).
- (было) `cpus=8`: panic `PSCI CPU_ON rc=-9` (INVALID_ADDRESS) для cpu1 с `entry_pa=0x1014cc000` — вероятно, ATF Unisoc не принимает адрес входа выше 4 ГБ (shim будил ядра с адресом ниже 4 ГБ успешно).

## 🎉 iOS LOCK SCREEN на AGM H6 (2026-10-25, фото `../photo/photo_2026-10-25_18-35-24.jpg`)
- Ядро `xnu-out/agm-0963` (topaz e477291 + 0960–0963), shim с PMIC watchdog 2400 с, cmdline: `cpus=1 rd=ufsroot0 ufsroot=iosroot ufsrw=1 rootrw=1 ignition_level=1 unentitled_ios_sim_launch=1 launchd_unsecure_cache=1 wdt=-1 wdt_test=no_user_checkin sweetfb_native=1 sweetfb_hz=30 sweetfb_noconsole=1`; корень iOS 16.7.16 + `sweet-root-tweaks.py` + `tools/agm-root-tweaks.py`.
- Первая загрузка ждёт, пока `lsd` перестроит базу LaunchServices (SpringBoard и backboardd стоят в `_LSCopyServerStore`), на одном A55 это дольше 5 минут → нужен был watchdog 40 мин.
- Экран блокировки: время идёт (от эпохи, RTC нет), красная полоса `rdar:45025538`. Ввода нет: тач/кнопки topaz выключены на AGM (0960).
- Дальше: ввод (кнопки громкости/питания, тач Chipone ICNL9916 по SPI3), cpus=8, RTC.

## Кнопки (патч 0964, 2026-10-25, лог `logs/boot-keys1.txt`)
- `bsd/dev/agm_keys.c`: Vol Down = AP GPIO 124 (активный 0, банк 7 бит 12, INEN 0x28), Power = SC2730 EIC бит 1 (активный 0), Vol Up = EIC бит 4 (активная 1); опрос 20 мс → `sweet_hid_key()`. Плюс подкормка PMIC watchdog: перезарядка на 600 с каждые 60 с.
- Все три кнопки детектируются. iOS: Power гасит/будит экран (подсветка остаётся — нет управления backlight), долгое нажатие вызывает Siri; громкость iOS игнорирует (нет аудио-маршрута).
- Возврат в Android: держать Power 10–15 с (аппаратный сброс PMIC, bootcause "Reboot into normal").

## Тач (план)
- Chipone ICNL9916 TDDI на SPI3 `0x20150000` (`sprd,sc9860-spi`), irq GPIO 144, reset GPIO 145. Драйвер-справочник: Motorola `motorola-kernel-modules` → `drivers/input/touchscreen/chipone_tddi_mmi_v2` (TCS-протокол по SPI, ICNL9916 в `cts_device_hwdatas`), копия в `ref/moto-kernel-modules`.
- Шаги: проба из shim (такт SPI3 + чтение fwid/разрешения) → поток опроса в ядре → `sweet_hid_touch()`.

## Зарядка (патч 0967, 2026-10-25, в проверке)
- Зарядник ETA6937 (похож на bq24157) на I2C5 `0x20210000`, адрес 0x6a (живой FDT `fdt.dts`: `i2c@0x20210000/charger@6a`). Такт I2C5: гейт AP APB бит 26 (set-регистр `0x20001000`), mux/делитель `0x20010044` (0 = 26 МГц). Контроллер `sprd,sc9860-i2c` (вендорский `i2c-sprd.c`), в ядре опросом на 100 кГц.
- PMIC: `charger_pd` = 0x1b98 бит 0 (1 = заряд выключен), `charger_detect` = 0x1ba0 (syscon `reg = <0x1800 0x1ba0 0x1b98>`).
- Батарея (DT): 4900 мА·ч, max 4.40 В, term 60 мА; DCP 2 А, SDP/unknown 0.5 А.
- Настройка (как `eta6937_charger_hw_init`): REG6 safety 4.40 В/1.55 А, ICHG 1.15 А, VOREG 4.40 В, вход REG7 EN_ILIM_2 + 1.5 А, TE включено (у Linux окончание заряда делает charger-manager), REG4 бит 7 (RESET) никогда не пишется обратно. Watchdog микросхемы 32 с → TMR_RST каждые 10 с; если VOREG сбросился — настройка заново. STAT (REG0 биты 5:4) → SweetBattery (external/charging/full).
- Нет температурного контроля (JEITA) — пока не заряжать в жаре.

## GPU Mali-G57 (с 2026-10-25)
- Проба 0968 (`logs/boot-0968.txt`): `GPU_ID 0x90910010` (prod 0x9091 = G57 в Mesa `pan_props.c`; ядро panfrost матчит 0x9001 по маске), L2 0x7110206, TILER 0x809, MMU 0x2830 (VA 48 бит, PA 40), AS 0xff, JS 0x7, shader/tiler/l2 present = 1 (MP1), threads 0x400, coherency 0.
- До пробы: PMU force-shutdown уже снят (u-boot), домен включён (`+0x500` = 0x7000000), такты core/mem выключены, mux = 1 (384 МГц, DVFS индекс 0, 0.7 В).
- Зарядка 0967 проверена (`logs/boot-0967.txt`): регистры после настройки `80 c8 b4 54 61 04 5a 0c`, REG6 safety принят (0x5a), подключение/отключение видно (STAT 0↔1).
- **0969 самотест прошёл** (`logs/boot-0969.txt`): `AGM MALI TEST: job 1 done in 130 us: word 0xc0ffee42`. Первая версия паниковала: IOBMD с `kIOMemoryPhysicallyContiguous`/`kIOMapWriteCombineCache` даёт PA вне ОЗУ (0x4c70000 = настоящий PA с отрезанным старшим битом) → `phystokv_range` panic. Теперь вся память GPU — обычная кэшируемая + ручная чистка кэша (`sweet_dcache_clean/flush`), программам буферы отображаются WC.
- 0970: почтовый ящик `bsd/dev/agm_dropbox.c` + `tools/agm-drop.py`: файлы в корень iOS без перепрошивки образа (iosroot+32 ГиБ, заголовок AGMDROP1, FNV-1a, раз на поколение `/usr/lib/agm-drop.gen`, каталог должен существовать).
- **🎉 2026-10-26: iOS рисует на Mali-G57** (0971, `logs/boot-0971.txt`): Mesa panfrost «Mali-G57 (Panfrost)», OpenGL ES 3.1, 0 сбоев за 8200+ кадров, кадр ~14.5 мс (из них ~5.5 мс ожидание GPU). Прозрачность/размытие работают, местами артефакты.
  - Ловушки: `applog=1` ломает запуск не-root служб (backboardd не стартует, xpcproxy крутится) — не использовать, вывод backboardd идёт через `sweetnull` (по умолчанию). WC/contiguous IOBMD даёт PA с потерянным старшим битом (паника) даже с маской 40 бит → буферы кэшируемые + `dc civac` при каждой отправке (замапленные программой) и после WAIT_BO. Без этого задания падали с 0x58 DATA_INVALID.
  - Восстановление: ошибка MMU или задание > 2 с → hard stop + soft reset GPU.
- **0973 пул GPU / 0974 прямой показ через DPU (2026-10-26):** пул `topaz_gpu_pool` на AGM = `0xb8000000`–`0xdc000000` (ниже 4 ГБ, между oemcrypto и widevine; DPU берёт 32-битный адрес слоя). Буферы Mesa и экранные поверхности backboardd в пуле (WC, без чистки кэша: 10–40 мкс на отправку вместо мегабайт). DPU у qogirl6 = **r5p0** (не r6p0!): CTRL 0x04 (REG_UPDATE бит 2; RUN читается 0 в видеорежиме), CFG0 0x08, слои 0x30+0x30·i (адрес +0, CTRL +0x10 бит0, SIZE +0x14, PITCH +0x18 в пикселях), INT_CLR 0x1e4, INT_RAW 0x1ec (UPDATE_DONE бит 4, VSYNC бит 5), MMU_EN 0x804. u-boot: слой 0 → 0x9e000000, 720×1612. Прямой показ убрал разрывы. Ловушка: `showDirect` из topaz трогал регистры MDP (NULL на AGM) → паника по адресу 0x18.
- После 0974: кадр 6–40 мс, из них 5–21 мс ожидание GPU → GPU — узкое место. 0975: DVFS GPU индекс 2 (614.4 МГц) вместо 0 (384).
- **Производительность (2026-10-26, 0975–0979):** замеры (0976): A55 ~1530 МГц, A75 ~1230 МГц (частота CPU не узкое место; SIP DVFS прошивки `0x8200060x` не инициализирован, FREQ_GET = -1). GPU DVFS-индекс через `0x23014000+0x8c` не применяется (state 0) → частота GPU напрямую мультиплексором `agm_gpu_mux=2` = 512 МГц (измерено счётчиком тактов Mali, 0978), стабильно. Профилировщик в backboardd (`agmprof=1`, `gpu/gl/agm_inject.c`; ловушка: нельзя звать dladdr при остановленном потоке — deadlock на локе dyld) показал: ~40% главного потока CA в ядре — drmIoctl, ожидания GPU, map/unmap BO. Исправлено: Mesa на Darwin держит BO отображёнными в кэше (`pan_bo.c`), прерывание Mali (SPI 60 = INTID 92, GICD, хук `agm_irq_hook` в `sleh_irq`, 0979) вместо опроса. Кадры 5–24 мс (было 12–54). Остаток: drmIoctl ~20–25% (submit/wait ~3000 вызовов/с).
- **Подсветка (0980, 2026-10-26):** ШИМ `0x643f0000` канал 2 (+0x40: PRESCALE, MOD=255, DUTY, ENABLE +0x18; u-boot: prescale 32771, duty 255). Экран выкл/вкл (Power) → duty 0 / прежняя — работает. Сервис `AppleARMBacklight` (backlight-control) по умолчанию ВЫКЛ (`sweetbl=1` включает): с ним CoreBrightness занимает 42% главного потока backboardd (HID), UI падает до кадра в несколько секунд (замер `agmprof=2`, ожидания потоков). Ползунок яркости и автоблокировка пока не работают — нужен разбор, чего CoreBrightness ждёт от подсветки.
- Userland (сборка на Mac): `gpu/mk-mesa-agm.sh` (Mesa 25.0.7 softpipe+panfrost, `gpu/mesa-agm.patch`, прослойка `gpu/agm-drm`), `gpu/gl/build-gl.sh` (eagl.c topaz + mkeagl/eagl_slots HackiOS → `libagm-gl.dylib`, `libagm-inject.dylib`), ad-hoc подпись `codesign -s -`. Всё в `/usr/lib/libagm-*.dylib`; cmdline `agm_gpu=1 applog=1 topaz_inject=/usr/lib/libagm-inject.dylib,backboardd,containermanagerd,containermanagerd_system`.
- 0969: `iokit/Kernel/arm/AgmMali.cpp` — железо Mali (питание, сброс, AS0 AArch64 4K, очередь заданий с опросом), самотест WRITE_VALUE по `agm_gpu_test=1`. Исходники: `ref/realme-ums9230` (vendor platform qogirl6), `ref/ums9230-mainline/drivers/gpu/drm/panfrost`, Mesa 25.0.7 в `gpu/mesa-src`.

## Сторожевой таймер PMIC (2026-10-25)
- Сброс через 5 мин делает watchdog в PMIC SC2730 (`sprd,sc2730-wdt` @0x40): загрузчик ставит `ctrl=0xA` (RUN|RST_EN), `load=0x960000` (300 с @32768 Гц). На Android его забирает CM4 по команде Linux через SIPC (`sprd_pmic_wdt.c`: "watchdog on"/"watchdog rstoff"); XNU этого не делает.
- AP-таймер `0x644e0000` (`sprd,wdt-r2p0-fiq`) выключен по тактированию: чтение даёт external abort (ESR 0x96000010).
- Доступ к PMIC: ADI `0x64200000` (вариант sharkl5pro: окно `+0x20000`, RD_CMD `+0x28`, RD_DATA `+0x2c`, FIFO `+0x30`) — по вендорскому `spi-sprd-adi.c` (Motorola kernel-sprd, ветка android-13-release-tla33). Код: `shim-agm/pmic.c`.
- Shim перезаряжает таймер (`pmic_wdt_extend`, ключ 0xe551, LOAD_HIGH/LOW); проверено: 600 с → возврат в Android через 636 с. Для загрузок iOS стоит 2400 с (40 мин). Таймер остаётся включённым — зависший телефон сам вернётся на A.

## Цикл «один запуск слота B» (с 2026-10-25, без fastboot)
- `tools/run-b-once.sh <boot_b.img>` из Android (слот A, Magisk root): dd в boot_b + проверка sha256, затем `tools/abctl.py once-b` правит `misc`+2048 (AOSP `bootloader_control`, магия BCAB, CRC32), перезагрузка.
- **Особенность этого u-boot:** откат на другой слот, когда у неуспешного слота остаётся **1** попытка (а не 0, как в AOSP). Поэтому B ставится с `tries=2` → ровно одна загрузка B. `fastboot set_active b` даёт 7 → шесть загрузок B; `tries=1` → B не грузится вообще.
- Проверено: shim запустился, после сброса кнопкой телефон сам вернулся на A; misc снова a=15/успешный, b=14/tries=1.
- Перезагрузка через PSCI SYSTEM_RESET на этом телефоне не работает (или виснет); примерно через 5 мин срабатывает аппаратный watchdog, и это не тратит попытку слота.
- Исходный блок misc: `backup/misc-initial.bin`.

## Shim stage 0 (`shim-agm/`, topaz shim e477291 + правки под AGM; фото `../IMG_20261023_193854.jpg`)
Сборка: `shim-agm/build-agm.sh` (Docker `hackios-tc`) → `out/shim-boot_b.img`. Вход на EL2 → `el2_to_el1` в start.S (HCR.RW, CNTHCTL, CPTR, ICC_SRE_EL2.SRE|Enable).
- CPU0: MIDR 0x412FD050 (A55 r2p0), **MPIDR 0x81000000 (MT=1, ядро в Aff1)**, MMFR0 0x00101122.
- PSCI CPU_ON 0x100..0x700 → все ret=0. CPU1–5 A55 (412FD050), CPU6–7 A75 (413FD0A1, r3p1). **У всех 8 ядер tg16=1.**
- PSCI 1.0, SMCCC_VERSION не поддерживается (0xFFFFFFFF → SMCCC 1.0).
- u-boot передаёт initrd: /chosen 0xA0000000–0xA1743B62 (~24 МБ) → payload можно класть в ramdisk (у topaz ABL этого не было).
- GIC: GICD_CTLR 0x10 → 0x12, GICR CPU0 @0x10040000 (waker=0), IGROUPR0 читается 0 и запись игнорируется, но SGI1 доходит 10/10.
- Таймеры: vtimer INTID 27 работает (30 IRQ за 300 мс). **INTID 30 (CNTP) постоянно в pending — bootloader оставил физический таймер включённым**; перед XNU выключить `cntp_ctl_el0`.

## UFS из shim (2026-10-23, фото `../IMG_20261023_210111.jpg`, `shim-agm` + topaz `ufs.c` @0x20200000, только чтение)
- UFSHCI **2.1** (ver 0x210), cap 0x0187011F (32 слота). u-boot оставляет контроллер поднятым: HCE=1, HCS=0x10F; его UTRL лежал по 0x9F6B0C00.
- Наш UTRL (0x80096000) принят, TUR / READ CAPACITY / READ(10) — OK. LUN0: lastlba 0x3B95FFF, блок 4096 (~256 ГБ), GPT: prodnv, boot_a/b, vendor_boot_a/b, super, cache (f2fs), blackbox, metadata, userdata (~231 ГиБ).
- Вывод: подход `sweet_ufs.c` (подхватить контроллер после загрузчика) на Unisoc работает.

## Клавиши в загрузчике (разбор uboot_a.img, `re/xref.py`)
- Образ: заголовок DHTB 0x200, загружается по 0x9f000000, AArch64. Это не mainline U-Boot, а Unisoc "sprdboot".
- `get_mode_from_keypad` @0x9f0055e0. Опрашиваются только Vol Up (0x73) и Vol Down (0x72), код = их сумма.
- Таблица @0x9f0d10f8 (режим, клавиша1, клавиша2): `0xe5 → USB_MUX (18)`, `0x72 → RECOVERY (4)`, `0x66 (HOME) → FASTBOOT (5)`.
  Кнопка HOME никогда не опрашивается, поэтому **fastboot клавишами недоступен**. Vol Down + Power ведёт в recovery, Vol Up + Vol Down + Power переключает USB на UART/JTAG (USB_MUX).
- Команды fastboot: getvar, download, flash, erase, powerdown, continue, reboot-bootloader/-fastboot/-recovery, set_active, flashing, oem…; **команды `boot` нет**.

## Бэкап
`backup/` — 23 раздела, сверены по `backup/SHA256SUMS` с хешами на телефоне.
Снимать только через `dd` в /data/local/tmp + `adb pull`: `adb exec-out su -c cat` у Magisk портит бинарники (LF→CRLF).

## USB (2026-10-28, патч 0988, `xnu-agm/agm_usb.c`, boot-arg `agm_usb=1`)
- **✅ Работает**: CDC-ECM (USB Ethernet), high speed, телефон `ue0` 192.168.7.2, ПК 192.168.7.1 (профиль NM `agm-usb` по MAC 02:00:5E:77:EE:01). Ping до 8000 Б без потерь, ~3.8 МБ/с в каждую сторону. Лог `xnu-out/agm-0988g`, `logs/usb-0988f.txt`.
- **Живой лог консоли**: UDP broadcast на 192.168.7.255:5140 (без ARP), на ПК `tools/agm-usbnet.sh logs/x.txt` (socat). `agm_usblog=0` выключает. Лог очень объёмный из-за журнала прошивки WCN.
- **Удалённая перезагрузка**: `echo reboot | socat - UDP:192.168.7.2:5141` → PE_halt_restart → загрузчик возвращает слот A. Цикл прошивки без рук.
- Железо: MUSB @0x64900000 (плоские регистры EP 0x100+0x10n, FIFO 0x20+4n), такт AON EB1 бит 8; PHY: AON 0x64000000 (CGM_REG1 0x138 биты 12/10, RST1 0x10 биты 9/8, OTG_PHY_TEST 0x204 бит 24, OTG_PHY_CTRL 0x208 биты 30/3), аналог 0x64570000 (по `phy-sprd-qogirl6.c`), LDO VDDUSB33 (pd 0x19e8, vol 0x19ec = 210 → 3.3 В). VBUS форсируется валидным.
- **Грабли**:
  - PIO-чтение FIFO OUT-endpoint'а даёт нули (правильная длина; EP0 читается нормально). Вендор всегда использует свой DMA → bulk OUT через DMA-канал ep+15 (16), один узел V3 16 Б {addr, 32 | blk_len<<16, list_end|ioc|addr[35:32]<<8, 0}, RXCSR = 0xa800, готовность — CHN_INTR бит 18, длина = blk_len − CHN_LEN[31:16]. Канал останавливается на коротком пакете.
  - PIO-запись в двойной буфер IN FIFO портит многопакетные кадры (ICMP csum errors на ПК) → EP1 TX одинарный буфер (TX_DPKTBUFDIS бит 1) и загрузка только при пустом FIFO (TXPKTRDY|FIFONOTEMPTY = 0).
- Сборка ядра AGM только с `SWEET_AGM=1 tools/build-local.sh` (без флага AGM-код не компилируется, ошибки нет).

## Шелл и стоковые приложения (2026-10-28)
- Root-шелл: `agm-shelld` (TCP 2323, pty, toybox sh из `shell/build-shell.sh`, ad-hoc подпись с platform-application), задание launchd `com.agm.shelld`; на ПК `tools/agm-sh.sh`. В образ ставится `tools/agm-root-tweaks.py ROOT shell/out`.
- 32 удаляемых приложения (Notes, Calculator, Mail, Calendar, Weather, Files, Music…) лежат в `/private/var/staged_system_apps`; в `/System/Library/AppPlaceholders` — только заглушки. Ставит их миграция (`SystemAppMigrator`) при первой загрузке в `/var/containers/Bundle/Application`.
- topaz глушит миграцию тремя файлами в `/var/mobile/Library/Preferences`: `com.apple.migration.plist` (LastSystemVersion, sweet-root-tweaks.py), `ByHost/com.apple.migration.<UUID>.plist` и `com.apple.DataMigration.plist` (agm-root-tweaks.py). Без всех трёх (+ `com.apple.migrationpluginwrapper.plist`) на 8 ядрах миграция за несколько минут ставит все 32 приложения и пишет своё состояние. Без первого файла `datamigrator` стартует и сразу выходит.
- Запасной путь: скопировать `.app` из staged в `/Applications` и убрать базу LaunchServices (`…/InternalDaemon/<UUID>/Library/Caches/*LaunchServices*.csstore`) — тоже работает (`--apps=copy`).
- Перезагрузка по USB до 0990 (`PE_halt_restart`) теряла несохранённые изменения HFS: после правок корня ждать ≥60 с или перезагружаться ядром 0990+ (`reboot_kernel`).

## Нагрев, «Фото», экран iPhone X (2026-10-28)
- Нагрев — зацикленные процессы, не частоты. Замер `tools/agm-top.sh` (прирост CPU-времени): `UserEventAgent` → launchd ищет `com.apple.commcenter.xpc` ~38 000 раз/с (BackgroundTaskAgent plug-in) — launchd 76%; `mediaserverd` переподключается к bluetoothd (BTAudioHALPlugin) — 48%; vmd/syncdefaultsd/wifip2pd; обои «Астрономия» (AegirPoster, Metal) падают по кругу → ReportCrash 50%. Всё, кроме обоев, выключает `agm-root-tweaks.py`.
- «Фото»: `assetsd` падал «requires TCCPhotosService access» — tccd не мог выдать sandbox extension (заглушка sandbox отвечала ENOENT). 0991: op 5 пишет токен, op 6 возвращает дескриптор.
- Журнал iOS: `/var/db/diagnostics` + `uuidtext` (tar | nc с телефона), `tools/mktimesync.py` если пуст timesync, парсер mandiant macos-UnifiedLogs `unifiedlog_iterator -m log-archive`.
- **Экран 750×1624 = 375×812 pt @2x (размер iPhone X в точках)**, по умолчанию с 0992 (без `sweetfb_native=1`). С 720×1612 (360×806 pt, такого iPhone нет) не работала кнопка «123» клавиатуры и вместо строки состояния была красная полоса rdar://45025538.
  - ядро: режим 750×1624, на панели окно 720×1612 с (12, 6); DPU берёт его через CROP_START (+0x2c слоя, `y<<16|x`) от начала поверхности; тач пересчитывается так же (SweetHID.cpp);
  - ADT `disp0` canvas-width/height должны совпадать с режимом (`mkadt.py --agm` = 750×1624, `--agm --native` = 720×1612): иначе CoreAnimation рисует в холст и ждёт аппаратный скейлер (`AppleM2ScalerCSCDriver`), которого нет — чёрный экран при живой системе (снимок кадра: boot-arg `agm_fbdump=N`, 0993);
  - строка состояния: UIKit выбирает провайдер по ширине/масштабу/радиусу углов, для 375 pt @2x подходящего нет; переопределение — default `UIStatusBarVisualProviderClassName = _UIStatusBarVisualProvider_Split1125` в `.GlobalPreferences.plist` пользователя mobile (agm-root-tweaks.py).

## Wi-Fi: скорость (2026-10-28)
- Было ~2 Мбит/с в Safari: при занятых 64 TX-буферах `net_output` выбрасывал кадры, TCP откатывался таймаутами. 0994: ждать свободный буфер до 100 мс. Замер `nc` по Wi-Fi: приём 84 Мбит/с, передача 47 Мбит/с (toybox `nc` в роли клиента зависает на 135168 байтах — баг nc, не сети; мерить с телефоном-сервером).
- На простое RTT до 2 с — энергосбережение прошивки; под нагрузкой 1–3 мс.
