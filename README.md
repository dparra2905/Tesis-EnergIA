# EnergIA

EnergIA es una herramienta de escritorio para Windows que mide el consumo de energía y el uso de recursos (CPU y RAM) de un equipo mientras entrena un modelo de Machine Learning y, si se activa, mientras hace inferencia con él. Es el sistema desarrollado como trabajo de grado.

El usuario elige un modelo, configura sus hiperparámetros, carga un dataset en CSV y lanza una corrida. EnergIA ejecuta el entrenamiento y, al mismo tiempo, registra dos fuentes de medición:

- un monitor físico de energía, el Atorch S1BW, que se lee por Bluetooth y entrega voltaje, corriente y potencia cada segundo;
- un monitor de software que toma el uso de CPU y RAM del sistema operativo.

Las dos fuentes y el progreso del entrenamiento se alinean en una rejilla común de 1 s. Cada corrida guarda una línea base de 20 s antes y después del entrenamiento (configurable), para poder separar el consumo del equipo en reposo. Los resultados se consultan, comparan y exportan desde la interfaz, y se pueden subir a una base de datos en la nube (Supabase) con cuenta de usuario.

Los modelos se integran como DLL que cumplen el contrato de EnergIA (versión 4). Hoy vienen dos: un árbol de decisión (mlpack) y una red neuronal MLP (libtorch, en CPU).

## Probarlo sin compilar

1. En la pestaña **Releases** de este repositorio, descarga `EnergIA-<versión>-windows-x64.zip`.
2. Descomprímelo y abre `EnergIA\ui.exe`.
3. Crea una cuenta en la pantalla de acceso, o entra con «Continuar sin conexión».

El paquete trae todo lo necesario: Qt, los dos modelos con libtorch, el runtime de Visual C++ y un Python portátil para el monitor de energía. No hay que instalar nada. Sin el Atorch conectado se miden solo CPU y RAM. La primera vez Windows puede mostrar «Windows protegió su PC» porque los ejecutables no están firmados: **Más información → Ejecutar de todas formas**.

## Compilarlo

Requisitos (versiones con las que se probó):

| Qué | Para qué |
|---|---|
| Windows 10 u 11 de 64 bits | Todo el sistema |
| Qt 6.11.2 con kit MinGW 64 bits, CMake y Qt Creator | Interfaz, orquestador, monitor de software y pruebas |
| Visual Studio 2022 o sus Build Tools (C++ y CMake) y Git | DLL del árbol y del MLP |
| Python 3.13 | Monitor de energía desde el código fuente |
| Atorch S1BW y Bluetooth | Medir energía |

Pasos:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build --parallel
cd build && ctest --output-on-failure
```

También se puede abrir el `CMakeLists.txt` de la raíz en Qt Creator. Los ejecutables quedan en `build/<kit>/bin/`. Para tener los modelos reales en esa carpeta hay dos caminos: compilarlos con `src\execution\models\compilar_dlls.bat` (la primera vez tarda entre 15 y 40 minutos) o descargar el zip de modelos del Release y ejecutar `scripts\obtener_modelos.bat`.

## Estructura del repositorio

```
├── src/
│   ├── ui/                   Interfaz (ui.exe, QML)
│   ├── experiment_manager/   Orquestador de la corrida (energia_orquestador.exe)
│   ├── execution/            Contrato de DLL v4, carga de modelos y datos
│   │   └── models/           Árbol de decisión, MLP y DLL de prueba
│   ├── monitoring/           Monitor de software (system_meter.exe) y de energía (energia_ble.py)
│   ├── sync/                 Fases de la corrida, energía y auditoría
│   ├── storage/              Carpetas de las corridas
│   └── analysis/             Estadísticas y rejilla de 1 s
├── database/                 Esquema de Supabase y migraciones
├── config/                   laboratorio.json (MAC del Atorch y rutas)
├── scripts/                  Empaquetado del Release y descarga de modelos
└── tests/                    Pruebas unitarias, de integración y de QML
```

El prototipo de la interfaz está en [Figma](https://www.figma.com/proto/tc09oELKRYrAoXV1vaOiIG/Nocta-CM?node-id=0-1&t=zbAFZOscCmBsF8Mv-1).

## Equipo

Trabajo de grado de [PENDIENTE: programa y universidad].

| Rol | Nombre |
|---|---|
| Autores | David Parra · Diego Andrés Villabón Correa · Mateo Maldonado|
| Directora | Mariela Curiel |

## Origen del código

 El monitor de energía (`energia_ble.py`) se reescribió a partir de `atorch_ble_capture.py` de Juan Diego Peña ([proactive-autoscaling-dl-models](https://github.com/JuanDPenaM/proactive-autoscaling-dl-models)). Los archivos que vienen de esas fuentes lo indican en su cabecera.
