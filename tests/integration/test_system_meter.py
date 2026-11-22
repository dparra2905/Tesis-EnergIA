#!/usr/bin/env python3
"""Pruebas de system_meter.exe (monitor de software, F5).

Python solo conduce la prueba: el codigo bajo prueba es system_meter.exe. Como proceso
medido se usa un proceso hijo de Python que ocupa un nucleo (bucle activo de un hilo), asi
que cada fila debe registrar ~1 s de CPU de ese proceso (orq_cpu_s ~ 1,0).

Verifica: encabezado, rejilla de 1 s alineada a t_start (D-007), intervalos reales, CPU del
proceso medido y del propio medidor, fin por stop.flag y por duracion maxima, cierre del
medidor cuando termina el proceso vigilado (a mitad de la medicion y antes de t_start; D-010),
PID que no existe (no se vigila) y argumentos invalidos.

Uso:
    python test_system_meter.py --medidor <system_meter.exe>
Solo usa la biblioteca estandar de Python.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta
from pathlib import Path

FALLOS = 0
COLUMNAS_FIJAS = ["timestamp", "timestamp_lectura", "intervalo_ms",
                  "cpu_sis_ocupado_s", "cpu_sis_total_s", "ram_sis_usada_bytes", "rendimiento_cpu_pct",
                  "orq_cpu_s", "orq_ws_bytes", "orq_privado_bytes",
                  "med_cpu_s", "med_ws_bytes", "med_privado_bytes"]


def comprobar(condicion: bool, descripcion: str) -> None:
    global FALLOS
    print(f"[{'OK  ' if condicion else 'FALLA'}] {descripcion}", flush=True)
    if not condicion:
        FALLOS += 1


def iso(texto: str) -> datetime:
    return datetime.fromisoformat(texto.replace("Z", "+00:00"))


def proceso_ocupado() -> subprocess.Popen:
    """Proceso hijo que mantiene un nucleo ocupado (un solo hilo)."""
    return subprocess.Popen([sys.executable, "-c", "while True:\n    pass\n"])


def leer_csv(ruta: Path) -> tuple[list[str], list[dict]]:
    with ruta.open(encoding="utf-8", newline="") as f:
        lector = csv.DictReader(f)
        filas = list(lector)
        return list(lector.fieldnames or []), filas


def leer_info(carpeta: Path) -> dict:
    return json.loads((carpeta / "sistema_info.json").read_text(encoding="utf-8"))


class Medicion:
    def __init__(self) -> None:
        self.codigo: int | None = None
        self.muerte_s: float | None = None   # cuando se mato el proceso medido (s desde el lanzamiento)
        self.fin_s: float | None = None      # cuando termino el medidor


def medir(medidor: Path, carpeta: Path, duracion_s: int, pid: int, crear_stop_tras_s: float | None,
          matar: subprocess.Popen | None = None, matar_tras_s: float | None = None) -> Medicion:
    """Lanza el medidor con --t-start ahora (siguiente segundo entero + 3 s)."""
    m = Medicion()
    proc = subprocess.Popen([str(medidor), "--t-start", "ahora", "--carpeta", str(carpeta),
                             "--duration", str(duracion_s), "--pid", str(pid)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    inicio = time.monotonic()
    while proc.poll() is None:
        transcurrido = time.monotonic() - inicio
        if matar is not None and matar_tras_s is not None and transcurrido >= matar_tras_s and matar.poll() is None:
            matar.kill()
            matar.wait()
            m.muerte_s = time.monotonic() - inicio
        if crear_stop_tras_s is not None and transcurrido >= crear_stop_tras_s and not (carpeta / "stop.flag").exists():
            (carpeta / "stop.flag").write_text("stop", encoding="utf-8")
        if transcurrido > duracion_s + 30:
            proc.kill()
            comprobar(False, "el medidor no termino a tiempo (se forzo su cierre)")
            break
        time.sleep(0.02)
    m.fin_s = time.monotonic() - inicio
    proc.communicate(timeout=10)
    m.codigo = proc.returncode
    return m


def comprobar_rejilla(filas: list[dict], info: dict, etiqueta: str) -> None:
    t_ref = iso(info["t_referencia"])
    ok_rejilla = all(iso(f["timestamp"]) == t_ref + timedelta(seconds=i + 1) for i, f in enumerate(filas))
    comprobar(ok_rejilla, f"{etiqueta}: timestamps en la rejilla t_referencia + n s, alineados a segundo entero (D-007)")
    desfases = [abs((iso(f["timestamp_lectura"]) - iso(f["timestamp"])).total_seconds()) for f in filas]
    comprobar(max(desfases) < 0.25, f"{etiqueta}: lectura real a menos de 250 ms del punto de la rejilla "
                                    f"(max {max(desfases) * 1000:.0f} ms)")
    intervalos = [float(f["intervalo_ms"]) for f in filas]
    comprobar(all(800 <= x <= 1200 for x in intervalos),
              f"{etiqueta}: intervalos reales entre 800 y 1200 ms ({min(intervalos):.0f}-{max(intervalos):.0f})")
    comprobar(all(float(f["cpu_sis_ocupado_s"]) <= float(f["cpu_sis_total_s"]) + 1e-9 for f in filas),
              f"{etiqueta}: CPU ocupada del sistema <= CPU total disponible")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--medidor", required=True, type=Path)
    args = p.parse_args()
    medidor = args.medidor.resolve()

    with tempfile.TemporaryDirectory(prefix="energia_f5_") as tmp:
        raiz = Path(tmp)

        # -------------------------------------------------------- 1. stop.flag
        print("\n== 1. Medicion de un proceso que ocupa un nucleo; fin por stop.flag")
        hijo = proceso_ocupado()
        try:
            c1 = raiz / "corrida_stop"
            c1.mkdir()
            codigo = medir(medidor, c1, duracion_s=60, pid=hijo.pid, crear_stop_tras_s=9.5).codigo
        finally:
            hijo.kill()
            hijo.wait()
        comprobar(codigo == 0, f"codigo de salida 0 (obtenido {codigo})")
        encabezado, filas = leer_csv(c1 / "sistema_metrics.csv")
        info = leer_info(c1)
        comprobar(encabezado[:len(COLUMNAS_FIJAS)] == COLUMNAS_FIJAS, "encabezado con las 13 columnas fijas")
        nucleos = [c for c in encabezado if c.startswith("nucleo_")]
        comprobar(len(nucleos) == info["nucleos_medidos"] and len(nucleos) > 0,
                  f"una columna por nucleo ({len(nucleos)})")
        comprobar(info["estado"] == "terminado" and info["motivo_fin"] == "stop_flag",
                  f"sistema_info.json: terminado por stop_flag ({info['estado']}, {info['motivo_fin']})")
        comprobar(3 <= len(filas) <= 8 and info["filas_escritas"] == len(filas),
                  f"filas escritas antes de stop.flag: {len(filas)} (info: {info['filas_escritas']})")
        comprobar(info["pid_orquestador"] == hijo.pid and info["orquestador_estado"] == "vivo",
                  "sistema_info.json: PID medido y estado vivo")
        comprobar(info["intervalo_ms"] == 1000 and info["convencion_timestamp"] == "rejilla_fin_de_intervalo",
                  "sistema_info.json: intervalo de 1000 ms y convencion de timestamp")
        t_start = iso(info["t_start"])
        comprobar(t_start.microsecond == 0, "t_start alineado a segundo entero")
        if filas:
            comprobar_rejilla(filas, info, "stop.flag")
            cpu_orq = [float(f["orq_cpu_s"]) for f in filas]
            mediana = statistics.median(cpu_orq)
            # El hijo ocupa un nucleo, pero en un equipo compartido y saturado (runner de GitHub
            # Actions con 2 nucleos) recibe menos: se midio una mediana de 0,758. El minimo de 0,5
            # sigue distinguiendo el proceso medido (~1) de otro proceso en reposo (~0).
            comprobar(0.5 <= mediana <= 1.1,
                      f"CPU del proceso medido ~1 s por fila (mediana {mediana:.3f} s; valores {[round(x, 3) for x in cpu_orq]})")
            cpu_med = [float(f["med_cpu_s"]) for f in filas]
            comprobar(max(cpu_med) < 0.1, f"consumo propio del medidor bajo (max {max(cpu_med):.4f} s de CPU por fila)")
            comprobar(all(int(f["orq_ws_bytes"]) > 0 and int(f["ram_sis_usada_bytes"]) > 0 for f in filas),
                      "memoria del proceso medido y del sistema registradas")

        # -------------------------------------------------------- 2. duracion maxima
        print("\n== 2. Fin por duracion maxima (3 s) sin proceso medido")
        c2 = raiz / "corrida_duracion"
        c2.mkdir()
        codigo = medir(medidor, c2, duracion_s=3, pid=0, crear_stop_tras_s=None).codigo
        _, filas2 = leer_csv(c2 / "sistema_metrics.csv")
        info2 = leer_info(c2)
        comprobar(codigo == 0 and info2["motivo_fin"] == "duracion_maxima",
                  f"termina por duracion maxima ({info2['motivo_fin']})")
        comprobar(len(filas2) == 3, f"3 filas para 3 s ({len(filas2)})")
        comprobar(all(f["orq_cpu_s"] == "" for f in filas2) and info2["orquestador_estado"] == "no_solicitado",
                  "sin --pid: columnas del proceso vacias")

        # -------------------------------------------------------- 3. el proceso medido termina
        # Como la detencion forzada de la interfaz: nadie crea stop.flag. El medidor vigila el
        # PID y se cierra solo (codigo 5, como energia_ble.py), en vez de medir hasta el tope.
        print("\n== 3. El proceso medido termina a mitad de la medicion (sin stop.flag)")
        hijo = proceso_ocupado()
        c3 = raiz / "corrida_muere"
        c3.mkdir()
        try:
            m3 = medir(medidor, c3, duracion_s=60, pid=hijo.pid, crear_stop_tras_s=None, matar=hijo, matar_tras_s=7.5)
        finally:
            if hijo.poll() is None:
                hijo.kill()
            hijo.wait()
        _, filas3 = leer_csv(c3 / "sistema_metrics.csv")
        info3 = leer_info(c3)
        comprobar(m3.codigo == 5, f"codigo de salida 5: el proceso vigilado termino (obtenido {m3.codigo})")
        comprobar(info3["estado"] == "terminado" and info3["motivo_fin"] == "orquestador_terminado",
                  f"sistema_info.json: terminado por orquestador_terminado ({info3['estado']}, {info3['motivo_fin']})")
        comprobar(info3["orquestador_estado"] == "terminado" and bool(info3["orquestador_fin_detectado"]),
                  "sistema_info.json: el fin del proceso medido queda registrado (orquestador_fin_detectado)")
        espera3 = (m3.fin_s - m3.muerte_s) if m3.muerte_s is not None and m3.fin_s is not None else None
        comprobar(espera3 is not None and espera3 < 1.0,
                  f"el medidor se cierra en menos de 1 s tras la muerte del proceso ({espera3} s)")
        comprobar(not (c3 / "stop.flag").exists(), "sin stop.flag: el cierre lo provoca solo la muerte del proceso")
        comprobar(len(filas3) >= 2 and info3["filas_escritas"] == len(filas3) and filas3[0]["orq_cpu_s"] != "",
                  f"filas escritas hasta la muerte del proceso: {len(filas3)} (info: {info3['filas_escritas']})")
        if filas3:
            ultima = filas3[-1]
            fin_det = iso(info3["orquestador_fin_detectado"])
            comprobar(ultima["orq_cpu_s"] == "" and iso(ultima["timestamp"]) - timedelta(seconds=1) <= fin_det,
                      f"la ultima fila es la del intervalo en curso al morir el proceso (intervalo_ms {ultima['intervalo_ms']})")
            comprobar(all(iso(f["timestamp"]) == iso(info3["t_referencia"]) + timedelta(seconds=i + 1)
                          for i, f in enumerate(filas3)),
                      "todas las filas, tambien la ultima, en su punto de la rejilla (D-007)")

        # -------------------------------------------------------- 3b. muere antes de t_start
        print("\n== 3b. El proceso medido termina antes de t_start")
        hijo = proceso_ocupado()
        c3b = raiz / "corrida_muere_antes"
        c3b.mkdir()
        try:
            m3b = medir(medidor, c3b, duracion_s=60, pid=hijo.pid, crear_stop_tras_s=None, matar=hijo, matar_tras_s=1.0)
        finally:
            if hijo.poll() is None:
                hijo.kill()
            hijo.wait()
        _, filas3b = leer_csv(c3b / "sistema_metrics.csv")
        info3b = leer_info(c3b)
        espera3b = (m3b.fin_s - m3b.muerte_s) if m3b.muerte_s is not None and m3b.fin_s is not None else None
        comprobar(m3b.codigo == 5 and info3b["motivo_fin"] == "orquestador_terminado" and not filas3b,
                  f"codigo 5, motivo orquestador_terminado y sin filas "
                  f"(codigo {m3b.codigo}, {info3b['motivo_fin']}, {len(filas3b)} filas)")
        comprobar(espera3b is not None and espera3b < 1.0,
                  f"mientras espera t_start tambien se cierra en menos de 1 s ({espera3b} s)")

        # -------------------------------------------------------- 3c. PID que no existe
        print("\n== 3c. PID sin proceso vivo al arrancar: no se vigila")
        muerto = subprocess.Popen([sys.executable, "-c", "pass"])
        muerto.wait()  # su PID ya no corresponde a un proceso vivo
        c3c = raiz / "corrida_sin_pid"
        c3c.mkdir()
        m3c = medir(medidor, c3c, duracion_s=60, pid=muerto.pid, crear_stop_tras_s=6.5)
        info3c = leer_info(c3c)
        comprobar(m3c.codigo == 0 and info3c["motivo_fin"] == "stop_flag"
                  and info3c["orquestador_estado"] in ("no_encontrado", "terminado"),
                  f"se sigue como antes: termina por stop.flag "
                  f"(codigo {m3c.codigo}, {info3c['motivo_fin']}, {info3c['orquestador_estado']})")

        # -------------------------------------------------------- 4. argumentos
        print("\n== 4. Argumentos invalidos")
        r = subprocess.run([str(medidor), "--carpeta", str(raiz)], capture_output=True)
        comprobar(r.returncode == 2, f"faltan argumentos: codigo 2 (obtenido {r.returncode})")
        r = subprocess.run([str(medidor), "--t-start", "no-es-fecha", "--carpeta", str(raiz), "--duration", "5"],
                           capture_output=True)
        comprobar(r.returncode == 2, f"t_start invalido: codigo 2 (obtenido {r.returncode})")

        tmps = [x for x in raiz.rglob("*.tmp")]
        comprobar(not tmps, "no quedan archivos temporales (sistema_info.json se escribe de forma atomica)")

    print(f"\n{'SYSTEM_METER OK' if FALLOS == 0 else 'SYSTEM_METER CON FALLOS'}: {FALLOS} fallo(s)")
    return 0 if FALLOS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
