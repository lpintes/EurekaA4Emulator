@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build

cl /nologo /O2 /MT /TC /c src\z80.c /Fobuild\z80.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\machine.cpp /Fobuild\machine.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\virtual_disk.cpp /Fobuild\virtual_disk.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\text_codec.cpp /Fobuild\text_codec.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\audio_player.cpp /Fobuild\audio_player.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\diagnostics.cpp /Fobuild\diagnostics.obj || exit /b 1
cl /nologo /O2 /MT /std:c++20 /utf-8 /EHsc /Isrc /c src\main.cpp /Fobuild\main.obj || exit /b 1

link /nologo /out:build\EurekaA4Emulator.exe ^
  build\main.obj build\machine.obj build\virtual_disk.obj build\text_codec.obj ^
  build\audio_player.obj build\diagnostics.obj build\z80.obj winmm.lib ole32.lib shell32.lib || exit /b 1

echo Vytvorene: build\EurekaA4Emulator.exe
