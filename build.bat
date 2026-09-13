@echo off

dotnet run --project "%~dp0build\EasyBuild.fsproj" -- %*
