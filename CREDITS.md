# Credits

[Back to README](README.md)

The app icon is the [2.4 monogram](res/app.svg).

The protocols come from these projects' research:

- Google Fast Pair specification: left / right / case battery broadcasts of Fast Pair earbuds
- HeadsetControl Astro drivers, eh-fifty, and AstroBar: Astro A50 X / Gen 5 / Gen 4 / Gen 3 / A20 battery protocols; OpenAstroCC: A50 Gen 3 `0x72` status read
- mouse-battery-monitor (stuffz): VAXEE battery and charging commands (protocol documentation)
- HeadsetControl: SteelSeries, Corsair, HyperX, Logitech and Sony INZONE headsets
- inzone-buds-battery-tray (kinako19) and inzone-buds-battery-tray (takamachi66): Sony INZONE Buds
- OpenRazer: Razer
- Solaar and the Logitech HID++ 2.0 documents: Logitech
- g435-battery-percentage and logitech-g435-battery: Logitech G435 (`0ADF` dongle)
- rivalcfg: SteelSeries mice (Rival Pro from its pull request #294)
- glorious-indicator and glorious-ctl: Glorious
- Hitscan's web configurator: Hitscan
- Linux `hid-kysona` (Lode Willems): Kysona, Pulsar and ATK
- ATK's web configurator (hub.atk.pro): ATK mice and the N9 Pro / N9 Ultra headsets
- PulsarBattery: Pulsar X2 V3 / X2 V3 eS
- HaloBattery PR #350 and mouse-battery: ATK / VXE NK NANO dongle
- HaloBattery (MIT): Audeze Maxwell, NZXT Lift Elite, Poly BT600, Razer Barracuda Pro, SteelSeries Arctis Nova Elite, ATK V75X and Endgame Gear XM2we, ported from its providers (each driver names its source)
- HaloBattery `providers/turtlebeach.py` and halobattery-stealth700 (PaulRichez, MIT): Turtle Beach Stealth 700 Gen 3; Neap (findings only): the Turtle Beach Swarm II device family
- G-Wolves' web driver: G-Wolves models and their protocols
- BatteryWatch: CHERRY keyboards on the 2.4 GHz dongle (frame format and battery query)
- Keychron Launcher: Keychron mice
- NinjaForce and ninjutso-battery-tray: Ninjutso Ten / Ten Air feature queries
- nape_pro and KeychronNapeBatteryWidget: Keychron Nape Pro on the Link-KM receiver
- LAMZU Aurora: LAMZU mice
- vole and Endgame_Gear_Web_Configurator: Endgame Gear 4K mice
- mouse-gauge and HaloBattery: WLMouse
- OpenMouse mouse-protocol and FinalmousePollingRateSwitcher: Finalmouse UltralightX / Starlight X (from Finalmouse's xpanel)
- redragon-battery-omarchy and g49-web-control: Redragon ids
- m913-ctl and redragon-m913-battery-tray: Areson `25A7` frame layout and `GetPower` command
- peripheral-battery-tray / G-Helper: ASUS ROG; pelta-control / BatteryWatch: ROG Pelta
- zowie-battery and zowie-battery-tray: ZOWIE EC-CW and DW
- RK's web driver: Royal Kludge MG6 Pro
- MCHOSE-Monitor: MCHOSE G3 V2
- mchose-a5-pro-max-web-driver (MIT), HaloBattery (MIT), mchose-macos (MIT) and OpenMouse mouse-protocol (docs): MCHOSE A5 Pro / A5 Pro Max (first generation, VID `2023`)
- LGSTrayBattery-MotospeedX6, omarchy-delux-m900, aj179-linux: Motospeed, Delux, Ajazz
- AreYouLowBattery: CompX receiver reports for Delux M900 Pro
- ajazz-aj139-macos and AjazzAJ139V2: Ajazz AJ139 V2
- Incott-Manager, incott-mouse-app and MouseBattery: Incott
- The Wraith web driver: Wraith W1
- OpenRazer PR #2794 and omarchy-blackshark-v3: BlackShark V3
- OpenRazer PR #2862 and HaloBattery: BlackShark V2 Pro (2023)
- razer-barracuda-x-battery and barracuda-x-battery: Razer Barracuda X (2022) on the `1532:0550` receiver; razer-barracuda-battery-tray: framing of the `1532:0552` receiver
- OpenRazer PR #2899: BlackShark V3 X
- HyperXWM: HyperX Cloud III Wireless
- HyperHeadset: HyperX Cloud III S, Cloud II Core, Cloud Flight S
- HyperBatteryMonitor: HyperX Cloud Stinger Core Wireless
- BatteryTray: Rongyuan and Xenta receivers (Attack Shark and others)
- sonix-keyboard-studio: SONiX keyboards on their 2.4G dongle (AJAZZ AK820 MAX, AK980 V2, AULA F108 Pro); ak820-rs and ak820-studio: AJAZZ AK820 Pro receiver `0C45:FDFD`
- openajazz and Kraber: Semico `1A2C` (`AA 55 CC 33` / `0x0E`) status protocol for Attack Shark X98 and AJAZZ AK820 Max Plus
- coolermaster-usb-protocol and MM731-Controller: Cooler Master MM712 and MM731
- GamesirBatteryTray and Gamesir-Nova-2-Lite-Battery-Detection: GameSir Nova
- GameSirBattery: GameSir Cyclone Pro Bluetooth LE "base info" battery request
- jabridge: Jabra GNP protocol (Link 380 / 390 dongles)
- xd5-go-web: Fantech Helios Go XD5
- Linux `hid-playstation`: DualShock 4 / DualSense report layout
- omarchy-aula-SC580: AULA SC580 / SC580X vendor protocol
- RAWM HUB web app (Hub Web 2.0) and its device table: RAWM mice and the SADES Dagger
- SDL's Flydigi HID driver and HaloBattery issue #191: Vader 5 Pro and Apex 5 query framing and battery layout
- BEITONG iControl (its KP20 and KP40D pages and the KP20 receiver's USB traffic): BEITONG KP20 / KP40D battery query and payload scrambling
- SDL's 8BitDo HID driver and 8bitdo-ultimate2-linux: 8BitDo battery byte and vendor HID protocol
- SDL (`src/joystick/hidapi/SDL_hidapi_switch.c`, `src/joystick/usb_ids.h`): Switch Pro and Joy-Con battery byte of report `0x30`, Joy-Con product ids
- Linux kernel, `Documentation/ABI/testing/sysfs-class-power`: peripheral batteries in `/sys/class/power_supply` (Linux build)
- HaloBattery: Corsair Sabre V2 Pro Ultralight, Compx protocol on `FF02:0002`
- MouseDock (MIT) and BrimDeck: Scyrox V12M profile read
- JBL QuantumENGINE: HIDV1 connection and battery reports for the Quantum 350, and HIDV3 device-info messages for newer Quantum headsets
- jbl-quantum-linux: JBL Quantum 610 HIDV1 battery and link reports, and the Quantum lineup (360 as a HIDV1 dongle model)
