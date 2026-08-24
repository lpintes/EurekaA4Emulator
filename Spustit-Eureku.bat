@echo off
cd /d "%~dp0"
rem Po zostaveni je EXE v bin\; v distribuovanom balicku lezi vedla tohto skriptu.
if exist bin\EurekaA4Emulator.exe (
  "%~dp0bin\EurekaA4Emulator.exe" %*
) else (
  "%~dp0EurekaA4Emulator.exe" %*
)
