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
if not exist "%MINGW64%\bin\mingw32-make.exe" (
  echo Nenasiel som mingw32-make v %MINGW64%\bin. Nastavte premennu MINGW64.
  exit /b 1
)

rem Sedem prekladovych jednotiek je na sebe nezavislych, takze sa prekladaju
rem naraz. Na styroch jadrach je to zhruba dvojnasobok rychlosti oproti
rem seriovemu prekladu. Vlastny pocet sa da vnutit premennou JOBS.
if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

rem Cesta ide do make s lomkami dopredu: make si podla PATH moze vybrat sh
rem a ten by backslashe zral ako escape sekvencie.
mingw32-make -j%JOBS% MINGW64="%MINGW64:\=/%" all
if errorlevel 1 exit /b 1

echo Vytvorene: bin\EurekaA4Emulator.exe
