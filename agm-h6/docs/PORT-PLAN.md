# План порта XNU (ios-topaz e477291) на AGM H6

Основа — ios-topaz: 204 патча xnu-8796, `tools/mkadt.py` (ADT), `tools/mkpayload.py`, shim.
Большая часть патчей — драйверы и userland topaz (тач FT5452, модем, Wi-Fi, Adreno, звук, MDP).
Для первой вехи («XNU стартует и пишет в консоль на экране») важны только платформенные.

## Сборка ядра
- Только macOS: Xcode 15.4 (`/Applications/Xcode_15.4.app`), KDK 13.5, Homebrew bash, ccache;
  `tools/build-local.sh` (blacktop/darwin-xnu-build @6639065, `MACHINE_CONFIG=SWEET`).
- Вариант без Mac — свой форк + `.github/workflows/xnu.yml` на macOS-раннерах GitHub.
- `TOPAZ_4K=1` (патч 0140) включает 4K-страницы — для AGM **не нужен** (у всех 8 ядер 16K).
- Флаги сборки: `-mcpu=cortex-a53` (из-за ARMv8.0 у topaz). Для AGM можно оставить на первом шаге.

## Что поменять для AGM

### ADT (`tools/mkadt.py`)
| topaz | AGM H6 |
|---|---|
| `PHYS_BASE 0xb0000000`, `MEM_SIZE 0x4d000000`; варианты `--highmem/--midmem` | RAM 0x80000000 + 8 ГБ; свободные куски между reserved-memory (см. fdt.dts). Первый кандидат: **0x100000000 – 0x27ffff000** (выше 4 ГБ резервов нет, кроме cma 0xf0000000 ниже) |
| GICD 0x0f200000, GICR 0x0f300000 | GICD **0x10000000**, GICR **0x10040000** (stride 0x20000) |
| FB 0x5c000000, 1080×2400 | FB **0x9e000000**, **720×1612**, stride 2880 |
| UFS 0x04804000 (`qcom,ufshc`) | UFS **0x20200000** |
| ядра: Kryo, 4 gold | 8 ядер: MPIDR 0x000–0x700 (**MT=1, Aff1**), A55×6 + A75×2 |
| compatible `xiaomi,topaz`, `qcom,khaje` | свои (`agm,h6`, `sprd,ums9230`) |

### Ядро (патчи)
- `SWEET_VTIMER_INTID 19` → **27** (0017/SWEET.h); ptimer **30** — и выключать CNTP в shim (bootloader оставил его тикать).
- Ранняя консоль 0142: `0x5c000000` → `0x9e000000`, размеры экрана.
- `SWEET_SCANOUT_BASE 0xb0000000` — **на AGM это SML/TrustOS (reserved)**, нужен другой регион.
- Экран: iPhone X canvas 1125×2436 больше панели 720×1612 → масштабирование/кроп (MDP-скан-аут topaz — Qualcomm-only; на старте копировать в фреймбуфер u-boot).
- SMP: 0065/0078 (PSCI CPU_ON, 8 ядер) — проверить, как берётся MPIDR (у topaz MT=0, ядра в Aff0 у silver).
- 0081 (ARMv8.0: без LSE/PAN) — на AGM не обязателен, но безвреден.
- 0082 (boot on gold) — убрать: на AGM грузимся с CPU0.
- 0080/0141 (топология/AMP-кластеры) — переписать: кластер A55 (0–5) + A75 (6–7).
- UFS (0028/0030/0034/0069 `sweet_ufs.c`): стандартный UFSHCI, контроллер **не инициализирует**, подхватывает поднятый загрузчиком → проверить пробой shim (`shim-agm`, test_ufs, только чтение).
- Выкинуть/отключить: тач FT5452, батарея/зарядка, модем, Wi-Fi, звук, Adreno, DSI/подсветка, cpufreq.

### Shim / payload
- Payload можно класть в ramdisk: u-boot передаёт initrd (0xA0000000–0xA1743B62); `loader.c` его уже сканирует.
- Перед прыжком в XNU: CNTP off, GICR всех 8 ядер подготовить (как `gold_gicr_prepare`).

## Порядок
1. Shim: проба UFS (готова, `shim-agm/out/shim-boot_b.img`).
2. Mac: Xcode 15.4 + KDK 13.5, собрать **неизменённый** topaz-ядро (проверка тулчейна).
3. Ветка `agm`: ADT + VTIMER + early console + отключение topaz-драйверов → первая загрузка до консоли.
4. UFS root, 8 ядер, экран.

