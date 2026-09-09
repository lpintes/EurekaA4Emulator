# Zostavenie emulatora a testov. Vstupne body su build.bat, build-tests.bat
# a run-tests.bat -- tie si predradia mingw na PATH a zavolaju tento subor.
# Volat `mingw32-make` priamo ide tiez, ale potom si MINGW64 nastav sam.
#
# Preco make a nie davka: davka prekladala vzdy vsetkych sedem jednotiek,
# lebo nevedela, ktora hlavicka sa zmenila. Tu to vie -- gcc -MMD vypise
# vedla kazdeho .o subor .d so zoznamom hlaviciek a make podla neho
# prelozi presne to, co treba. Zastaraly objekt tak nevznikne a nemusi sa
# tomu predchadzat prekladom vsetkeho.
#
# POZOR: make si shell vyberie podla PATH -- z cmd.exe je to cmd, z bashu
# sh.exe. Recepty preto nesmu pouzivat nic, co je len v jednom z nich:
# ziadne `copy`, `if not exist`, `rem`, `mkdir -p`. `mkdir build` funguje
# v oboch, kopirovanie READ.COM uz nie, a preto ho robi run-tests.bat.

MINGW64 ?= C:/msys64/mingw64
CXX := $(MINGW64)/bin/g++.exe
CC  := $(MINGW64)/bin/gcc.exe
RC  := $(MINGW64)/bin/windres.exe

BUILD := build
BIN   := bin

WARN     := -Wall -Wextra -Wno-unused-parameter
DEPFLAGS  = -MMD -MP
STATIC   := -static -static-libgcc -static-libstdc++

CXXFLAGS := -std=c++20 -O2 $(WARN) -Isrc -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
CFLAGS   := -std=c11 -O2 $(WARN) -Isrc
# Testy sa doteraz prekladali bez WINVER a nic sa tym nepokazilo: machine.h
# ani virtual_disk.h windows.h netahaju, takze o tychto makrach nevedia.
# Necham to tak, aby sa spolu s prechodom na make nemenilo aj chovanie.
TESTFLAGS := -std=c++20 -O2 $(WARN) -Isrc

EMU_NAMES  := main machine virtual_disk cpm_disk disk_stash disk_layout \
              disk_split text_codec audio_player \
              diagnostics host_console emulator_thread main_window dialogs \
              settings
# Nezavisle na emulatore, da sa vziat do ineho projektu tak ako je.
WIN_NAMES  := window dialog
EMU_OBJS   := $(addprefix $(BUILD)/,$(addsuffix .o,$(EMU_NAMES))) \
              $(addprefix $(BUILD)/win_,$(addsuffix .o,$(WIN_NAMES))) \
              $(BUILD)/z80.o $(BUILD)/eureka_res.o
# Objekty, proti ktorym sa linkuju sonda a integracny test. Bez main.o
# (ma vlastny wmain) a bez audio_player.o (testy nehraju).
CORE_OBJS  := $(BUILD)/machine.o $(BUILD)/virtual_disk.o $(BUILD)/cpm_disk.o \
              $(BUILD)/disk_stash.o \
              $(BUILD)/diagnostics.o $(BUILD)/text_codec.o $(BUILD)/z80.o

EMU        := $(BIN)/EurekaA4Emulator.exe
TEST_EXES  := $(BIN)/codec_test.exe $(BIN)/disk_test.exe \
              $(BIN)/settings_test.exe \
              $(BIN)/diag_probe.exe $(BIN)/integration_test.exe

.PHONY: all tests check clean
.DEFAULT_GOAL := all

all: $(EMU)
tests: $(EMU) $(TEST_EXES)

$(BUILD) $(BIN):
	mkdir $@

$(BUILD)/z80.o: src/z80.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/win_%.o: src/win/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/test_%.o: tests/%.cpp | $(BUILD)
	$(CXX) $(TESTFLAGS) $(DEPFLAGS) -c $< -o $@

# Ponuka, akceleratory, sablony dialogov a manifest. --codepage=65001 preto,
# ze .rc je v UTF-8 a su v nom slovenske retazce; bez toho by windres cital
# bajty ako ANSI a diakritika by sa do zdrojov dostala rozsypana -- a ticho.
# --include-dir preto, aby "resource.h" aj "eureka.manifest" nasiel vedla .rc.
$(BUILD)/eureka_res.o: src/res/eureka.rc src/res/resource.h \
                       src/res/eureka.manifest | $(BUILD)
	$(RC) --codepage=65001 --include-dir src/res -I src -i $< -o $@

