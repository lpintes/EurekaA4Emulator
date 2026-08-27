@echo off
cd /d "%~dp0"
rem Po zostaveni je EXE v bin\; v distribuovanom balicku lezi vedla tohto skriptu.
rem
rem "start" preto, aby cmd na emulator necakal. Je to program GUI subsystemu,
rem takze sam ziadnu konzolu neotvara -- ale bez "start" by tu cmd stal az do
rem jeho konca a okno tejto davky by zostalo visiet na obrazovke celu relaciu.
if exist bin\EurekaA4Emulator.exe (
  start "" "%~dp0bin\EurekaA4Emulator.exe" %*
) else (
  start "" "%~dp0EurekaA4Emulator.exe" %*
)