## GPU Mali-G57 (план от 2026-10-25)
Как у topaz: CoreAnimation → OpenGL ES (eagl.c, OSMesa) → Mesa → «libdrm» (`userland/mesa/redwood-drm`: DRM-ioctl = вызовы IOKit user client) → драйвер в ядре. У topaz это Adreno (freedreno, UAPI msm, `RedwoodGPU.cpp` + `TopazAdreno.cpp`). Для Mali-G57 (Valhall v9) в Mesa есть **panfrost** (сертифицированный GLES 3.1 с Mesa 22.2).
1. **Проба (0968, boot-arg `agm_gpu_probe=1`)**: питание и такты по вендорскому `platform/qogirl6/mali_kbase_config_qogirl6.c` (Realme C53, `ref/realme-ums9230`), чтение GPU_ID (G57 ≈ 0x9093xxxx), present-масок.
   - AON APB `0x64000000` бит 11 (gpu_eb, set `+0x1000`); PMU `0x64020000+0x2c4` бит 25 = force shutdown (снять), `+0x500` биты 20:16 = 0 → включено; GPU APB `0x23000000`: `+0x0` soft reset (биты 0/1), `+0x4` core / `+0x8` mem (gate бит 0, mux 6:4: 26M/384M/512M/614.4M/768M/GPLL, div 10:8), `+0xc` sys. Напряжение — аппаратный top-DVFS (`0x64014000`), индекс в GPU DVFS APB `0x23014000`. IRQ: SPI 60 (JOB/MMU/GPU общий). `system-coherency = 31` → некогерентно, нужна явная работа с кэшем.
2. **Ядро**: драйвер Mali (по образцу Linux `drm/panfrost`): soft reset, включение L2/tiler/shader, MMU AS (таблицы страниц), job slots, ожидание (опрос или IRQ 60). Наружу — UAPI panfrost (GET_PARAM, CREATE_BO, MMAP_BO, SUBMIT, WAIT_BO, GET_BO_OFFSET, MADVISE) через тот же путь IOKit, что `RedwoodGPU`.
3. **Mesa 25.0.7** с `-Dgallium-drivers=softpipe,panfrost` (сборка `userland/mesa/mk-mesa-mac.sh`), redwood-drm отвечает «panfrost», syncobj — синхронно; правки сборки под Darwin как `freedreno-darwin.patch`.
4. Подключение к CoreAnimation как у topaz (eagl.c / inject), поверхности IOSurface ↔ BO.

## Красная полоса `rdar:45025538`
UIKit не находит «status bar visual provider» для размера экрана: при `sweetfb_native=1` экран 720×1612 (в точках при scale 3 — 240×537, такого iPhone нет). topaz (0061) отдаёт 1125×2436 (iPhone X) и показывает центр 1080×2400. Для AGM варианты: 1125×2436 со сжатием ×0.64 при выводе (дорого на CPU, дёшево на GPU) или 750×1624 при scale 2 (= 375×812 pt, как iPhone X) с обрезкой 15 px по бокам.

## Wi-Fi (план от 2026-10-26)
Встроенный WCN (Marlin3 integrated, `unisoc,integrate_marlin`, DT `wcn_btwf`): прошивка (раздела `wcnmodem` на этом телефоне нет — берётся файл `/odm/firmware/wcnmodem.bin`, копия в `wcn/`) → DDR `0x87000000` (2 МБ, sync `+0x7fdc00`), Wi-Fi sc2355 поверх SIPC (`sprd,sc2355-sipc-wifi`, общая память `0x87380000`). Исходники (Realme C53, `ref/realme-ums9230`): `drivers/unisoc_platform/sprdwcn/boot/wcn_integrate*.c` (запуск: `wcn_proc_native_start` = clean ddr flag → download image → pre_boot (cali) → `wcn_poweron_device` → `wcn_wait_marlin_boot` → `wcn_marlin_boot_finish` → `wcn_firmware_init`), `drivers/soc/sprd/modem/sipc/` (smsg/sbuf/sblock), `kernel_modules/kernel5.4/wcn/wlan/wlan_combo/merlion` (драйвер sc2355 для встроенного варианта).
iOS-сторона готова в topaz (патч 0900): `bsd/sys/redwood_sta.h` — граница «станции» (`redwood_sta_ops`: power, scan, heard, join, leave, state, channels, address; события SCAN_DONE/JOIN_DONE/LINK), кадры через `redwood_net.h`. Нужно написать backend под Unisoc.
Шаги: 1) запуск сопроцессора (лог: CP boot ok); 2) SIPC (smsg + sbuf/sblock, mailbox-прерывания); 3) команды sc2355: MAC, scan → список сетей в логе; 4) WPA2 (если не делает прошивка — 4-way в ядре); 5) кадры + привязка к redwood_sta/net → Настройки, Safari.
Отложено: строка состояния (iOS считает экран iPhone X 1125×2436 и сжимает в 720×1559 — DPU-масштабатор или GPU), 90 Гц, удешевление вызовов драйвера GPU, сервис подсветки (CoreBrightness), автоблокировка, USB.

