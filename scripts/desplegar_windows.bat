@echo off
setlocal EnableExtensions
REM tar.exe de Windows (bsdtar, sabe hacer zip): el de Git interpreta "C:" como un servidor remoto
set "TAR=%SystemRoot%\System32\tar.exe"
REM ==========================================================================================
REM desplegar_windows.bat - Arma EnergIA listo para usar sin compilar nada (D-011).
REM
REM Deja en la carpeta de destino:
REM   - los ejecutables y las DLL de modelos ya compilados (carpeta bin del build), con libtorch;
REM   - Qt, su modulo QML, el runtime de MinGW y el plugin TLS de Qt (windeployqt). Sin el plugin
REM     TLS, ui.exe no puede hablar con la base de datos por HTTPS en un PC sin OpenSSL;
REM   - el runtime de Visual C++ (las DLL de modelos y libtorch se compilan con MSVC), para que
REM     no haya que instalar el Visual C++ Redistributable;
REM   - el medidor de energia (medidores\, con rutas relativas en laboratorio.json) con un Python
REM     portatil (medidores\python: el paquete embeddable oficial de python.org con bleak ya
REM     instalado), para que el PC de destino no tenga que instalar Python ni crear un entorno.
REM Y junto a la carpeta, los dos archivos que se publican en un GitHub Release:
REM   EnergIA-<version>-windows-x64.zip          la aplicacion completa (para quien solo la usa)
REM   EnergIA-modelos-<version>-windows-x64.zip  solo modelos, libtorch y runtime de Visual C++
REM                                              (para quien clona y compila; ver obtener_modelos.bat)
REM
REM Uso:  scripts\desplegar_windows.bat [carpeta_build] [carpeta_destino]
REM   carpeta_build    la de CMake (por defecto build\Desktop_Qt_6_11_2_MinGW_64_bit_Debug)
REM   carpeta_destino  por defecto dist\EnergIA (se reemplaza)
REM Variables opcionales:
REM   QT_DIR           carpeta de Qt MinGW (por defecto C:\Qt\6.11.2\mingw_64)
REM   ENERGIA_VERSION  version del paquete (por defecto, la de git describe)
REM   VCREDIST_CRT     carpeta Microsoft.VC14x.CRT (por defecto, la de Visual Studio con vswhere)
REM   PYTHON_HOST      comando del Python de ESTE equipo con pip, solo para descargar bleak
REM                    (por defecto python; p. ej. "py -3.13")
REM
REM Para armar el paquete hace falta internet la primera vez (el zip de Python se guarda en
REM descargas\ y se comprueba su SHA-256) y un Python con pip en este equipo. En el PC de destino
REM no hay que instalar nada: se descomprime y se abre ui.exe.
REM ==========================================================================================

set "RAIZ=%~dp0.."
set "BUILD=%~1"
if "%BUILD%"=="" set "BUILD=%RAIZ%\build\Desktop_Qt_6_11_2_MinGW_64_bit_Debug"
set "DESTINO=%~2"
if "%DESTINO%"=="" set "DESTINO=%RAIZ%\dist\EnergIA"
if not defined QT_DIR set "QT_DIR=C:\Qt\6.11.2\mingw_64"
set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
set "BIN=%BUILD%\bin"

if not defined ENERGIA_VERSION (
    for /f "delims=" %%V in ('git -C "%RAIZ%" describe --tags --always 2^>nul') do set "ENERGIA_VERSION=%%V"
)
if not defined ENERGIA_VERSION set "ENERGIA_VERSION=sin-version"

if not exist "%BIN%\ui.exe" (
    echo No se encontro %BIN%\ui.exe. Compila el proyecto primero o indica la carpeta del build.
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo No se encontro windeployqt en %WINDEPLOYQT%. Define QT_DIR con la carpeta de Qt MinGW.
    exit /b 1
)
REM windeployqt usa el compilador de Qt para el runtime de MinGW
set "PATH=%QT_DIR%\bin;%QT_DIR%\..\..\Tools\mingw1310_64\bin;%PATH%"

echo == EnergIA %ENERGIA_VERSION%: copiando ejecutables y DLL de modelos a %DESTINO%
if exist "%DESTINO%" rmdir /s /q "%DESTINO%"
mkdir "%DESTINO%"
REM Sin las pruebas, la DLL de prueba ni los simbolos de depuracion
robocopy "%BIN%" "%DESTINO%" /E /XF test_*.exe EnergIA_Prueba.dll *.pdb *.ilk /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo Fallo la copia de %BIN%.
    exit /b 1
)
if not exist "%DESTINO%\EnergIA_Arbol.dll" echo AVISO: falta EnergIA_Arbol.dll en %BIN% (ejecuta src\execution\models\compilar_dlls.bat).
if not exist "%DESTINO%\EnergIA_MLP.dll" echo AVISO: falta EnergIA_MLP.dll en %BIN% (ejecuta src\execution\models\compilar_dlls.bat).

