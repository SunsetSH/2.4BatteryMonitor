# План

Задачи отмечаются `[x]` в той же правке, что и код. TODO в коде допустим только со ссылкой на пункт отсюда.
Разбор и обоснования: [REVIEW-2026-11.md](REVIEW-2026-11.md).

## Среда и качество

- [x] Цель `make check`: сборка, все тесты, проверка значков
- [x] Подключить к `make test` тесты arbiter, semico, steelseries_driver, nothing_ear_protocol
- [x] Починить `tests/dareu_driver_test.cpp` и подключить к `make test`
- [x] Отдельная цель на каждый тест, параллельная сборка (`make -j` по умолчанию)
- [x] Общая заглушка `tests/fake_hid.h` для тестов драйверов
- [x] Проверка фраз интерфейса и покрытия переводов в `make test` (`tests/lang_test.cpp`)
- [x] CI на GitHub Actions: Windows (MSYS2, `make check`) и Ubuntu (`./build-linux.sh && ./build-linux.sh test`)
- [x] Сборка с `-Wextra` без предупреждений, `-Werror` в `make check`
- [x] Проверка, что каждый `tests/*.cpp` собирается Makefile (`tests/tests_registered_test.cpp`)
- [ ] Тесты на записанных ответах для оставшихся 57 файлов `src/drivers/*.cpp` без теста; по массовости: razer_headset,
      glorious, pulsar, rog, sony_inzone, eightbitdo, steelseries_elite, misc_mice, gamesir, xinput (разбор вынести
      в `*_protocol.h`), затем остальные: akg akko areson atk_bitmouse atk_keyboard atk_serial audeze barracuda_pro
      beitong chimera coolermaster darmoshark endgame endgame_we eweadn fantech flydigi fractal galaxy_buds
      gamesir_ble jbl logitech_g435_0adf mchose mchose_a5 mchose_g3 mchose_headset mchose_keyboard moondrop msi
      nothing_ear nzxt phonelink plantronics rongyuan royal_kludge scyrox sonix sprime steam_controller turtlebeach
      via_dongle zenith zowie (airpods, ble_adverts, fastpair, generic уже покрыты разбором в `*_protocol.h` или
      работают с Windows API напрямую)

## Структура кода

- [ ] Разделить `src/main.cpp` (сейчас 1429 строк): вынесены значки трея, консольный вывод, демо-устройства,
      правила показа, настройки реестра, окно настроек; остались записи трея, меню и цикл опроса
- [ ] Разделить `src/widget.cpp` (1070 строк): раскладка, отрисовка, мониторы
- [ ] Разделить `src/battery_history.cpp` (691 строка): хранение и окно графика
- [ ] Разделить `src/linux/main_linux.cpp` (711 строк)
- [x] Вынести цвета из `widget.cpp`, `main.cpp`, `battery_history.cpp` в общий заголовок (`src/ui_theme.h`)
- [x] Окно переименования в общей палитре

## Быстродействие

- [x] Замер по драйверам: `--timing`
- [x] Промежуточные результаты в обзоре во время опроса
- [x] Кэш перечисления HID и строк продукта
- [x] Отступ для драйверов, которые долго ничего не находят
- [x] Пауза опроса при блокировке и выключенном экране
- [x] Параллельные полосы для производителей с собственным VID ([ADR 0007](adr/0007-parallel-poll-lanes.md))
- [ ] Проверить полосы на реальных устройствах Logitech, Razer, SteelSeries, Corsair, Sony (вывод `--list` и
      `--timing` от владельцев)

## Релизы

- [x] Создать ключ подписи: `scripts/sign-update.ps1 -InitializeKey`, закоммитить `installer/update-public-key.txt`
- [x] Первый релиз `v1.0.0`: портативный ZIP и `SHA256SUMS.txt` (без установщика: на машине сборки нет Inno Setup)
- [ ] Установщик и `.sig` в следующем релизе (нужен Inno Setup на машине сборки)
- [ ] Манифесты winget и Scoop (нужны URL и SHA-256 опубликованного релиза)

## Linux

- [x] Фаза 2, часть 1: батареи периферии из `/sys/class/power_supply` (`src/power_supply.h`)
- [ ] Фаза 2, часть 2: остальные драйверы производителей

## Совместимость

- [x] «Сообщить об устройстве» и формы обращений `.github/ISSUE_TEMPLATE/`
- [x] Joy-Con (L) / (R) в драйвере Switch Pro (не проверено на железе)
- [ ] Sony WH / WF, Huawei FreeBuds, Bose по RFCOMM: по источникам из ревью 4.3, когда найдутся владельцы для проверки
- [ ] Сверка таблиц VID:PID с HeadsetControl, Solaar, OpenRazer скриптом (ревью 4.4)

## Интерфейс и функции

- [x] Обзор на карточках в общей палитре, тёмный заголовок окна
- [x] Окно настроек; меню трея только для частых действий
- [x] Красный по порогу устройства, оранжевый «скоро мало»; одна логика «заряжается / полностью» везде
- [x] Напоминание «заряжено до N%», тишина в полноэкранных приложениях, `status.json`
- [x] Окна проверены в тёмной и светлой теме при 100% DPI
- [ ] Проверить окна при 150% DPI на живой системе

## Документация

- [x] README на русском (по умолчанию), английском и китайском; разделы для разработчиков в `docs/DEVELOPERS.md`
- [x] Таблица устройств в `docs/DEVICES.md` без дублей, благодарности в `CREDITS.md`
- [x] Переводы интерфейса на все 14 языков (покрытие печатает `lang_test`)