### Шаг 1 — запуск CP (0981, `bsd/dev/agm_wcn.c`, boot-arg `agm_wcn=1`)
- Прошивка `wcn/wcnmodem.bin` (1 238 920 Б, обычный образ Cortex-M без подписи BTHD: SP 0x1e0008, PC 0x3029) кладётся через dropbox в `/usr/lib/agm-wcnmodem.bin`; ядро копирует её в DDR 0x87000000 (XNU берёт RAM только выше 4 ГБ, так что эта область свободна).
- Общие слова на +0x7fdc00: init_status (0x5a5a5a5a → CP пишет 0xf0f0f0ff → AP 0x13579bdf), efuse +0x20 (кэш eFuse 0x65014c24, затем 0x22222222/0x33333333), калибровка +0x2c (нули), флаг +0xf7c (0xefeffefe → 0x12345678).
- Питание: LDO SC2730 (база 0x1800, PD бит0): VDDSIM2 (=dcxo1v8) 1.8 В, VDDWCN 1.2 В, VDDWIFIPA 3.3 В (как в Android); RF-чип Merlion: GPIO 118 enable, GPIO 117 reset.
- PMU 0x64020000: 0x3a8 (задержки 0x0204<<8, сброс бит24, бит25), опрос 0x538[28:24]=0; 0x818 бит7 сброс, опрос 0x860[31:28]=6. Затем WCN AON APB 0x5180c000 (PLL 0x3c4/8/c, 0x19c, 0x168, 0x90 бит30, 0x98 биты 3/12/2), WCN AON AHB 0x51880000 (0x0c |=0x55 сброс CPU, 0x4c = 0x7fdc00, 0xc4), BTWF AHB 0x51130000 0x410 |= 3<<24, AON 0x354 clr. Регистры WCN трогаются только после подтверждения питания (иначе висит шина).
- 4-way WPA2 в Android делает wpa_supplicant на хосте (ключи ставятся командой WIFI_CMD_KEY) — значит для iOS шаг 4 понадобится.
- **✅ 2026-10-26 шаг 1 работает** (`logs/boot-0981e.txt`, ядро `xnu-out/agm-0981`, `agm_wcn=1`): CP готов через ~550 мс (init_status f0f0f0f1 → калибровка a2…a6 → f0f0f0ff), затем сам уходит в глубокий сон (PMU 0x860 = 0x6, cp2_sleep "DSLP").
  - Обязательно: **такт мейлбокса** AON APB 0x04 бит 2 (`sprd,mailbox_clk`, мейлбокс 0x641c0000) — без него прошивка падает HardFault в инициализации MBOX (`WCN Assert … nvic_fault.c … Hfault!(PC-101d34 … CFSR-00008200)` пишется в начало DDR 0x87000000).
  - **Нельзя читать регистры WCN (0x51xxxxxx) после запуска CP**: WCN засыпает → внешний abort AP (паника ESR 0x96000010).
  - Одно отображение 8 МБ через ml_io_map_wcomb не влезает (kmem_alloc ret 3 → паника): маппим 2 МБ образа + 16 КБ хвоста.
  - Отладка: `agm_wcn=5` — ждать 20 с и снять память CP в iosroot+33 ГиБ (`sweet_ufs_dump`), из Android: `dd if=/dev/block/by-name/iosroot bs=4096 skip=8650752 count=2048`.
