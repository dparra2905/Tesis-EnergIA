#!/usr/bin/env python3
"""Pruebas de energia_ble.py que no necesitan el Atorch ni Bluetooth (F6).

- decodificar(): una trama armada a mano con el mapa de campos documentado en el script
  (DATOS DE PRUEBA, no es una trama capturada del Atorch) y tramas invalidas;
- formato de tiempo ISO UTC con milisegundos y lectura de t_start;
- linea de comandos: MAC invalida y falta de 'bleak' terminan con el codigo 4 (configuracion).

La captura real (conexion BLE, rejilla, energia_lista.flag) solo se puede verificar con el
Atorch S1BW fisico.

Uso: python test_energia_ble.py --script <energia_ble.py>
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

FALLOS = 0
# Salida de los medidores en UTF-8, como los lanza el orquestador
ENTORNO = {**os.environ, "PYTHONIOENCODING": "utf-8"}


def comprobar(condicion: bool, descripcion: str) -> None:
    global FALLOS
    print(f"[{'OK  ' if condicion else 'FALLA'}] {descripcion}", flush=True)
    if not condicion:
        FALLOS += 1


def trama_de_prueba(voltaje_dv: int, corriente_ma: int, potencia_dw: int, energia_10wh: int) -> bytes:
    """Trama armada con el mapa de campos del script (big-endian, sin signo)."""
    t = bytearray(b"\xff\x55\x01\x01")
    t += voltaje_dv.to_bytes(3, "big")      # offset 4, escala 0,1 V
    t += corriente_ma.to_bytes(3, "big")    # offset 7, escala 0,001 A
    t += potencia_dw.to_bytes(3, "big")     # offset 10, escala 0,1 W
    t += energia_10wh.to_bytes(4, "big")    # offset 13, escala 0,01 kWh
    t += b"\x00\x00\x00"                    # relleno, como en las tramas reales (largo > 17)
    return bytes(t)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--script", required=True, type=Path)
    args = p.parse_args()
    script = args.script.resolve()

    spec = importlib.util.spec_from_file_location("energia_ble", script)
    modulo = importlib.util.module_from_spec(spec)
    sys.modules["energia_ble"] = modulo  # dataclass lo necesita registrado
    spec.loader.exec_module(modulo)  # sin bleak: el modulo solo lo importa al conectar
    comprobar(modulo.VERSION_MEDIDOR == "1.3", f"version del medidor 1.3 ({modulo.VERSION_MEDIDOR})")

    print("\n== decodificar()")
    v = modulo.decodificar(trama_de_prueba(2203, 512, 1127, 1234))
    comprobar(v is not None, "trama valida decodificada")
    if v:
        comprobar(abs(v["voltage_v"] - 220.3) < 1e-9, f"voltaje 220,3 V ({v['voltage_v']})")
        comprobar(abs(v["current_a"] - 0.512) < 1e-9, f"corriente 0,512 A ({v['current_a']})")
        comprobar(abs(v["power_w"] - 112.7) < 1e-9, f"potencia 112,7 W ({v['power_w']})")
        comprobar(abs(v["energy_kwh"] - 12.34) < 1e-9, f"contador del medidor 12,34 kWh ({v['energy_kwh']})")
        comprobar(modulo.formatear("power_w", v["power_w"]) == "112.7" and modulo.formatear("current_a", v["current_a"]) == "0.512",
                  "formato con los decimales de cada campo")
    comprobar(modulo.decodificar(trama_de_prueba(1, 1, 1, 1)[:16]) is None, "trama demasiado corta: invalida")
    comprobar(modulo.decodificar(b"\x00\x55" + trama_de_prueba(1, 1, 1, 1)[2:]) is None, "cabecera distinta de ff55: invalida")

    print("\n== tiempo")
    dt = datetime(2026, 10, 1, 12, 0, 5, 334000, tzinfo=timezone.utc)
    comprobar(modulo.iso_utc(dt) == "2026-10-01T12:00:05.334Z", "iso_utc con milisegundos y Z")
    comprobar(modulo.leer_iso_utc("2026-10-01T12:00:05.334Z") == dt, "leer_iso_utc lee el formato comun")
    try:
        modulo.leer_iso_utc("2026-10-01T12:00:05")
        comprobar(False, "un instante sin zona horaria se rechaza")
    except ValueError:
        comprobar(True, "un instante sin zona horaria se rechaza")

    print("\n== linea de comandos")
    with tempfile.TemporaryDirectory() as tmp:
        base = [sys.executable, str(script), "medir", "--t-start-utc", "2026-10-01T12:00:00.000Z",
                "--carpeta", tmp, "--duracion-max-s", "5"]
        r = subprocess.run(base + ["--mac", "00:11:22:33:44"], capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENTORNO)
        comprobar(r.returncode == 4 and "--mac" in r.stderr, f"MAC invalida: codigo 4 ({r.returncode})")
        r = subprocess.run(base + ["--mac", "zz:11:22:33:44:55"], capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENTORNO)
        comprobar(r.returncode == 4, f"MAC con caracteres no hexadecimales: codigo 4 ({r.returncode})")
        r = subprocess.run([sys.executable, str(script)], capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENTORNO)
        comprobar(r.returncode == 4, f"sin modo: codigo 4, no el 2 de argparse ({r.returncode})")
        try:
            import bleak  # noqa: F401
            hay_bleak = True
        except ImportError:
            hay_bleak = False
        if not hay_bleak:
            r = subprocess.run(base + ["--mac", "00:11:22:33:44:55"], capture_output=True, text=True, encoding="utf-8", errors="replace", env=ENTORNO)
            comprobar(r.returncode == 4 and "bleak" in r.stderr, f"sin el paquete bleak: codigo 4 y aviso ({r.returncode})")
            comprobar(not (Path(tmp) / "energia_lista.flag").exists(), "sin bleak no se crea energia_lista.flag")
        else:
            print("[INFO] bleak esta instalado: no se prueba el caso 'falta bleak'")

    print(f"\n{'ENERGIA_BLE OK' if FALLOS == 0 else 'ENERGIA_BLE CON FALLOS'}: {FALLOS} fallo(s)")
    return 0 if FALLOS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