# -mwindows robi z toho program GUI subsystemu, takze sa pri spusteni
# neotvori ziadne okno konzoly. Konzolu si emulator vypyta sam, az ked treba
# (--diag, --help) -- viz host_console.cpp. Vstupny bod je preto wWinMain
# a -municode zostava, lebo aj ten jeho startup je sirokoznakovy.
#
# Staticke runtime kniznice preto, aby EXE bezalo aj mimo msys2 shellu.
$(EMU): $(EMU_OBJS) | $(BIN)
	$(CXX) -municode -mwindows $(STATIC) -s -o $@ $(EMU_OBJS) \
	    -lwinmm -lole32 -lshell32 -luuid -lcomctl32

# codec_test a disk_test maju obycajny main, preto bez -municode; s nim
# linker spadne na chybajucom wWinMain.
$(BIN)/codec_test.exe: $(BUILD)/test_codec_test.o $(BUILD)/text_codec.o | $(BIN)
	$(CXX) $(STATIC) -o $@ $^

$(BIN)/disk_test.exe: $(BUILD)/test_disk_test.o $(BUILD)/virtual_disk.o \
                     $(BUILD)/cpm_disk.o $(BUILD)/disk_layout.o \
                     $(BUILD)/disk_split.o $(BUILD)/text_codec.o \
                     $(BUILD)/disk_stash.o | $(BIN)
	$(CXX) $(STATIC) -o $@ $^

# settings.o sa pyta shellu, kde je %APPDATA% (SHGetKnownFolderPath), preto
# shell32 a ole32 aj tu; FOLDERID_RoamingAppData je GUID z uuid.
$(BIN)/settings_test.exe: $(BUILD)/test_settings_test.o $(BUILD)/settings.o | $(BIN)
	$(CXX) $(STATIC) -o $@ $^ -lole32 -lshell32 -luuid

$(BIN)/diag_probe.exe: $(BUILD)/test_diag_probe.o $(CORE_OBJS) | $(BIN)
	$(CXX) $(STATIC) -municode -o $@ $^

$(BIN)/integration_test.exe: $(BUILD)/test_integration_test.o $(CORE_OBJS) | $(BIN)
	$(CXX) $(STATIC) -municode -o $@ $^

# ---------------------------------------------------------------------------
# Testy
#
# Kazdy test je samostatny ciel, aby ich make -j pustil naraz -- su to
# nezavisle procesy, nic nezdielaju. Priecinok diskety pripravuje
# run-tests.bat: kopirovanie sa v neutralnom recepte spravit neda a rezim
# `com` bez eurekatech/TECHMAN1/READ.COM zlyha.

A4ROM ?= C:/b/a4rom.dmp
ROM   ?= $(A4ROM)
DISK  ?= $(BUILD)/testdisk

MODES  := bas com kbd power dc rtc hudba format wp hlaseni
CHECKS := check-codec check-disk check-settings $(addprefix check-,$(MODES))

.PHONY: $(CHECKS)

check: $(CHECKS)

check-codec: $(BIN)/codec_test.exe
	$(BIN)/codec_test.exe

check-disk: $(BIN)/disk_test.exe
	$(BIN)/disk_test.exe

check-settings: $(BIN)/settings_test.exe
	$(BIN)/settings_test.exe

# Rezimy integracneho testu sa generuju ako VYSLOVNE pravidla. Vzorove
# pravidlo `check-%` tu bolo a bola to ticha pasca: make implicitne ani
# vzorove pravidla na .PHONY cieloch nehlada, takze vsetkych sedem rezimov
# zostalo bez receptu, make ich vyhlasil za splnene a run-tests.bat oznamil
# uspech bez toho, aby cokolvek z nich bezalo.
define CHECK_RULE
check-$(1): $$(BIN)/integration_test.exe
	$$(BIN)/integration_test.exe $$(ROM) $$(DISK) $(1)
endef
$(foreach m,$(MODES),$(eval $(call CHECK_RULE,$(m))))

# Jedine miesto, kde sa shellu vyhnut neda: mazanie sa v cmd a v sh pise
# inak. Rozhodne to nazov shellu, ktory si make sam vybral.
CLEANFILES := $(BUILD)/*.o $(BUILD)/*.d $(TEST_EXES) $(EMU)
ifeq (,$(findstring sh,$(notdir $(SHELL))))
clean:
	-del /q $(subst /,\,$(CLEANFILES))
else
clean:
	rm -f $(CLEANFILES)
endif

-include $(wildcard $(BUILD)/*.d)
