@echo off
setlocal
cd /d "%~dp0"

rem mingw64 z msys2. Vlastny prefix ide na zaciatok PATH -- 32-bitovy cc1
rem s 64-bitovymi DLL po ceste zomiera bez jedineho slova, viz CLAUDE.md.
if "%MINGW64%"=="" set "MINGW64=C:\msys64\mingw64"
set "PATH=%MINGW64%\bin;%PATH%"

if not exist "%MINGW64%\bin\g++.exe" (
  echo Nenasiel som g++ v %MINGW64%\bin. Nastavte premennu MINGW64.
  exit /b 1
)

if not exist build mkdir build
if not exist bin mkdir bin

set "CWARN=-Wall -Wextra -Wno-unused-parameter"
set "CXXFLAGS=-std=c++20 -O2 %CWARN% -Isrc -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00"

rem z80.c je C, nie C++.
gcc -std=c11 -O2 %CWARN% -Isrc -c src\z80.c -o build\z80.o || exit /b 1

for %%f in (machine virtual_disk text_codec audio_player diagnostics main) do (
  g++ %CXXFLAGS% -c src\%%f.cpp -o build\%%f.o || exit /b 1
)

rem -municode kvoli wmain, staticke runtime kniznice preto, aby EXE bezalo
rem aj mimo msys2 shellu.
g++ -municode -static -static-libgcc -static-libstdc++ -s ^
  -o bin\EurekaA4Emulator.exe ^
  build\main.o build\machine.o build\virtual_disk.o build\text_codec.o ^
  build\audio_player.o build\diagnostics.o build\z80.o ^
  -lwinmm -lole32 -lshell32 -luuid || exit /b 1

echo Vytvorene: bin\EurekaA4Emulator.exe
