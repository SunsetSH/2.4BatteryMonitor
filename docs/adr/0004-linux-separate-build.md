# 0004. Linux-сборка отдельно от Windows

Статус: принято (зафиксировано задним числом, 2026-11-21)

## Контекст

Драйверы производителей почти не зависят от ОС: им нужен HID-ввод-вывод. Интерфейс (виджет на панели задач, трей,
реестр) полностью завязан на Win32.

## Решение

- Общие драйверы компилируются под Linux через `src/platform_compat.h` и HID-слой `src/linux/hid_linux.cpp` на hidraw.
- Интерфейс Linux свой: GTK4 + StatusNotifierItem в `src/linux/main_linux.cpp`.
- Отдельный `linux/Makefile.linux` с явным списком `DRIVERS`. Windows-сборка файлы `src/linux/` не видит.

## Последствия

- Изменение драйвера из списка `DRIVERS` должно собираться под Linux: без `windows.h`-only вызовов вне
  `platform_compat.h`.
- Новый драйвер не попадает в Linux автоматически, его нужно добавить в `DRIVERS` и `devices_linux.cpp`.
- Проверить Linux-сборку на машине разработчика (Windows) нельзя; нужен CI на Ubuntu (см. PLAN).
