@echo off
rem DK Blaster Combat: removes the mod and the files it made. Run it in the game folder (next to SWTFU.exe).
rem dinput8.dll (the DK loader) stays: other DK mods use it. Delete it by hand if no DK mod is left.
cd /d "%~dp0"
if not exist SWTFU.exe (echo Put this file in the game folder, next to SWTFU.exe. & pause & exit /b 1)
rem the Windows tools by full path: a Unix find.exe earlier on PATH (Git, MSYS, Cygwin) would let this check pass
"%SystemRoot%\System32\tasklist.exe" /NH /FI "IMAGENAME eq SWTFU.exe" 2>nul | "%SystemRoot%\System32\find.exe" /I "SWTFU.exe" >nul && (echo Close the game first. & pause & exit /b 1)
for %%f in (DK_Blaster.dll DK_Blaster.ini DK_Blaster_Classes.ini DK_Blaster.log DK_Blaster.old.log DK_Blaster_README.txt DK_ModOptions\DK_Blaster.ini) do if exist "%%f" del /q "%%f"
del /q LevelPacks\dk_blaster.* LevelPacks\dk_blaster_outfits.dkver 2>nul
for %%f in (dk_hothrebelsoldier dk_hothrebelelite dk_hothrebelpilot dk_hothrebelsniper dk_tatweequay dk_tattuskensniper) do if exist "LevelPacks\%%f.actor.xml_pc.lp" del /q "LevelPacks\%%f.actor.xml_pc.lp"
echo DK Blaster Combat removed.
pause
(goto) 2>nul & del /q "%~f0"
