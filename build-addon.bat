@echo off
rem Zostavi doplnok pre NVDA z nvda-addon\ do bin\eurekaA4Emulator.nvda-addon.
rem
rem Preco to nie je v Makefile: doplnok nema co prekladat ani na com zavisiet,
rem je to zip a jeden preklad .po -> .mo. Make by tu neriesil nic, len by pribudlo
rem miesto, kde sa da zabudnut, ze recepty musia bezat aj pod sh.exe.
rem
rem "build-addon.bat scratchpad" nakopiruje modul do vyvojoveho scratchpadu NVDA.
rem To je vyvojova slucka: NVDA ho nacita po restarte (NVDA+Q, alebo NVDA+Ctrl+F3
rem na znovunacitanie doplnkov) a nemusi sa nic instalovat.
setlocal
set "ROOT=%~dp0"
set "SRC=%ROOT%nvda-addon\addon"
set "STAGE=%ROOT%build\addon"
set "OUT=%ROOT%bin\eurekaA4Emulator.nvda-addon"
if "%MSGFMT%"=="" set "MSGFMT=C:\msys64\usr\bin\msgfmt.exe"

if /i "%~1"=="scratchpad" goto :scratchpad

if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy "%SRC%" "%STAGE%" /s /i /q >nul
if errorlevel 1 goto :fail
rem Bajtkod z vyvoja by sa inak zabalil so zdrojakom -- vlastny import modulu
rem pri skusani nechava v appModules\__pycache__ .pyc, a to do balicka nepatri.
for /d /r "%STAGE%" %%d in (__pycache__) do if exist "%%d" rmdir /s /q "%%d"
if not exist "%ROOT%bin" mkdir "%ROOT%bin"

rem Preklad hlasok. Do balicka ide .mo, .po je zdroj a v nom nema co robit.
if not exist "%MSGFMT%" goto :nomsgfmt
"%MSGFMT%" --check -o "%STAGE%\locale\sk\LC_MESSAGES\nvda.mo" "%SRC%\locale\sk\LC_MESSAGES\nvda.po"
if errorlevel 1 goto :fail
goto :pack

:nomsgfmt
echo VAROVANIE: msgfmt sa nenasiel (%MSGFMT%), doplnok bude bez prekladu.
echo Je v msys2 v usr\bin; inu cestu podstrcite premennou MSGFMT.

:pack
del /q "%STAGE%\locale\sk\LC_MESSAGES\nvda.po" 2>nul
if exist "%OUT%" del /q "%OUT%"
rem CreateFromDirectory, nie Compress-Archive: ten v PowerShelli 5.1 zapisuje
rem oddelovac ciest podla Windows a rozbalovace to potom neprecitaju spravne.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::CreateFromDirectory('%STAGE%', '%OUT%')"
if errorlevel 1 goto :fail
echo Hotovo: %OUT%
goto :eof

:scratchpad
set "PAD=%APPDATA%\nvda\scratchpad\appModules"
if not exist "%PAD%" mkdir "%PAD%"
copy /y "%SRC%\appModules\eurekaa4emulator.py" "%PAD%\" >nul
if errorlevel 1 goto :fail
echo Skopirovane do %PAD%.
echo Zapnite v NVDA Nastavenia - Rozsirene - Nacitat vlastny kod z vyvojoveho
echo priecinka scratchpad, a NVDA restartujte.
goto :eof

:fail
echo ZLYHALO.
exit /b 1
