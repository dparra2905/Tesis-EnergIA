@echo off
setlocal EnableExtensions
REM tar.exe de Windows (bsdtar, sabe hacer zip): el de Git interpreta "C:" como un servidor remoto
set "TAR=%SystemRoot%\System32\tar.exe"
REM ==========================================================================================
REM obtener_modelos.bat - Pone el Arbol de decision y el MLP en la carpeta bin del build, sin
REM compilarlos (no hace falta Visual Studio).
REM
REM Las DLL de modelos se compilan con MSVC (src\execution\models\compilar_dlls.bat) y, con libtorch, pesan unos
REM 300 MB: no van en el repositorio. Se publican en un GitHub Release como
REM EnergIA-modelos-<version>-windows-x64.zip (lo arma scripts\desplegar_windows.bat), con las
REM DLL de los modelos, las de libtorch y el runtime de Visual C++.
REM
REM Uso:  scripts\obtener_modelos.bat [zip_de_modelos] [carpeta_bin]
REM   zip_de_modelos  por defecto: el EnergIA-modelos-*-windows-x64.zip mas reciente de la carpeta
REM                   Descargas; si no hay y esta GitHub CLI (gh) con sesion, lo descarga del
REM                   ultimo Release
REM   carpeta_bin     por defecto: toda carpeta build\*\bin que tenga energia_orquestador.exe
REM                   (compila primero el proyecto en Qt Creator)
REM ui.exe debe estar cerrado mientras se copian las DLL.
REM ==========================================================================================

set "RAIZ=%~dp0.."
set "REPO=LSDVillax923/Una-Humilde-Tesis"
set "ZIP=%~1"
set "BIN=%~2"

REM ---- 1. El zip de modelos
if not "%ZIP%"=="" goto :zip_elegido
for /f "delims=" %%F in ('dir /b /o-d "%USERPROFILE%\Downloads\EnergIA-modelos-*-windows-x64.zip" 2^>nul') do (
    set "ZIP=%USERPROFILE%\Downloads\%%F"
    goto :zip_elegido
)
where gh >nul 2>&1
if errorlevel 1 goto :sin_zip
echo == Descargando el zip de modelos del ultimo Release de %REPO% (gh)
if not exist "%RAIZ%\descargas" mkdir "%RAIZ%\descargas"
gh release download --repo %REPO% --pattern "EnergIA-modelos-*-windows-x64.zip" --dir "%RAIZ%\descargas" --clobber
if errorlevel 1 goto :sin_zip
for /f "delims=" %%F in ('dir /b /o-d "%RAIZ%\descargas\EnergIA-modelos-*-windows-x64.zip" 2^>nul') do (
    set "ZIP=%RAIZ%\descargas\%%F"
    goto :zip_elegido
)
:sin_zip
echo No se encontro el zip de modelos.
echo Descarga EnergIA-modelos-...-windows-x64.zip del ultimo Release:
echo   https://github.com/%REPO%/releases
echo dejalo en tu carpeta Descargas y vuelve a ejecutar este script (o pasale la ruta del zip).
exit /b 1

:zip_elegido
if not exist "%ZIP%" (
    echo No existe %ZIP%.
    exit /b 1
)
echo == Zip de modelos: %ZIP%

REM ---- 2. Las carpetas bin donde copiarlo
set "COPIADAS=0"
if not "%BIN%"=="" (
    call :extraer "%BIN%"
    goto :resumen
)
for /d %%D in ("%RAIZ%\build\*") do (
    if exist "%%D\bin\energia_orquestador.exe" call :extraer "%%D\bin"
)

:resumen
if "%COPIADAS%"=="0" (
    echo No se encontro ninguna carpeta build\*\bin con energia_orquestador.exe.
    echo Compila primero el proyecto en Qt Creator, o indica la carpeta bin como segundo argumento.
    exit /b 1
)
echo.
echo Listo. Abre ui.exe: el Arbol de decision y el MLP aparecen en Nuevo experimento.
exit /b 0

:extraer
if not exist "%~1" (
    echo No existe la carpeta %~1.
    exit /b 1
)
"%TAR%" -x -f "%ZIP%" -C "%~1"
if errorlevel 1 (
    echo No se pudo descomprimir en %~1. Si ui.exe esta abierto, cierralo y vuelve a intentarlo.
    exit /b 1
)
echo Modelos copiados a %~1
set /a COPIADAS+=1
exit /b 0
