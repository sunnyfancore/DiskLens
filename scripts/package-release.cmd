@echo off
setlocal
set "DISKLENS_SCRIPT_ROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { $scriptPath = $args[0]; $scriptArgs = @(); if ($args.Count -gt 1) { $scriptArgs = $args[1..($args.Count - 1)] }; $code = [System.IO.File]::ReadAllText($scriptPath, [System.Text.Encoding]::UTF8); $block = [ScriptBlock]::Create($code); & $block @scriptArgs }" "%~dp0package-release.ps1" %*
exit /b %ERRORLEVEL%
