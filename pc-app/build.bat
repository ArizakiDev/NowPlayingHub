@echo off
REM Compile l'appli en un seul .exe autonome (necessite le SDK .NET 8 : https://dotnet.microsoft.com/download)
cd /d "%~dp0NowPlayingHub"
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o "%~dp0dist"
echo.
echo ===== Termine : %~dp0dist\NowPlayingHub.exe =====
pause
