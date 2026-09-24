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
# Kazda funkcia a kazda premenna do vlastnej sekcie, aby linker vedel
# zahodit tie, na ktore sa nikto neodkazuje (-Wl,--gc-sections nizsie).
# Samo o sebe to nic nemeni, len rozdrobi objekty; zmysel to dostane az
# tam. Odmerane: 2 032 128 -> 1 937 408 bajtov, teda o 4,7 % mensie EXE.
#
# -Os sa tu neskusaj. Pri nom prestane byt presuvaci konstruktor
# std::string inlinovany a v libstdc++ ako samostatny symbol neexistuje,
# takze settings.o, disk_split.o, virtual_disk.o aj disk_layout.o skoncia
# na undefined reference. Nelinkuje sa to vobec.
SECTIONS := -ffunction-sections -fdata-sections

CXXFLAGS := -std=c++20 -O2 $(WARN) $(SECTIONS) -Isrc -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
CFLAGS   := -std=c11 -O2 $(WARN) $(SECTIONS) -Isrc
# Testy sa doteraz prekladali bez WINVER a nic sa tym nepokazilo: machine.h
# ani virtual_disk.h windows.h netahaju, takze o tychto makrach nevedia.
# Necham to tak, aby sa spolu s prechodom na make nemenilo aj chovanie.
TESTFLAGS := -std=c++20 -O2 $(WARN) -Isrc

EMU_NAMES  := main machine md5 virtual_disk cpm_disk disk_stash disk_layout \
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
CORE_OBJS  := $(BUILD)/machine.o $(BUILD)/md5.o $(BUILD)/virtual_disk.o \
              $(BUILD)/cpm_disk.o $(BUILD)/disk_stash.o \
              $(BUILD)/diagnostics.o $(BUILD)/text_codec.o $(BUILD)/z80.o
# Testovacia vrstva nad strojom (eureka.md). Zije v tests/, lebo GUI ju
# nepotrebuje.
SESSION_OBJS := $(BUILD)/test_eureka_session.o

EMU        := $(BIN)/EurekaA4Emulator.exe
TEST_EXES  := $(BIN)/codec_test.exe $(BIN)/disk_test.exe \
              $(BIN)/settings_test.exe $(BIN)/zex_test.exe \
              $(BIN)/diag_probe.exe $(BIN)/integration_test.exe

.PHONY: all tests check clean
.DEFAULT_GOAL := all

all: $(EMU)
tests: $(EMU) $(TEST_EXES)

$(BUILD) $(BIN):
	mkdir $@

# Kazde pravidlo si pyta aj tento subor. Su v nom prepinace prekladu, a bez
# toho ich zmena objekty neprelozi: .d subory sleduju hlavicky, nie
# Makefile, takze make vyhlasi za hotove nieco, co je prelozene inak, nez
# hovoria pravidla. Prislo to hned pri prvom pokuse -- pridanie
# -ffunction-sections nezmenilo velkost EXE ani o bajt, lebo sa nic
# neprelozilo. Je to ta ista pasca ako davne "ak uz existuje, preskoc",
# len tichsia.
$(BUILD)/z80.o: src/z80.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.cpp Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/win_%.o: src/win/%.cpp Makefile | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/test_%.o: tests/%.cpp Makefile | $(BUILD)
	$(CXX) $(TESTFLAGS) $(DEPFLAGS) -c $< -o $@

# Ponuka, akceleratory, sablony dialogov a manifest. --codepage=65001 preto,
# ze .rc je v UTF-8 a su v nom slovenske retazce; bez toho by windres cital
# bajty ako ANSI a diakritika by sa do zdrojov dostala rozsypana -- a ticho.
# --include-dir preto, aby "resource.h" aj "eureka.manifest" nasiel vedla .rc.
$(BUILD)/eureka_res.o: src/res/eureka.rc src/res/resource.h \
                       src/res/eureka.manifest Makefile | $(BUILD)
	$(RC) --codepage=65001 --include-dir src/res -I src -i $< -o $@

# -mwindows robi z toho program GUI subsystemu, takze sa pri spusteni
# neotvori ziadne okno konzoly. Konzolu si emulator vypyta sam, az ked treba
# (--diag, --help) -- viz host_console.cpp. Vstupny bod je preto wWinMain
# a -municode zostava, lebo aj ten jeho startup je sirokoznakovy.
#
# Staticke runtime kniznice preto, aby EXE bezalo aj mimo msys2 shellu.
#
# -s zahodi symboly, --gc-sections zahodi kod, na ktory sa nikto
# neodkazuje -- to druhe funguje len vdaka $(SECTIONS) pri preklade.
# Obe su len na emulatore: testy a sonda si symboly nechavaju, lebo ked
# spadnu, chce sa vediet kde.
$(EMU): $(EMU_OBJS) | $(BIN)
	$(CXX) -municode -mwindows $(STATIC) -Wl,--gc-sections -s -o $@ \
	    $(EMU_OBJS) -lwinmm -lole32 -lshell32 -luuid -lcomctl32

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

# Hole jadro bez stroja; ZEXDOC nie je v repozitari, preto nie je ani
# medzi check-* (ea4-z8y). Obycajny main, teda bez -municode.
$(BIN)/zex_test.exe: $(BUILD)/test_zex_test.o $(BUILD)/z80.o | $(BIN)
	$(CXX) $(STATIC) -o $@ $^

$(BIN)/diag_probe.exe: $(BUILD)/test_diag_probe.o $(SESSION_OBJS) $(CORE_OBJS) | $(BIN)
	$(CXX) $(STATIC) -municode -o $@ $^

$(BIN)/integration_test.exe: $(BUILD)/test_integration_test.o $(CORE_OBJS) | $(BIN)
	$(CXX) $(STATIC) -municode -o $@ $^

# ---------------------------------------------------------------------------
# Testy
#
# Kazdy test je samostatny ciel, aby ich make -j pustil naraz -- su to
# nezavisle procesy, nic nezdielaju. Priecinok diskety pripravuje
# run-tests.bat: kopirovanie sa v neutralnom recepte spravit neda a rezim
# `com` bez prveho READ.COM z Technical Manualu zlyha.
#
# Manual je material tretich stran a v repozitari nie je (ROM-NOTICE.txt).
# Ked ho run-tests.bat nenajde, nastavi v PROSTREDI SKIP_MODES na `com wp` --
# READ.COM potrebuju oba, `wp` nim overuje citanie z chranenej diskety.
# Testov je potom patnast, nie sedemnast. Prostredim a nie argumentom preto,
# ze su to dve slova a make by to druhe vzal ako dalsi ciel.
#
# Zoznam rezimov je len tu. Druha kopia inde by sa s touto rozisla potichu.

A4ROM ?= C:/b/a4rom.dmp
ROM   ?= $(A4ROM)
DISK  ?= $(BUILD)/testdisk

ALL_MODES := bas com kbd power dc rtc hudba zvuk format wp hlaseni snimka akord budik trap
MODES  := $(filter-out $(SKIP_MODES),$(ALL_MODES))
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
