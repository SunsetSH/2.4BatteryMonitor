# Contributing

## Reporting whether a device works

Most drivers were written from protocol documentation and could not be tested on every model.
For a local diagnosis, collect:

- the device name, and whether it is connected by receiver, cable or Bluetooth
- the output of `2.4BatteryMonitor.exe --list`
- the output of `2.4BatteryMonitor.exe --dump`
- what the vendor's own software shows as the battery level

The quickest way is *Report this device...* in the device's menu (or *Report a missing device...* in the
overview and the settings): it opens a GitHub issue with these details filled in. Nothing is sent until you
submit it.

## Adding a device

1. Run `2.4BatteryMonitor.exe --dump` and find your device's vendor and product IDs, and the
   vendor-defined collections (usage page `FFxx`) with their report sizes.
2. Find the battery protocol. Good places to look: HeadsetControl, OpenRazer, Solaar, rivalcfg,
   OpenRGB, OpenLinkHub, or a USB capture of the vendor software (Wireshark + USBPcap).
3. If the vendor already has a driver in `src/drivers/`, add the product ID (and any differences) there.
   Otherwise create `src/drivers/<vendor>.cpp`:

   ```cpp
   #include "drivers.h"

   void PollAcme(PollContext& ctx) {
       for (auto [vid, pid] : ctx.Products(0x1234)) {           // unclaimed products of this vendor
           for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.page() == 0xFF00; })) {
               HidDev d;
               if (!d.Open(*c)) continue;
               std::vector<BYTE> r;
               if (!d.Write({ 0x01, 0x02 }) || d.Read(r, 500) < 3) continue;   // hidapi-style buffers
               DeviceStatus s;
               s.key = L"acme:" + Hex4(pid);
               s.name = HidProduct(*c);
               s.kind = DeviceKind::Mouse;
               s.via = L"2.4 GHz receiver";
               s.percent = Clamp100(r[2]);
               ctx.Add(s, vid, pid);
               break;
           }
       }
   }
   ```

4. Declare the function in `src/drivers/drivers.h`, call it from `PollAllDevices()` in
   `src/devices.cpp` (before the generic fallbacks). `build.bat` and the Makefile pick up new files automatically.
5. Add a test with captured replies (see `tests/*_driver_test.cpp`) and add it to `make test` in the `Makefile`.
6. Update the device table in [`docs/DEVICES.md`](docs/DEVICES.md), and `CREDITS.md` for the protocol source.
7. Add a test on recorded replies with [`tests/fake_hid.h`](tests/fake_hid.h) (see `tests/kysona_driver_test.cpp`):
   level, charging, sleep, malformed reply, another vendor's device, deadline. Register it in the `Makefile`.
8. A new driver on a vendor id listed in [`src/driver_lanes.h`](src/driver_lanes.h) joins that lane;
   `tests/driver_lanes_test.cpp` fails otherwise.

### Rules for drivers

- **Read-only:** only send requests that read state. Never write settings, lighting, profiles, DPI or power
  options. Allowed, with a comment at the top of the driver saying why it is safe: a request that the vendor's own
  app sends only to read the battery; reading a feature report that switches a controller to full input reports
  (DualShock 4 over Bluetooth, as every game does; the controller drops back on reconnect); opening a Bluetooth
  RFCOMM link that the earbuds answer with a status frame (Galaxy Buds, Nothing Ear).
- **Exact matching:** only talk to devices you recognise by vendor and product ID, or by a
   vendor-specific collection that is unique to the protocol.
- **Always time out:** a missing device must not stall the poll for more than about a second.
  Passive protocols may need longer to cover a documented push interval (for example, Delux and
  Rapoo). Keep a single absolute deadline of at most 4 seconds per matching collection, do not
  restart it for unrelated reports, and document why the exception is needed.
- **Credit sources:** name the project and relevant source files in a comment at the top of the file.

## Translations

A language is one UTF-8 text file in `lang/` (copy `lang/_template.txt`, fill in `@code`, `@name` and the phrases). It is built into the app automatically. New UI text in the code goes through `T(L"...")`, or `TF(L"... {name} ...", { { L"name", value } })` when it contains values; never build translated sentences from pieces. After adding phrases, add them to `lang/_template.txt` too.

## Code style

C++17, no third-party dependencies: Win32 on Windows, plus GTK4 for the Linux tray. Keep the binary small.
Format changed lines with `git clang-format` (`.clang-format`: 4-space indent, 120-column lines); do not
reformat whole files.

Before a pull request, run `make check` (build, all tests, icon check). Enable the repository hooks once with
`git config core.hooksPath .githooks`.
