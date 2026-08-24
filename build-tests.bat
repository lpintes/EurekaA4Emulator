@echo off
setlocal
cd /d "%~dp0"

if "%MINGW64%"=="" set "MINGW64=C:\msys64\mingw64"
set "PATH=%MINGW64%\bin;%PATH%"

rem Testy sa linkuju proti tym istym objektom ako emulator, preto sa
rem build.bat vola vzdy. Kedysi tu bola podmienka na existenciu
rem build\machine.o a bola to pasca: pri zmenenom zdrojaku nechala
rem stare .o, takze testy merali kod, ktory sa vobec neprelozil.
rem build.bat preklada vsetko nanovo, zastaraly objekt tak nevznikne.
call "%~dp0build.bat" || exit /b 1
if not exist bin mkdir bin

set "CXXFLAGS=-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc"
set "OBJS=build\machine.o build\virtual_disk.o build\diagnostics.o build\text_codec.o build\z80.o"
set "LDFLAGS=-static -static-libgcc -static-libstdc++"

rem codec_test ma obycajny main, preto bez -municode; ostatne maju wmain.
g++ %CXXFLAGS% %LDFLAGS% -o bin\codec_test.exe tests\codec_test.cpp build\text_codec.o || exit /b 1
g++ %CXXFLAGS% %LDFLAGS% -municode -o bin\diag_probe.exe tests\diag_probe.cpp %OBJS% || exit /b 1
g++ %CXXFLAGS% %LDFLAGS% -municode -o bin\integration_test.exe tests\integration_test.cpp %OBJS% || exit /b 1

echo Vytvorene: bin\codec_test.exe, bin\diag_probe.exe, bin\integration_test.exe
