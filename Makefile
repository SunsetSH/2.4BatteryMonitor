# Builds 2.4BatteryMonitor.exe with MinGW-w64 (MSYS2 MINGW64/UCRT64 shell, or any mingw g++ on PATH).
CXX      ?= g++
WINDRES  ?= windres
VERSION  ?= 0.0.0
.DEFAULT_GOAL := 2.4BatteryMonitor.exe
# Parallel by default: Windows sets NUMBER_OF_PROCESSORS.
MAKEFLAGS += -j$(or $(NUMBER_OF_PROCESSORS),4)
# -Wno-cast-function-type: GetProcAddress results must be cast to the function's real type.
CXXFLAGS ?= -std=c++17 -Os -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type \
            -ffunction-sections -fdata-sections \
            -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
LDFLAGS  ?= -municode -mwindows -static -s -Wl,--gc-sections
LIBS     := -lhid -lsetupapi -lcfgmgr32 -lole32 -loleaut32 -lcomctl32 -lgdi32 -lruntimeobject -lws2_32 -lbluetoothapis -lwinhttp -lbcrypt -lwtsapi32

SRC := $(wildcard src/*.cpp) $(wildcard src/drivers/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC)) build/app_res.o

2.4BatteryMonitor.exe: $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

build/%.o: src/%.cpp $(wildcard src/*.h src/drivers/*.h) build/version.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -municode -c $< -o $@

build/version.h: scripts/generate-version.ps1 FORCE
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/generate-version.ps1 -Version "$(VERSION)"

# Every lang/*.txt is a built-in UI language: joined into one resource, each after an "@file <name>" line.
build/languages.txt: $(wildcard lang/*.txt)
	@mkdir -p build
	@printf '' > $@; for f in $^; do printf '@file %s\n' "$$(basename $$f .txt)" >> $@; cat "$$f" >> $@; printf '\n' >> $@; done

build/app_res.o: res/app.rc res/app.manifest res/app.ico build/languages.txt build/version.h
	@mkdir -p build
	$(WINDRES) -i $< -o $@ --include-dir res --include-dir build

clean:
	rm -rf build 2.4BatteryMonitor.exe

test-icons: 2.4BatteryMonitor.exe
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) -municode tests/icon_resources_test.cpp -o build/tests/icon_resources_test.exe
	./build/tests/icon_resources_test.exe ./2.4BatteryMonitor.exe

# One target per test, so `make -j` builds them in parallel; `test` then runs them in order.
# A single test: make build/tests/rapoo_driver_test.exe && ./build/tests/rapoo_driver_test.exe
TEST_DEPS := $(wildcard src/*.h src/drivers/*.h tests/*.h) build/version.h
PORTABLE_FLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc
TESTS :=

# $(1) test name (tests/$(1).cpp), $(2) sources it links, $(3) libraries.
define win_test
TESTS += build/tests/$(1).exe
build/tests/$(1).exe: tests/$(1).cpp $(2) $$(TEST_DEPS)
	@mkdir -p build/tests
	$$(CXX) $$(CXXFLAGS) tests/$(1).cpp $(2) -o $$@ $(3)
endef
# Tests of portable modules build without the Windows defines, like the Linux build would.
define portable_test
TESTS += build/tests/$(1).exe
build/tests/$(1).exe: tests/$(1).cpp $(2) $$(TEST_DEPS)
	@mkdir -p build/tests
	$$(CXX) $$(PORTABLE_FLAGS) tests/$(1).cpp $(2) -o $$@ $(3)
endef

$(eval $(call win_test,update_core_test,,))
$(eval $(call win_test,update_signature_test,src/i18n.cpp src/app_settings.cpp src/self_update.cpp,-lwinhttp -lbcrypt))
$(eval $(call win_test,wlmouse_driver_test,src/drivers/wlmouse.cpp,))
$(eval $(call win_test,mixed_devices_test,src/drivers/wlmouse.cpp src/drivers/ajazz_ak992.cpp src/drivers/endorfy.cpp,))
$(eval $(call portable_test,device_aliases_test,,))
$(eval $(call portable_test,alert_rules_test,,))
$(eval $(call portable_test,history_model_test,,))
$(eval $(call portable_test,runtime_estimate_test,,))
$(eval $(call win_test,aula_mouse_driver_test,src/drivers/aula_mouse.cpp,))
$(eval $(call win_test,darkrim_driver_test,src/drivers/darkrim.cpp,))
$(eval $(call win_test,nuphy_driver_test,src/drivers/nuphy.cpp,))
$(eval $(call win_test,maono_driver_test,src/drivers/maono.cpp,))
$(eval $(call win_test,astro_driver_test,src/drivers/astro.cpp,))
$(eval $(call win_test,steelseries_mouse_test,src/drivers/steelseries.cpp,))
$(eval $(call portable_test,rapoo_protocol_test,,))
$(eval $(call win_test,rapoo_driver_test,src/drivers/rapoo.cpp,))
$(eval $(call portable_test,rawm_protocol_test,,))
$(eval $(call win_test,rawm_driver_test,src/drivers/rawm.cpp,))
$(eval $(call win_test,ninjutso_driver_test,src/drivers/ninjutso.cpp,))
$(eval $(call win_test,cherry_driver_test,src/drivers/cherry.cpp,))
$(eval $(call win_test,cherry_xtrfy_driver_test,src/drivers/cherry.cpp,))
$(eval $(call win_test,ninjutso_cable_test,src/drivers/ninjutso.cpp,))
$(eval $(call win_test,machenike_driver_test,src/drivers/machenike.cpp,))
$(eval $(call win_test,niceboy_driver_test,src/drivers/niceboy.cpp,))
$(eval $(call win_test,redragon_evision_driver_test,src/drivers/redragon_evision.cpp,))
$(eval $(call win_test,alienware_driver_test,src/drivers/alienware.cpp,))
$(eval $(call win_test,mchose_modern_driver_test,src/drivers/mchose_modern.cpp,))
$(eval $(call win_test,vgn_driver_test,src/drivers/vgn.cpp,))
$(eval $(call win_test,vaxee_driver_test,src/drivers/vaxee.cpp,))
$(eval $(call win_test,finalmouse_driver_test,src/drivers/finalmouse.cpp,))
$(eval $(call win_test,keychron_driver_test,src/drivers/keychron.cpp,))
$(eval $(call win_test,hyperx_haste_driver_test,src/drivers/hyperx.cpp,))
$(eval $(call win_test,hyperx_cloud_flight_s_driver_test,src/drivers/hyperx.cpp,))
$(eval $(call win_test,aula_sc580_test,src/drivers/aula.cpp,))
$(eval $(call win_test,hyperx_stinger_driver_test,src/drivers/hyperx.cpp,))
$(eval $(call portable_test,galaxy_buds_protocol_test,,))
$(eval $(call portable_test,logitech_g435_0adf_test,,))
$(eval $(call portable_test,xenta_protocol_test,src/drivers/xenta_protocol.cpp,))
$(eval $(call portable_test,labels_test,src/labels.cpp,))
$(eval $(call win_test,brands_test,src/brands.cpp,))
$(eval $(call portable_test,xbox_bluetooth_identity_test,,))
$(eval $(call win_test,jabra_driver_test,src/drivers/jabra.cpp,))
$(eval $(call portable_test,gamesir_ble_protocol_test,,))
$(eval $(call win_test,arbiter_driver_test,src/drivers/arbiter.cpp src/drivers/lamzu.cpp,))
$(eval $(call win_test,semico_driver_test,src/drivers/semico.cpp,))
$(eval $(call win_test,steelseries_driver_test,src/drivers/steelseries.cpp,))
$(eval $(call win_test,nothing_ear_protocol_test,,))
$(eval $(call portable_test,json_output_test,,))
$(eval $(call portable_test,hid_util_test,src/hid_util.cpp,))
$(eval $(call portable_test,lang_test,,))
$(eval $(call portable_test,status_text_test,,))
$(eval $(call portable_test,report_text_test,,))
$(eval $(call portable_test,driver_backoff_test,,))
$(eval $(call win_test,dareu_driver_test,src/drivers/dareu.cpp,))
$(eval $(call win_test,logitech_driver_test,src/drivers/logitech.cpp src/hid_util.cpp,))
$(eval $(call win_test,razer_driver_test,src/drivers/razer.cpp src/hid_util.cpp,))
$(eval $(call win_test,corsair_driver_test,src/drivers/corsair.cpp src/hid_util.cpp,))
$(eval $(call win_test,playstation_driver_test,src/drivers/playstation.cpp src/hid_util.cpp,))
$(eval $(call win_test,kysona_driver_test,src/drivers/kysona.cpp src/hid_util.cpp,))
$(eval $(call win_test,switch_pro_driver_test,src/drivers/switch_pro.cpp src/hid_util.cpp,))
$(eval $(call portable_test,generic_protocol_test,,))
$(eval $(call portable_test,driver_lanes_test,,))
$(eval $(call win_test,power_supply_test,src/hid_util.cpp,))
$(eval $(call portable_test,ignore_list_test,,))
$(eval $(call win_test,self_update_test,src/self_update.cpp,))
$(eval $(call portable_test,tests_registered_test,,))

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || { echo "FAILED: $$t"; exit 1; }; done
	@echo "All $(words $(TESTS)) test programs passed"

# Full check before "done": the release build, every regression test and the icon resources.
check: 2.4BatteryMonitor.exe test test-icons
# Whatever check has to rebuild must build without a warning.
check: CXXFLAGS += -Werror
check: PORTABLE_FLAGS += -Werror

FORCE:

.PHONY: check clean test test-icons FORCE
