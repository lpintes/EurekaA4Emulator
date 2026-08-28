@echo off
setlocal
cd /d "%~dp0"

rem Zostavi testy a pusti vsetkych jedenast naraz. Su to samostatne procesy,
rem nic nezdielaju, takze paralelne bezia bez rizika. Wall time urcuje
rem najdlhsi z nich -- rezim kbd.
rem
rem Volanie:  run-tests.bat            (ROM z %A4ROM%, inak C:\b\a4rom.dmp)
rem           run-tests.bat C:\cesta\a4rom.dmp

if "%MINGW64%"=="" set "MINGW64=C:\msys64\mingw64"
set "PATH=%MINGW64%\bin;%PATH%"

if not "%~1"=="" set "A4ROM=%~1"
if "%A4ROM%"=="" set "A4ROM=C:\b\a4rom.dmp"
if not exist "%A4ROM%" (
  echo Nenasiel som ROM: %A4ROM%
  echo Zadajte ju argumentom alebo premennou A4ROM.
  exit /b 1
)

call "%~dp0build-tests.bat"
if errorlevel 1 exit /b 1

rem Vlastny priecinok diskety, cerstvy pri kazdom spusteni. Skutocny disk sa
rem na to pouzivat neda, meni sa pod rukami -- a testy donho zapisuju.
rem Cesta sa sklada z %~dp0, takze nikdy neukazuje mimo repozitara.
set "TESTDISK=%~dp0build\testdisk"
if exist "%TESTDISK%" rmdir /s /q "%TESTDISK%"
mkdir "%TESTDISK%"
if errorlevel 1 exit /b 1

rem Rezim com bez tohto zlyha, a nie je to jeho chyba.
copy /y "%~dp0eurekatech\TECHMAN1\READ.COM" "%TESTDISK%\READ.COM" >nul
if errorlevel 1 (
  echo Nepodarilo sa pripravit disketu: chyba eurekatech\TECHMAN1\READ.COM
  exit /b 1
)

if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

rem -k dobehne aj po prvom zlyhani, nech je vidiet vsetky naraz.
rem --output-sync=target drzi vypis kazdeho testu pohromade; bez neho sa
rem riadky jedenastich procesov premiesaju a vysledok sa neda precitat.
echo.
echo === Testy ===
mingw32-make -j%JOBS% -k --output-sync=target MINGW64="%MINGW64:\=/%" ROM="%A4ROM:\=/%" DISK="%TESTDISK:\=/%" check
if errorlevel 1 (
  echo.
  echo VYSLEDOK: niektore testy ZLYHALI, viz riadky vyssie.
  exit /b 1
)

echo.
echo VYSLEDOK: vsetky testy presli.