echo == Agregando Qt (windeployqt)
"%WINDEPLOYQT%" --qmldir "%RAIZ%\src\ui\qml" --compiler-runtime --no-translations "%DESTINO%\ui.exe" >nul
if errorlevel 1 (
    echo Fallo windeployqt para ui.exe.
    exit /b 1
)
"%WINDEPLOYQT%" --compiler-runtime --no-translations "%DESTINO%\energia_orquestador.exe" >nul
if errorlevel 1 (
    echo Fallo windeployqt para energia_orquestador.exe.
    exit /b 1
)

REM HTTPS: el backend Schannel viene con Windows; se asegura aunque windeployqt no lo copie
if not exist "%DESTINO%\tls\qschannelbackend.dll" (
    mkdir "%DESTINO%\tls" 2>nul
    copy /y "%QT_DIR%\plugins\tls\qschannelbackend.dll" "%DESTINO%\tls\" >nul
)
if not exist "%DESTINO%\tls\qschannelbackend.dll" (
    echo Falta el plugin TLS de Qt: ui.exe no podria conectarse a la base de datos.
    exit /b 1
)

echo == Runtime de Visual C++ (para las DLL de modelos y libtorch)
if not defined VCREDIST_CRT (
    for /f "usebackq delims=" %%C in (`powershell -NoProfile -Command "$w = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'; if (Test-Path $w) { $vs = & $w -latest -prerelease -products * -property installationPath; if ($vs) { Get-ChildItem -Directory -Path (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT') -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName } }"`) do set "VCREDIST_CRT=%%C"
)
if defined VCREDIST_CRT if exist "%VCREDIST_CRT%\msvcp140.dll" (
    copy /y "%VCREDIST_CRT%\*.dll" "%DESTINO%\" >nul
    echo Copiado desde %VCREDIST_CRT%
    goto :runtime_listo
)
echo AVISO: no se encontro el runtime de Visual C++ (define VCREDIST_CRT). En un PC sin el
echo Visual C++ Redistributable x64, el Arbol y el MLP no apareceran en la lista de modelos.
:runtime_listo

echo == Medidor de energia (medidores\)
REM Sin crear_entorno.bat: el paquete trae su propio Python (abajo), no hace falta un venv
robocopy "%RAIZ%\src\monitoring\hardware_monitor" "%DESTINO%\medidores" energia_ble.py requirements.txt /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo Fallo la copia del medidor de energia.
    exit /b 1
)

echo == Python portatil para el medidor (medidores\python)
REM Paquete embeddable oficial de python.org. Al cambiar de version: actualizar PY_VERSION, PY_PTH
REM (python3XY._pth) y PY_SHA256 (el de la pagina de la version en python.org)
set "PY_VERSION=3.13.17"
set "PY_PTH=python313._pth"
set "PY_SHA256=9E35319BFFEDAB8B36AD44D8F377CB4F1733BCEC257A0B6D04D94F01486DDB38"
set "PY_ZIP_NOMBRE=python-%PY_VERSION%-embed-amd64.zip"
set "PY_URL=https://www.python.org/ftp/python/%PY_VERSION%/%PY_ZIP_NOMBRE%"
set "DIR_DESCARGAS=%RAIZ%\descargas"
set "PY_ZIP=%DIR_DESCARGAS%\%PY_ZIP_NOMBRE%"
set "DIR_PY=%DESTINO%\medidores\python"
if not defined PYTHON_HOST set "PYTHON_HOST=python"

if exist "%PY_ZIP%" goto :python_descargado
if not exist "%DIR_DESCARGAS%" mkdir "%DIR_DESCARGAS%"
echo Descargando %PY_ZIP_NOMBRE% de python.org...
powershell -NoProfile -Command "$ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%PY_URL%' -OutFile '%PY_ZIP%.parcial'"
if errorlevel 1 goto :python_sin_descarga
move /y "%PY_ZIP%.parcial" "%PY_ZIP%" >nul
:python_descargado
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 '%PY_ZIP%').Hash -ne '%PY_SHA256%') { exit 1 }"
if errorlevel 1 (
    echo El SHA-256 de %PY_ZIP% no es el esperado: borralo y vuelve a ejecutar este script.
    exit /b 1
)
mkdir "%DIR_PY%"
"%TAR%" -x -f "%PY_ZIP%" -C "%DIR_PY%"
if errorlevel 1 (
    echo No se pudo descomprimir %PY_ZIP%.
    exit /b 1
)
REM El ._pth fija sys.path (ignora PYTHONPATH y el Python instalado en el PC): la libreria estandar,
REM la carpeta de python.exe y Lib\site-packages, donde se instalan bleak y sus dependencias
> "%DIR_PY%\%PY_PTH%" (
    echo python313.zip
    echo .
    echo Lib\site-packages
    echo import site
)
REM bleak y winrt con el pip de este equipo, como ruedas para Windows x64 y Python 3.13
%PYTHON_HOST% -m pip install --disable-pip-version-check --no-warn-script-location --quiet ^
    --only-binary=:all: --platform win_amd64 --python-version 3.13 --implementation cp ^
    --target "%DIR_PY%\Lib\site-packages" -r "%RAIZ%\src\monitoring\hardware_monitor\requirements.txt"
