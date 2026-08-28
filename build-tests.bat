@echo off
setlocal
cd /d "%~dp0"

if "%MINGW64%"=="" set "MINGW64=C:\msys64\mingw64"
set "PATH=%MINGW64%\bin;%PATH%"

if not exist "%MINGW64%\bin\mingw32-make.exe" (
  echo Nenasiel som mingw32-make v %MINGW64%\bin. Nastavte premennu MINGW64.
  exit /b 1
)

if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

rem Ciel `tests` obsahuje aj emulator, aby jedno spustenie tejto davky dalo
rem hotovy cely strom tak, ako to robila predtym.
rem
rem Kedysi tu bola podmienka na existenciu build\machine.o a bola to pasca:
rem pri zmenenom zdrojaku nechala stare .o, takze testy merali kod, ktory sa
rem vobec neprelozil. Riesilo sa to prekladom vsetkeho pri kazdom spusteni.
rem Teraz to riesi make cez .d subory z gcc -MMD: vie o kazdej hlavicke, na
rem ktorej ktory objekt zavisi, takze zastaraly objekt nevznikne a pritom sa
rem nepreklada viac, nez treba.
mingw32-make -j%JOBS% MINGW64="%MINGW64:\=/%" tests
if errorlevel 1 exit /b 1

echo Vytvorene: bin\codec_test.exe, bin\disk_test.exe, bin\settings_test.exe, bin\diag_probe.exe, bin\integration_test.exe