- Шаг 2 (дальше): AP-сторона мейлбокса (unisoc-mailbox-phy-r2.c, inbox 0x641c0000 / outbox 0x641d0000 / common 0x641e0000, IRQ SPI 0x52/0x53/0x54, канал WCN = mbox id 8) + SIPC smsg (core@3, smem 0x87240000 размер 0x140000 → в адресах CP 0x240000) → sbuf/sblock каналы sc2355.
- **✅ 2026-10-26 шаг 2 (0982)**: SIPC работает. smsg идут прямо через мейлбокс (AP inbox 0x641c0000 / outbox 0x641d0000, ядро WCN = id 8, опрос), каналы в smem 0x87240000 (CP видит 0x240000). CP сам открывает каналы 4, 5, 7 после старта; 8/9 — при WIFI_CMD_OPEN. Журнал прошивки (sbuf 5) печатается в консоль как `AGM WCN log:`.
- **✅ 2026-10-26 шаг 3 (0983)**: SYNC_VERSION → DOWNLOAD_INI (4 секции из `wcn/wifi_board_config.ini`, генератор `wcn/inigen` = вендорский парсер rf_marlin3.c; секция 3 отвечает status 1 — это не ошибка) → GET_INFO (chip 0x2355, fw 47516; **MAC прошивка не знает**: берём prodnv `wifimac.txt` = boot-arg `agm_wifimac=<12 hex digits>`) → OPEN station (нужны sblock-каналы данных 8: 64×1664/256×1664 и 9: 64×1664/8×1664, иначе CP висит) → SCAN (биты каналов 1–13 + список 5 ГГц) → события WIFI_EVENT_MGMT_FRAME (0x83: {type, channel, s8 signal, rsv, bssid[6], le16 len, кадр}) и SCAN_DONE (0x82). Лог `logs/boot-0983c.txt`: видны сети пользователя на 2.4 и 5 ГГц.
- Дальше: шаг 4 — подключение (WIFI_CMD_CONNECT + ключи WIFI_CMD_KEY, 4-way handshake WPA2 делает хост: EAPOL через канал данных), шаг 5 — данные (sblock 8/9) + связка с iOS (`redwood_sta_ops`/`redwood_net`, wifid).
- **0985 (2026-10-27, лог `logs/boot-0985b.txt`)**: ассоциация с WPA2-сетью работает (SET_IE с RSN + CONNECT → STA_LUT 6 → CONNECT event), но **прошивка 4-way не делает**, даже с PMK в CONNECT: «start protection period for eapol», message 1 приходит хосту на канал 9, точка повторяет его 4 раза и рвёт связь (~7 с). На Android так же: psk_len 0, 4-way делает wpa_supplicant, кадры 802.1X идут `WIFI_CMD_TX_DATA` (72), ключи — `WIFI_CMD_KEY` ADD (pairwise idx 0, затем group idx из GTK KDE).
  - RX на канале 9 — «special data»: первое слово rx_msdu_desc {host_type:4 = 3, ctx:4, msdu_offset:8, msdu_len:16}, 28 байт дескриптора, затем 802.11-заголовок; сам кадр — Ethernet (DA, SA, 88 8e) по msdu_offset (54), длина msdu_len.
  - TX `WIFI_CMD_TX_DATA`: {5 байт адреса (бит 39), tx_msdu_dscr 11 байт: common (type 0, interface=ctx<<5), offset 11, tx_ctrl 0, le16 pkt_len, buffer_info 0, sta_lut_index, 0…; Ethernet-кадр}.
  - WIFI_CMD_KEY ADD: {sub 3; key_index, pairwise, mac[6], keyseq[16], cipher (4 CCMP / 3 TKIP), key_len, key}; group: mac нули, keyseq = RSC из message 3.
- **0986**: 4-way в ядре (`agm_wifi.c`, порт topaz 0129: PRF-SHA1, MIC HMAC-SHA1, AES unwrap GTK; свой поток `hs_thread`, т.к. ответы на команды читает поток SIPC), join ждёт ключи (EAUTH при неверном пароле — точка шлёт message 1 >4 раз), group rekey. Проверено в симуляции против независимой точки на Python (msg2/msg4 MIC, байты ключей). **✅ на железе 2026-10-27** (`logs/boot-0986.txt`): msg1→2, msg3→4 с первой попытки, ключи поставлены, связь держится; событие 0xf9 = WFD_MIB_CNT (счётчики, игнорируем).
- **0987 путь данных** (вендорский SIPC-вариант, `sipc_txrx_mm.c`/`mm.c`/`if_sc2355.c`): кадры в общей памяти 0x87380000 (TX 64×1792 с начала, RX 256×1792 с +0x1b5800), по каналу 8 ходят только 5-байтные адреса (CP видит 0x80000000 | pa & 0xffffff).
  - RX-буферы прошивке (после OPEN, по ≤52): {0x0c (type 4, reserv 1), 0, num, адреса}. Заполненные: {type 4 + reserv; le32 time; le16 seq; tlv_num; tlv {type 0, num, адреса}}; буфер = rx_mh_desc 28 + rx_msdu_desc, Ethernet по msdu_offset от начала буфера. Возвращаем в `agm_wifi_service()` (цикл SIPC).
  - TX: буфер {свой адрес 5 байт, tx_msdu_dscr type 2, кадр}, сообщение {type 4 | buffer_type<<4 | ctx<<5, 7, 0x80, le16 1, 0, адрес}; «готово» — type 4 без reserv, адреса +0x10. Нет BA-переупорядочивания и ADDBA с нашей стороны (пока).
  - Линк en0 поднимается после join (и ключей) → DHCP.
