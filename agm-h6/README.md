# iOS 16 (XNU) на AGM H6

Порт ios-topaz на AGM H6 (Unisoc T606 / UMS9230, SC2730, 8 ГБ RAM, UFS). Ядро — xnu-8796 topaz +
патчи `xnu/patches/0960-*.patch` … `0990-*.patch` (под `SWEET_AGM=1`), корень — iOS 16.7.16 (20H392) из
IPSW iPhone X (iPhone10,3/10,6). Загружается через свой shim вместо ядра Android в `boot_b`;
Android на слоте A остаётся, любой сброс возвращает в него.

Что работает: 8 ядер, экран (DPU), GPU Mali-G57 (Mesa panfrost), тач, кнопки, зарядка/батарея,
UFS (корень на разделе `iosroot`), Wi-Fi (сканирование, WPA2), USB (CDC-ECM: сеть, живой лог,
root-шелл, удалённая перезагрузка). Подробности и грабли — `docs/NOTES.md`, план — `docs/PORT-PLAN.md`.

## Что нужно

- Телефон: AGM H6, разблокированный загрузчик, Android на слоте A с Magisk (root через `adb shell su`).
  Загрузчик и NV-разделы (spl, uboot, sml, trustos, vbmeta, prodnv, l_fixnv*) скрипты не трогают.
- ПК с Linux: adb, docker, python3, socat, apfs-fuse, hfsprogs, rsync, sudo.
- Mac (Apple Silicon) для ядра и userland: Xcode 15.4 (ядро) + Xcode с iPhoneOS SDK, KDK 13.5,
  Homebrew (bash, ccache, gnu-sed) — как в `tools/build-local.sh` topaz. Доступ по ssh с ключом:
  `export MAC=user@host`; на Mac — клон этого репозитория в `~/ios-topaz` (ветка `agm-h6`).
- IPSW: `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw` с сайта Apple.

## Установка с нуля

Все команды — из каталога `agm-h6/`, телефон в Android по USB.

```sh
docker build -t hackios-tc docker/          # тулчейн для shim (aarch64-linux-gnu)
tools/agm-backup.sh                         # 1. бэкап разделов и GPT в backup/ (обязательно)
tools/agm-repartition.sh                    # 2. раздел iosroot 128 ГиБ из конца userdata
                                            #    (СТИРАЕТ данные Android: потом recovery -> wipe data)
shell/build-shell.sh                        # 3. toybox + agm-shelld (на Mac)
AGM_BOOTSTRAP=1 tools/agm-mkroot.sh IPSW ios-root.img
                                            # 4. корень iOS из IPSW (sudo), с шеллом; AGM_BOOTSTRAP=1 —
                                            #    ещё rootless Procursus + Sileo (ставятся при 1-й загрузке)
tools/agm-install.sh ios-root.img           # 5. записать корень в iosroot (+ MAC Wi-Fi в wifimac.local)
tools/agm-build.sh agm                      # 6. ядро (Mac) + ADT + payload + shim -> xnu-out/agm/
tools/agm-run.sh xnu-out/agm/shim-boot_b.img   # 7. загрузить iOS один раз и смотреть лог по USB
```

Один раз на ПК — сеть к телефону (NetworkManager, интерфейс с MAC 02:00:5E:77:EE:01):

```sh
sudo nmcli con add type ethernet con-name agm-usb ifname '*' ethernet.mac-address 02:00:5E:77:EE:01 \
     ipv4.method manual ipv4.addresses 192.168.7.1/24 ipv4.never-default yes ipv6.method disabled
```

## Каждый день

- `tools/agm-run.sh IMG` — прошить `boot_b` и загрузить iOS один раз (из Android).
- `tools/agm-sh.sh` — root-шелл на телефоне (bash из бутстрапа, без него toybox sh); `tools/agm-sh.sh 'команда'` — одна команда.
- С бутстрапом: `ssh root@192.168.7.2` / `mobile@…` (пароль `alpine`, смените `passwd`), по Wi-Fi тоже; Sileo на домашнем экране, `apt` в шелле.
- `tools/agm-usbnet.sh [файл]` — живой лог ядра (UDP 5140).
- `tools/agm-reboot.sh` — перезагрузка из iOS (sync), телефон вернётся в Android.
- `tools/agm-drop.py LOCAL=/путь[:MODE[:UID]] …` — положить файлы в корень iOS из Android, без
  перепрошивки (применяются при следующей загрузке iOS).
- Изменить ядро: ветка с патчами — клон xnu с применёнными `xnu/patches` (topaz `tools/build-local.sh`
  делает это сам), новый патч — `git format-patch` в `xnu/patches/`, затем `tools/agm-build.sh NAME`.
- boot-args — `boot-args.txt` (`@WIFIMAC@` берётся из `wifimac.local`).

## Состав

| Каталог | Что |
|---|---|
| `../xnu/patches/096x–099x` | ядро: плата AGM, PMIC/кнопки/тач/зарядка, Mali, DPU, Wi-Fi (Marlin3/SIPC), USB, drop box |
| `../tools/mkadt.py --agm` | Apple Device Tree для AGM H6 |
| `shim-agm/` | загрузчик XNU из `boot_b` (EL2→EL1, CPU, GIC, PMIC watchdog, payload) |
| `tools/` | сборка, установка, запуск, GPT, drop box, лог, шелл |
| `shell/` | `agm-shelld` (root-шелл по TCP 2323) и сборка toybox для iOS |
| `gpu/` | userland GPU: прослойка libdrm → IOKit, Mesa (panfrost) для iOS, GL-инъекция в backboardd |
| `wcn/inigen` | генератор конфигурации Wi-Fi из `wifi_board_config.ini` (вендорский формат) |
| `docker/` | образ тулчейна для shim |

Не в репозитории (берутся с телефона/из IPSW): бэкапы разделов, `boot_b.img` (на него
накладывается shim), прошивка Wi-Fi `wcnmodem.bin` и `wifi_board_config.ini` (`/odm/firmware`
на Android), MAC Wi-Fi, сам IPSW и образ корня.
