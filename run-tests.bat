@echo off
setlocal
cd /d "%~dp0"

rem Zostavi testy a pusti vsetkych sedemnast naraz. Su to samostatne procesy,
rem nic nezdielaju, takze paralelne bezia bez rizika. Wall time urcuje
rem najdlhsi z nich -- rezim kbd.
rem
rem Volanie:  run-tests.bat            (ROM z %A4ROM%, inak C:\b\a4rom.dmp)
rem           run-tests.bat C:\cesta\a4rom.dmp
rem
rem Technical Manual sa hlada v %EUREKATECH%, inak v C:\b\eurekatech. Je to
rem material tretich stran a v repozitari nie je -- vid ROM-NOTICE.txt. Bez
rem neho sa preskocia rezimy com a wp a testov je patnast, nie sedemnast.

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

rem Pravy READ.COM z vyvojarskej diskety potrebuju DVA rezimy: `com` ho
rem spusti a `wp` nim overuje, ze z chranenej diskety sa da citat. Ked manual
rem nie je po ruke, oba sa vynechaju cez SKIP_MODES -- zlyhanie by ukazovalo
rem na emulator, hoci chyba len subor, ktory sa nedistribuuje. Ze je v tom aj
rem `wp`, sa ukazalo az merania 20. 9. 2026; odhad hovoril, ze je to len com.
rem
rem SKIP_MODES ide do make PROSTREDIM, nie argumentom: su to dve slova a make
rem by to druhe vzal ako dalsi ciel.
if "%EUREKATECH%"=="" set "EUREKATECH=C:\b\eurekatech"
set "SKIP_MODES="
if exist "%EUREKATECH%\TECHMAN1\READ.COM" (
  copy /y "%EUREKATECH%\TECHMAN1\READ.COM" "%TESTDISK%\READ.COM" >nul
  if errorlevel 1 (
    echo Nepodarilo sa pripravit disketu z %EUREKATECH%\TECHMAN1\READ.COM
    exit /b 1
  )
) else (
  echo.
  echo Nenasiel som %EUREKATECH%\TECHMAN1\READ.COM, preskakujem com a wp.
  echo Je to subor z Technical Manualu, ktory lezi mimo repozitara.
  echo Cestu k nemu zadajte premennou EUREKATECH.
  set "SKIP_MODES=com wp"
)

if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

rem -k dobehne aj po prvom zlyhani, nech je vidiet vsetky naraz.
rem --output-sync=target drzi vypis kazdeho testu pohromade; bez neho sa
rem riadky sedemnastich procesov premiesaju a vysledok sa neda precitat.
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
