@echo off
setlocal

set "PLUGIN_ROOT=%~dp0"
set "PLUGIN_FILE=%PLUGIN_ROOT%DLLMassUnits.uplugin"
set "OUTPUT_PATH=%~1"
set "TARGET_PLATFORM=%~2"
set "BUILD_CONFIG=%~3"

if not defined OUTPUT_PATH set "OUTPUT_PATH=%PLUGIN_ROOT%Build\Package"
if not defined TARGET_PLATFORM set "TARGET_PLATFORM=Win64"
if not defined BUILD_CONFIG set "BUILD_CONFIG=Development"

set "ENGINE_ROOT=%UE_ENGINE_PATH%"
if not defined ENGINE_ROOT set "ENGINE_ROOT=%UE_PATH%"

if not defined ENGINE_ROOT (
    echo Error: Set UE_ENGINE_PATH to your Unreal Engine root.
    echo Example: set UE_ENGINE_PATH=D:\Unreal Engine\UE_5.7
    exit /b 1
)

set "UAT=%ENGINE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
if not exist "%UAT%" set "UAT=%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat"

if not exist "%UAT%" (
    echo Error: RunUAT.bat was not found below "%ENGINE_ROOT%".
    exit /b 1
)

if not exist "%PLUGIN_FILE%" (
    echo Error: Plugin descriptor was not found at "%PLUGIN_FILE%".
    exit /b 1
)

echo Building "%PLUGIN_FILE%"
echo Target: %TARGET_PLATFORM% %BUILD_CONFIG%
echo Package: "%OUTPUT_PATH%"

call "%UAT%" BuildPlugin -Plugin="%PLUGIN_FILE%" -Package="%OUTPUT_PATH%" -TargetPlatforms=%TARGET_PLATFORM% -Configuration=%BUILD_CONFIG%
set "BUILD_RESULT=%ERRORLEVEL%"

if not "%BUILD_RESULT%"=="0" (
    echo Build failed with exit code %BUILD_RESULT%.
    exit /b %BUILD_RESULT%
)

echo Build completed successfully: "%OUTPUT_PATH%"
exit /b 0
