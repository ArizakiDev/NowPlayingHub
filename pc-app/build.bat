@echo off
cd /d "%~dp0NowPlayingHub"
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=None -o "%~dp0dist"
if errorlevel 1 (
  echo.
  echo ===== ECHEC de la compilation =====
  pause
  exit /b 1
)
echo.
echo ===== Termine : %~dp0dist\NowPlayingHub.exe =====
pause
