#!/usr/bin/env python3
"""DOBLE DE PRUEBA del monitor fisico de energia. NO mide nada y NO es el Atorch.

Solo sirve para probar la orquestacion de una corrida con el medidor de energia (lanzamiento,
energia_lista.flag, stop.flag, calculo de resultados.json) en equipos sin Atorch. Escribe
archivos con el mismo formato que energia_ble.py, con una potencia CONSTANTE INVENTADA, y deja
constancia de que es un doble en energia_info.json ("doble_de_prueba": true). No debe usarse
para resultados de la tesis.

Acepta los mismos argumentos de 'medir' que energia_ble.py.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

POTENCIA_W = 40.0  # valor inventado para la prueba


def iso(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond // 1000:03d}Z"


def main() -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="modo", required=True)
    m = sub.add_parser("medir")
    m.add_argument("--t-start-utc", required=True)
    m.add_argument("--carpeta", required=True, type=Path)
    m.add_argument("--duracion-max-s", required=True, type=int)
    m.add_argument("--pid", type=int)
    m.add_argument("--mac")
    a = p.parse_args()

    carpeta = a.carpeta
    t_start = datetime.fromisoformat(a.t_start_utc.replace("Z", "+00:00"))
    fin_tope = t_start + timedelta(seconds=a.duracion_max_s)
    (carpeta / "energia_info.json").write_text(json.dumps({
        "medidor": "medidor_energia_doble.py", "doble_de_prueba": True,
        "descripcion": "Doble de prueba: potencia constante inventada; no es una medicion"}, indent=2), encoding="utf-8")

    with (carpeta / "energia_tramas.csv").open("w", newline="", encoding="utf-8") as ft, \
            (carpeta / "energia_atorch.csv").open("w", newline="", encoding="utf-8") as fr:
        tramas = csv.writer(ft)
        tramas.writerow(["timestamp_trama", "sesion", "valida", "voltage_v", "current_a", "power_w", "energy_kwh", "raw_hex"])
        rejilla = csv.writer(fr)
        rejilla.writerow(["timestamp", "timestamp_trama", "conectado", "voltage_v", "current_a", "power_w", "energy_kwh", "raw_hex"])
        lista = False
        n = 0
        while True:
            ahora = datetime.now(timezone.utc)
            if (carpeta / "stop.flag").exists() or ahora >= fin_tope:
                break
            tramas.writerow([iso(ahora), 1, 1, "220.0", "0.182", f"{POTENCIA_W:.1f}", "0.00", f"d0b1e{n:04x}"])
            ft.flush()
            if not lista:
                (carpeta / "energia_lista.flag").write_text(iso(ahora) + "\n", encoding="utf-8")
                lista = True
            if ahora >= t_start:
                rejilla.writerow([iso(ahora), iso(ahora), 1, "220.0", "0.182", f"{POTENCIA_W:.1f}", "0.00", f"d0b1e{n:04x}"])
                fr.flush()
            n += 1
            time.sleep(1.0)
    print(f"doble de prueba: {n} tramas inventadas", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