if errorlevel 1 (
    echo No se pudieron instalar las dependencias del medidor con "%PYTHON_HOST% -m pip".
    echo Hace falta internet y un Python con pip en este equipo; define PYTHON_HOST si no es "python".
    exit /b 1
)
REM Comprobacion con el Python del paquete, sin el PATH de este equipo
setlocal
set "PATH=%SystemRoot%\System32;%SystemRoot%"
"%DIR_PY%\python.exe" -c "import bleak, winrt.windows.devices.bluetooth, importlib.metadata as m; print('   Python', __import__('platform').python_version(), '- bleak', m.version('bleak'))"
if errorlevel 1 (
    endlocal
    echo El Python portatil no puede importar bleak.
    exit /b 1
)
endlocal
goto :python_listo
:python_sin_descarga
if exist "%PY_ZIP%.parcial" del /q "%PY_ZIP%.parcial"
echo No se pudo descargar %PY_URL%
echo Descargalo a mano y guardalo como %PY_ZIP%
exit /b 1
:python_listo

REM Rutas relativas a la carpeta de laboratorio.json (main.cpp las resuelve); se conserva la MAC
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$p = '%DESTINO%\laboratorio.json';" ^
  "$j = Get-Content -Raw -Encoding UTF8 $p | ConvertFrom-Json;" ^
  "$j.python_medidor_energia = 'medidores\python\python.exe';" ^
  "$j.script_medidor_energia = 'medidores\energia_ble.py';" ^
  "[IO.File]::WriteAllText($p, ($j | ConvertTo-Json))"
if errorlevel 1 (
    echo No se pudo ajustar laboratorio.json.
    exit /b 1
)
> "%DESTINO%\VERSION.txt" echo EnergIA %ENERGIA_VERSION%

echo == Paquetes para el GitHub Release
for %%D in ("%DESTINO%\..") do set "DIR_PAQUETES=%%~fD"
set "ZIP_APP=%DIR_PAQUETES%\EnergIA-%ENERGIA_VERSION%-windows-x64.zip"
set "ZIP_MODELOS=%DIR_PAQUETES%\EnergIA-modelos-%ENERGIA_VERSION%-windows-x64.zip"
if exist "%ZIP_APP%" del /q "%ZIP_APP%"
if exist "%ZIP_MODELOS%" del /q "%ZIP_MODELOS%"
for %%D in ("%DESTINO%") do set "NOMBRE_DESTINO=%%~nxD"
REM La aplicacion completa, dentro de una carpeta EnergIA\
"%TAR%" -a -c -f "%ZIP_APP%" -C "%DESTINO%\.." "%NOMBRE_DESTINO%"
if errorlevel 1 (
    echo No se pudo crear %ZIP_APP%.
    exit /b 1
)
REM Solo lo que no sale de compilar con Qt: modelos, libtorch y runtime de Visual C++
pushd "%DESTINO%"
set "ARCHIVOS_MODELOS="
for %%F in (EnergIA_Arbol.dll EnergIA_MLP.dll compilacion_dlls.txt VERSION.txt c10*.dll torch*.dll libiomp*.dll uv.dll shm.dll concrt140.dll msvcp140*.dll vccorlib140.dll vcruntime140*.dll) do (
    if exist "%%F" call set "ARCHIVOS_MODELOS=%%ARCHIVOS_MODELOS%% %%F"
)
if defined ARCHIVOS_MODELOS "%TAR%" -a -c -f "%ZIP_MODELOS%" %ARCHIVOS_MODELOS%
set "ERROR_TAR=%errorlevel%"
popd
if not "%ERROR_TAR%"=="0" (
    echo No se pudo crear %ZIP_MODELOS%.
    exit /b 1
)

echo.
echo Listo: %DESTINO%
echo Para publicar (GitHub, Releases, Draft a new release), adjunta:
echo   %ZIP_APP%
if exist "%ZIP_MODELOS%" echo   %ZIP_MODELOS%
echo Quien solo quiere usar EnergIA descarga el primero, lo descomprime y abre ui.exe.
echo Quien clona el repositorio ejecuta scripts\obtener_modelos.bat con el segundo.
endlocal
