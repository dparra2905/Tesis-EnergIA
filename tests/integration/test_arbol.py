#!/usr/bin/env python3
"""Pruebas del Arbol de decision real (F8): EnergIA_Arbol.dll con energia_orquestador.

Necesita la DLL compilada con MSVC (src/execution/models/compilar_dlls.bat). Comprueba:
el catalogo muestra "Arbol de decision" con sus 4 hiperparametros, una corrida sobre un CSV
pequeno produce val_accuracy y un paso de unidad "arbol", y "guardar modelo" produce
modelo.bin declarado no recargable en EnergIA y exportable (D-009, D-013).

Uso: python test_arbol.py --orquestador <energia_orquestador.exe> --dll-arbol <EnergIA_Arbol.dll>
Solo usa la biblioteca estandar de Python.
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FALLOS = 0


def comprobar(condicion: bool, descripcion: str) -> None:
    global FALLOS
    print(f"[{'OK  ' if condicion else 'FALLA'}] {descripcion}", flush=True)
    if not condicion:
        FALLOS += 1


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--orquestador", required=True, type=Path)
    p.add_argument("--dll-arbol", required=True, type=Path)
    a = p.parse_args()
    orq, dll = a.orquestador.resolve(), a.dll_arbol.resolve()

    with tempfile.TemporaryDirectory(prefix="energia_f8_") as tmp:
        raiz = Path(tmp)
        modelos = raiz / "modelos"
        modelos.mkdir()
        shutil.copy(dll, modelos / dll.name)

        print("\n== 1. Catalogo")
        r = subprocess.run([str(orq), "--catalogo", "--dir-modelos", str(modelos)], capture_output=True)
        cat = json.loads(r.stdout.decode("utf-8")) if r.returncode == 0 else {}
        arboles = [m for m in cat.get("modelos", []) if m.get("clave") == "arbol_decision"]
        comprobar(r.returncode == 0 and len(arboles) == 1, f"el catalogo muestra el Arbol de decision ({r.returncode})")
        if arboles:
            claves = [h["clave"] for h in arboles[0]["hiperparametros"]]
            comprobar(claves == ["criterio", "max_depth", "min_hoja", "ganancia_min"],
                      f"4 hiperparametros declarados por la DLL ({claves})")
            comprobar(arboles[0]["interno"] is False and arboles[0]["unidad_paso"] == "arbol",
                      "modelo del estudio (no interno), unidad de paso 'arbol'")

        print("\n== 2. Corrida sobre un CSV pequeno (datos generados para la prueba)")
        rnd = random.Random(7)
        lineas = ["x1,x2,clase"]
        for _ in range(300):
            c = rnd.randrange(3)
            lineas.append(f"{c + rnd.gauss(0, 0.3):.4f},{2 * c + rnd.gauss(0, 0.3):.4f},{c}")
        csv = raiz / "tres_clases.csv"
        csv.write_text("\n".join(lineas) + "\n", encoding="utf-8")
        cfg = {"version_config": 1, "experimento": "arbol", "dir_salida": str(raiz),
               "modelo": {"dll": str(modelos / dll.name), "hiperparametros": {"max_depth": 5}},
               "datos": {"origen": "csv", "ruta": str(csv), "columna_objetivo": "clase"},
               "particion": {"porcentaje_entrenamiento": 80, "semilla": 42}, "guardar_modelo": True,
               "tiempos": {"fase_previa_ms": 0, "fase_posterior_ms": 0}}
        (raiz / "config.json").write_text(json.dumps(cfg), encoding="utf-8")
        r = subprocess.run([str(orq), "--ejecutar", str(raiz / "config.json")], capture_output=True, timeout=300)
        comprobar(r.returncode == 0, f"corrida completada (codigo {r.returncode}) {r.stderr.decode('utf-8', 'replace')[-300:]}")
        dir_c = raiz / "corridas" / "arbol" / "corrida_001"
        if (dir_c / "metadata.json").exists():
            m = json.loads((dir_c / "metadata.json").read_text(encoding="utf-8"))
            acc = m["evaluacion"]["metricas"].get("val_accuracy", -1) if m.get("evaluacion") else -1
            comprobar(0.0 <= acc <= 1.0, f"val_accuracy en metadata.json ({acc})")
            comprobar(acc > 0.8, f"el arbol separa las tres clases de prueba (val_accuracy {acc})")
            comprobar(m["entrenamiento"]["pasos_registrados"] == 1, "un solo paso: el arbol se construye de una vez")
            art = m["artefactos"]["modelo"]
            comprobar(art["guardado"] is True and (dir_c / "modelo.bin").is_file() and art["recargable_en_energia"] is False
                      and art["exportable"] is True,
                      "modelo.bin guardado, no recargable en EnergIA y exportable (D-009, D-013)")
            comprobar(m["hiperparametros"].get("max_depth") == 5, "hiperparametro solicitado aplicado")

    print(f"\n{'ARBOL OK' if FALLOS == 0 else 'ARBOL CON FALLOS'}: {FALLOS} fallo(s)")
    return 0 if FALLOS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
