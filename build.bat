@echo off
rem Builds bin\dinput8.dll (the DK mod loader) and bin\DK_Blaster.dll with the compiler and linker flags of the
rem released files. Needs Visual Studio 2022 or its Build Tools with "Desktop development with C++" (MSVC, x86).
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%bin"
set "S=%ROOT%src"
set "C=%ROOT%src\DK_Common"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found: install Visual Studio 2022 or its Build Tools & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo No Visual Studio with the C++ tools found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%OUT%\obj_ld" mkdir "%OUT%\obj_ld"
if not exist "%OUT%\obj_bl" mkdir "%OUT%\obj_bl"

rem DK mod loader: dinput8.dll
del /q "%OUT%\dinput8.dll" 2>nul
cl /nologo /O2 /MT /W3 /GS- /EHsc /Zi /DNDEBUG /DWIN32_LEAN_AND_MEAN /I"%C%" /Fo"%OUT%\obj_ld\\" /Fd"%OUT%\obj_ld\\" "%S%\DK_Loader\dk_loader.cpp" "%S%\DK_Loader\dk_services.cpp" "%C%\dk_util.cpp" /Fe"%OUT%\dinput8.dll" /link /DLL /NOLOGO /DEBUG /PDBALTPATH:%%_PDB%% /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEF:"%S%\DK_Loader\dinput8.def" /PDB:"%OUT%\dinput8.pdb" | findstr /i /c:"error" /c:"warning"
if not exist "%OUT%\dinput8.dll" (echo BUILD FAILED: dinput8.dll & exit /b 1)

rem Blaster Combat: DK_Blaster.dll
del /q "%OUT%\DK_Blaster.dll" 2>nul
set "B=%S%\DK_Blaster"
cl /nologo /O2 /MT /W4 /GS- /EHsc /Zi /DNDEBUG /DWIN32_LEAN_AND_MEAN /I"%C%" /Fo"%OUT%\obj_bl\\" /Fd"%OUT%\obj_bl\\" "%B%\dkb_main.cpp" "%B%\dkb_game.cpp" "%B%\dkb_cam.cpp" "%B%\dkb_input.cpp" "%B%\dkb_combat.cpp" "%B%\dkb_hud.cpp" "%B%\dkb_luamod.cpp" "%B%\dkb_render.cpp" "%B%\dkb_sound.cpp" "%B%\dkb_pack.cpp" "%B%\dkb_ability.cpp" "%B%\dkb_outfit.cpp" "%B%\dkb_hold.cpp" "%B%\dkb_crouch.cpp" "%B%\dkb_aim.cpp" "%C%\dk_util.cpp" "%C%\dk_lua.cpp" "%C%\dk_pack.cpp" "%C%\dk_costume.cpp" /Fe"%OUT%\DK_Blaster.dll" /link /DLL /NOLOGO /DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO /PDB:"%OUT%\DK_Blaster.pdb" /PDBALTPATH:%%_PDB%% user32.lib | findstr /i /c:"error" /c:"warning"
if not exist "%OUT%\DK_Blaster.dll" (echo BUILD FAILED: DK_Blaster.dll & exit /b 1)
echo BUILD OK: bin\dinput8.dll, bin\DK_Blaster.dll
exit /b 0
