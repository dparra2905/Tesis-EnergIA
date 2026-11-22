#!/usr/bin/env python3
"""Pruebas de integracion de F3: corrida completa con energia_orquestador --ejecutar.

Python es solo el conductor de las pruebas: el codigo bajo prueba es energia_orquestador.exe
(C++/Qt Core) con la DLL de prueba. Verifica lo mismo que tests/integration/test_corrida.cpp,
que compila pero en el equipo de desarrollo actual no puede ejecutarse porque Smart App
Control lo bloquea (evento Code Integrity 3077).

Uso:
    python test_corrida.py --orquestador <energia_orquestador.exe> --dll <EnergIA_Prueba.dll>
Las DLL de Qt deben estar en el PATH (CTest lo configura).
Solo usa la biblioteca estandar de Python.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import os
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime, timedelta
from pathlib import Path

FALLOS = 0
ARCHIVOS_ESPERADOS_COMPLETADA = {"config.json", "metadata.json", "progreso.csv", "modelo.bin", "auditoria.json",
                                 "metricas_modelo.json"}
SALIDA_COMPLETADA, SALIDA_CANCELADA, SALIDA_ERROR = 0, 5, 6


def comprobar(condicion: bool, descripcion: str) -> None:
    global FALLOS
    print(f"[{'OK  ' if condicion else 'FALLA'}] {descripcion}", flush=True)
    if not condicion:
        FALLOS += 1


def iso(texto) -> datetime | None:
    try:
        return datetime.fromisoformat(str(texto).replace("Z", "+00:00"))
    except ValueError:
        return None


class Resultado:
    def __init__(self) -> None:
        self.codigo: int | None = None
        self.eventos: list[dict] = []
        self.lineas_invalidas: list[str] = []
        self.stderr: str = ""


def correr(orquestador: Path, config: Path, cancelar: bool = False, timeout_s: float = 60.0,
           cancelar_en: bytes = b'"fin_paso"') -> Resultado:
    """Lanza el orquestador. Si cancelar, envia la orden por stdin en el primer evento que
    contenga 'cancelar_en' (por defecto, el primer fin de paso)."""
    r = Resultado()
    proc = subprocess.Popen([str(orquestador), "--ejecutar", str(config)],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not cancelar:
        proc.stdin.close()  # como si ui.exe se cerrara: la corrida debe seguir (D-010)

    lineas: list[bytes] = []
    err: list[bytes] = []

    def leer_stdout() -> None:
        enviado = False
        for linea in proc.stdout:
            lineas.append(linea)
            if cancelar and not enviado and cancelar_en in linea:
                proc.stdin.write(b'{"v":1,"orden":"cancelar"}\n')
                proc.stdin.flush()
                enviado = True

    def leer_stderr() -> None:
        err.extend(proc.stderr)

    hilos = [threading.Thread(target=leer_stdout), threading.Thread(target=leer_stderr)]
    for h in hilos:
        h.start()
    try:
        r.codigo = proc.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        proc.kill()
        r.codigo = proc.wait()
        comprobar(False, f"el orquestador no termino en {timeout_s} s (se forzo su cierre)")
    for h in hilos:
        h.join(timeout=10)
    if cancelar and not proc.stdin.closed:
        proc.stdin.close()

    for linea in lineas:
        texto = linea.decode("utf-8", errors="replace").rstrip("\r\n")
        if not texto.strip():
            continue
        try:
            obj = json.loads(texto)
        except json.JSONDecodeError:
            r.lineas_invalidas.append(texto)
            continue
        if isinstance(obj, dict) and obj.get("v") == 1 and obj.get("evento"):
            r.eventos.append(obj)
        else:
            r.lineas_invalidas.append(texto)
    r.stderr = b"".join(err).decode("utf-8", errors="replace")
    return r


def eventos(r: Resultado, nombre: str) -> list[dict]:
    return [e for e in r.eventos if e.get("evento") == nombre]


def comprobar_protocolo(r: Resultado, etiqueta: str) -> None:
    comprobar(not r.lineas_invalidas and len(r.eventos) > 0,
              f"{etiqueta}: stdout con {len(r.eventos)} eventos JSON Lines validos y "
              f"{len(r.lineas_invalidas)} lineas invalidas")
    lineas_err = [l for l in r.stderr.splitlines() if l.strip()]
    solo_diagnostico = all(l.startswith("[energia_orquestador ") for l in lineas_err)
    sin_eventos = not any('"evento"' in l for l in lineas_err)
    comprobar(solo_diagnostico and sin_eventos,
              f"{etiqueta}: stderr solo con {len(lineas_err)} lineas de diagnostico (ningun evento del protocolo)")


def leer_progreso(ruta: Path) -> tuple[list[str], list[list[str]]]:
    filas = [l.split(",") for l in ruta.read_text(encoding="utf-8").splitlines() if l.strip()]
    return (filas[0], filas[1:]) if filas else ([], [])


# Claves que contienen "energ" sin ser metricas de energia (D-009/D-013: el modelo no se
# recarga en EnergIA)
CLAVES_NO_ENERGETICAS = {"recargable_en_energia"}


def tiene_clave_energetica(v) -> bool:
    if isinstance(v, dict):
        return any((k not in CLAVES_NO_ENERGETICAS and any(p in k.lower() for p in ("energ", "potencia", "power", "kwh")))
                   or tiene_clave_energetica(x) for k, x in v.items())
    if isinstance(v, list):
        return any(tiene_clave_energetica(x) for x in v)
    return False


def comprobar_metricas_modelo(dir_: Path, m: dict, etiqueta: str) -> None:
    """metricas_modelo.json (D-015) de una corrida cuyo entrenamiento termino bien, con la DLL de
    prueba: predice siempre la clase mayoritaria del entrenamiento."""
    ruta = dir_ / "metricas_modelo.json"
    comprobar(ruta.is_file() and m["artefactos"]["metricas_modelo"] == {"archivo": "metricas_modelo.json", "tipo": "derivado"},
              f"{etiqueta}: metricas_modelo.json y metadata.artefactos.metricas_modelo")
    if not ruta.is_file():
        return
    mm = json.loads(ruta.read_text(encoding="utf-8"))
    d = m["datos"]
    k = d["n_clases"]
    comprobar(mm["version_formato"] == 1 and mm["clases_originales"] == d["clases_originales"] and mm["n_clases"] == k
              and mm["lote_predecir"] == 65536,
              f"{etiqueta}: version 1, clases_originales de metadata y lote 65536")
    for conjunto, n in (("validacion", d["n_val"]), ("entrenamiento", d["n_train"])):
        c = mm[conjunto]
        mat = c["matriz_confusion"]
        comprobar(c["status_dll"] == 0 and c["motivo"] is None and c["n_filas"] == n and len(mat) == k
                  and all(len(f) == k for f in mat) and sum(map(sum, mat)) == n
                  and c["duracion_ms"] >= c["duracion_predecir_ms"] >= 0,
                  f"{etiqueta}, {conjunto}: matriz {k}x{k} con {n} filas y duracion del calculo")
        # Clase mayoritaria: una sola columna con predicciones; las demas clases, precision indefinida
        columnas = [j for j in range(k) if any(f[j] for f in mat)]
        pc = c["por_clase"]
        comprobar(len(columnas) == 1 and len(pc) == k
                  and [x["etiqueta"] for x in pc] == d["clases_originales"]
                  and all(x["precision"]["indefinida"] == (x["indice"] != columnas[0]) for x in pc)
                  and all(x["soporte"] == sum(mat[x["indice"]]) for x in pc)
                  and abs(c["exactitud"] - sum(mat[i][i] for i in range(k)) / n) < 1e-12
                  and set(c["macro"]) == {"precision", "recall", "f1"} and set(c["ponderado"]) == {"precision", "recall", "f1"},
                  f"{etiqueta}, {conjunto}: por clase con etiquetas, soporte e indefinidas; exactitud y promedios")
    cons = mm["consistencia"]
    comprobar(cons["coincide"] is True and cons["val_accuracy_evaluar"] == m["evaluacion"]["metricas"]["val_accuracy"]
              and abs(cons["exactitud_validacion"] - mm["validacion"]["exactitud"]) < 1e-15,
              f"{etiqueta}: la exactitud de validacion coincide con val_accuracy de evaluar")


def config_base(dir_salida: Path, dll: Path, experimento: str, pasos: int, ms: int) -> dict:
    return {
        "version_config": 1,
        "experimento": experimento,
        "dir_salida": str(dir_salida),
        "modelo": {"dll": str(dll), "hiperparametros": {"pasos": pasos, "ms_por_paso": ms, "curva": "lineal"}},
        "datos": {"origen": "sintetico"},
        "particion": {"porcentaje_entrenamiento": 80, "semilla": 42},
        "guardar_modelo": True,
        # Sin linea base (el orquestador usa 20 s si no se indica): las pruebas no esperan 40 s
        "tiempos": {"fase_previa_ms": 0, "fase_posterior_ms": 0},
    }


def escribir_config(dir_: Path, nombre: str, cfg: dict) -> Path:
    ruta = dir_ / nombre
    ruta.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")
    return ruta


def procesos_orquestador() -> int:
    """Cantidad de procesos energia_orquestador.exe vivos (Windows)."""
    if sys.platform != "win32":
        return 0
    salida = subprocess.run(["tasklist", "/FI", "IMAGENAME eq energia_orquestador.exe", "/FO", "CSV", "/NH"],
                            capture_output=True, text=True).stdout
    return sum(1 for l in salida.splitlines() if "energia_orquestador.exe" in l.lower())


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--orquestador", required=True, type=Path)
    p.add_argument("--dll", required=True, type=Path)
    p.add_argument("--energia-ble", type=Path, default=None, help="energia_ble.py (prueba sin Atorch, opcional)")
    p.add_argument("--conservar", type=Path, default=None,
                   help="copia aqui el directorio de salida para inspeccionarlo (opcional)")
    args = p.parse_args()
    orq, dll = args.orquestador.resolve(), args.dll.resolve()
    script_ble = args.energia_ble.resolve() if args.energia_ble else None

    procesos_antes = procesos_orquestador()
    with tempfile.TemporaryDirectory(prefix="energia_f3_") as tmp:
        raiz = Path(tmp)
        dir_exp = raiz / "corridas" / "prueba_f3"

        # ------------------------------------------------------------ 1. completada
        print("\n== 1. Corrida sintetica completada (5 pasos de 100 ms, stdin cerrado al iniciar)")
        cfg1 = escribir_config(raiz, "config1.json", config_base(raiz, dll, "prueba_f3", 5, 100))
        r1 = correr(orq, cfg1)
        comprobar(r1.codigo == SALIDA_COMPLETADA, f"codigo de salida {SALIDA_COMPLETADA} (obtenido {r1.codigo})")
        comprobar_protocolo(r1, "completada")
        comprobar(r1.eventos and r1.eventos[0]["evento"] == "inicio" and r1.eventos[-1]["evento"] == "fin",
                  "primer evento 'inicio' y ultimo 'fin'")
        fin1 = eventos(r1, "fin")
        comprobar(len(fin1) == 1 and fin1[0]["estado"] == "completada" and fin1[0]["error"] is None,
                  "evento fin: estado completada y error null")
        comprobar(len(eventos(r1, "progreso")) == 10, f"10 eventos de progreso (5 inicio_paso + 5 fin_paso; "
                                                      f"obtenidos {len(eventos(r1, 'progreso'))})")
        comprobar("stdin cerrado; la corrida continua" in r1.stderr and fin1 and fin1[0]["estado"] == "completada",
                  "cerrar stdin no cancela la corrida (D-010)")

        dir1 = dir_exp / "corrida_001"
        comprobar(dir1.is_dir(), "se crea corridas/prueba_f3/corrida_001")
        try:
            cfg_guardada = json.loads((dir1 / "config.json").read_text(encoding="utf-8"))
            comprobar(cfg_guardada == json.loads(cfg1.read_text(encoding="utf-8")),
                      "config.json existe, es JSON valido e igual a la configuracion recibida")
        except (OSError, json.JSONDecodeError) as e:
            comprobar(False, f"config.json de la corrida: {e}")

        enc, filas = leer_progreso(dir1 / "progreso.csv")
        comprobar(len(enc) == 12 and enc[0] == "timestamp_inicio" and enc[3] == "step_total",
                  "progreso.csv: encabezado de 12 columnas")
        comprobar(len(filas) == 5, f"progreso.csv: una fila por paso ({len(filas)} filas)")

        m1 = json.loads((dir1 / "metadata.json").read_text(encoding="utf-8"))
        comprobar(m1.get("estado") == "completada" and m1.get("error") is None, "metadata.json valido: estado completada")
        comprobar(m1["experimento"] == "prueba_f3" and m1["corrida"] == 1 and m1["id_corrida"] == "prueba_f3/corrida_001",
                  "metadata: experimento, corrida e id_corrida")
        mod = m1["modelo"]
        comprobar(mod["clave"] == "prueba" and mod["interno"] is True and mod["version_contrato"] == 4,
                  "metadata: modelo 'prueba' interno, contrato v4")
        comprobar(mod.get("hilos_efectivos") == mod["hilos"] == 1,
                  "metadata: sin hiperparametro 'hilos', hilos_efectivos = los automaticos de la DLL (D2)")
        hip = m1["hiperparametros"]
        comprobar(hip.get("pasos") == 5 and hip.get("curva") == "lineal" and hip.get("capas") == "64,32",
                  "metadata: hiperparametros usados (solicitados + por defecto, opcion por nombre)")
        comprobar(m1["datos"]["origen"] == "sintetico" and m1["datos"]["n_train"] == 1200 and m1["datos"]["n_val"] == 300,
                  "metadata: datos y particion 80/20")
        d1 = m1["datos"]
        comprobar(d1.get("columnas_entrada") == [f"x{j}" for j in range(d1["n_cols"])]
                  and d1.get("indice_objetivo") == d1["n_cols"] and d1.get("columnas_generadas") is True,
                  "metadata: datos sinteticos con columnas_entrada generadas (x0..), indice_objetivo al final y columnas_generadas")
        comprobar(d1.get("sha256", "falta") is None and d1.get("tam_bytes", "falta") is None
                  and d1.get("fecha_modificacion", "falta") is None,
                  "metadata: datos sinteticos sin huella (sha256, tam_bytes y fecha_modificacion en null)")

        t = m1["tiempos"]
        t_start, t_antes, t_despues, t_fin = (iso(t["t_start"]), iso(t["t_antes"]), iso(t["t_despues"]), iso(t["t_fin"]))
        comprobar(None not in (t_start, t_antes, t_despues, t_fin) and t_start <= t_antes <= t_despues <= t_fin,
                  "metadata: t_start <= t_antes <= t_despues <= t_fin")
        comprobar(t_start is not None and t_start.microsecond == 0, "t_start alineado a segundo entero (rejilla D-007)")
        comprobar(t["fase_previa_ms"] == 0 and t["fase_posterior_ms"] == 0,
                  "fases previa/posterior en 0 ms, como se pidieron")
        comprobar(t["fase_posterior_iniciada"] is False, "sin fase posterior: fase_posterior_iniciada = false")
        # Sin la inferencia activada la corrida es como antes (D-014): marcas en false / null
        comprobar(m1["inferencia"] == {"activada": False} and t["t_inf_antes"] is None and t["t_inf_despues"] is None
                  and t["fase_media_ms"] is None and t["fase_media_iniciada"] is False and t["inferencia_iniciada"] is False
                  and m1["artefactos"]["progreso_inferencia"] is None and not (dir1 / "progreso_inferencia.csv").exists(),
                  "sin inferencia: metadata.inferencia = {activada: false}, sin marcas ni progreso_inferencia.csv")
        comprobar(not any(e.get("tarea") == "inferencia" for e in r1.eventos) and not eventos(r1, "inferencia_terminada")
                  and [e["fase"] for e in eventos(r1, "fase")].count("inferencia") == 0,
                  "sin inferencia: ningun evento de la inferencia")

        ok_pasos, anterior = len(filas) == 5, t_antes
        for i, f in enumerate(filas):
            ini, fin = iso(f[0]), iso(f[1])
            ok_pasos = ok_pasos and ini is not None and fin is not None and anterior <= ini <= fin <= t_despues \
                and int(f[2]) == i and int(f[3]) == 5 and f[4] == "train_loss"
            anterior = fin or anterior
        comprobar(ok_pasos, "progreso.csv: timestamps coherentes, ordenados y dentro de [t_antes, t_despues]")
        fines = [e for e in eventos(r1, "progreso") if e["momento"] == "fin_paso"]
        comprobar([e["paso"] for e in fines] == [0, 1, 2, 3, 4] and all(iso(e["t_callback"]) == iso(f[1])
                                                                         for e, f in zip(fines, filas)),
                  "eventos fin_paso coinciden con progreso.csv (mismo t_callback que timestamp_fin)")

        ev = m1["evaluacion"]
        acc = ev["metricas"].get("val_accuracy", -1)
        comprobar(ev["ejecutada"] and ev["status"] == 0 and 0 <= acc <= 1, f"evaluacion realizada: val_accuracy = {acc}")
        comprobar_metricas_modelo(dir1, m1, "completada")
        art = m1["artefactos"]["modelo"]
        comprobar(art["guardado"] is True and art["archivo"] == "modelo.bin" and (dir1 / "modelo.bin").is_file(),
                  "modelo.bin generado")
        # D-009 actualizada (D-013): no se recarga en EnergIA; la DLL de prueba no es exportable
        comprobar(art["recargable_en_energia"] is False and art["exportable"] is False and "reutilizable" not in art,
                  "el artefacto se declara no recargable en EnergIA y, con la DLL de prueba, no exportable (D-009, D-013)")
        ent = m1["entrenamiento"]
        # La DLL se libera al terminar el proceso, no con FreeLibrary (ver DllModelo.h)
        comprobar(ent["modelo_liberado"] == "liberado" and ent["dll_descarga"] == "al_terminar_el_proceso"
                  and fin1[0]["modelo_liberado"] == "liberado" and fin1[0]["dll_descarga"] == "al_terminar_el_proceso",
                  "modelo liberado; la DLL se libera al terminar el proceso (metadata y evento fin)")
        comprobar(m1["medidores"] == [] and not tiene_clave_energetica(m1), "sin medidores ni metricas energeticas (F3)")
        # Auditoria (D-012): se calcula al terminar, fuera de la medicion, y se anuncia antes de 'fin'
        aud1 = json.loads((dir1 / "auditoria.json").read_text(encoding="utf-8"))
        res1 = {c["id"]: c["resultado"] for c in aud1["comprobaciones"]}
        comprobar(aud1["version_reglas"] == 2 and aud1["veredicto"] == "valida" and res1["estado"] == "ok" and res1["pasos"] == "ok"
                  and res1["medidores"] == "no_evaluable" and res1["cobertura_entrenamiento"] == "no_evaluable"
                  and res1["cobertura_inferencia"] == "no_evaluable" and res1["pasos_inferencia"] == "no_evaluable",
                  f"auditoria.json: version de reglas 2, veredicto valida; sin medidores ni inferencia, el resto no evaluable ({res1})")
        nombres1 = [e["evento"] for e in r1.eventos]
        comprobar("auditoria" in nombres1 and nombres1.index("auditoria") == len(nombres1) - 2
                  and eventos(r1, "auditoria")[0]["veredicto"] == "valida",
                  "evento auditoria con el veredicto, justo antes de fin")
        comprobar({x.name for x in dir1.iterdir()} == ARCHIVOS_ESPERADOS_COMPLETADA,
                  f"la corrida solo contiene {sorted(ARCHIVOS_ESPERADOS_COMPLETADA)} (sin temporales)")

        # ------------------------------------------------------------ 2. numeracion
        print("\n== 2. Segunda corrida del mismo experimento")
        r2 = correr(orq, escribir_config(raiz, "config2.json", config_base(raiz, dll, "prueba_f3", 2, 50)))
        comprobar(r2.codigo == SALIDA_COMPLETADA and (dir_exp / "corrida_002").is_dir(), "se crea corrida_002")
        comprobar_protocolo(r2, "segunda corrida")

        # ------------------------------------------------------------ 3. cancelacion
        print("\n== 3. Cancelacion por stdin a mitad (50 pasos de 200 ms)")
        t0 = time.monotonic()
        r3 = correr(orq, escribir_config(raiz, "config3.json", config_base(raiz, dll, "prueba_f3", 50, 200)), cancelar=True)
        dur = time.monotonic() - t0
        comprobar(r3.codigo == SALIDA_CANCELADA, f"codigo de salida {SALIDA_CANCELADA} (obtenido {r3.codigo})")
        comprobar(dur < 8.0, f"termino en {dur:.1f} s, antes de los ~10 s que duraria sin cancelar")
        comprobar_protocolo(r3, "cancelacion")
        comprobar(len(eventos(r3, "cancelacion_solicitada")) == 1, "evento cancelacion_solicitada")
        terminado = eventos(r3, "entrenamiento_terminado")
        comprobar(len(terminado) == 1 and terminado[0]["status_dll"] == 4, "la DLL devolvio ENERGIA_CANCELADO (4)")
        fin3 = eventos(r3, "fin")
        comprobar(len(fin3) == 1 and fin3[0]["estado"] == "cancelada", "evento fin: estado cancelada")
        dir3 = dir_exp / "corrida_003"
        m3 = json.loads((dir3 / "metadata.json").read_text(encoding="utf-8"))
        comprobar(m3["estado"] == "cancelada", "metadata.json: estado cancelada")
        comprobar(m3["tiempos"]["fase_posterior_iniciada"] is False,
                  "cancelada durante el entrenamiento: la fase posterior no empieza")
        _, filas3 = leer_progreso(dir3 / "progreso.csv")
        comprobar(1 <= len(filas3) < 50, f"progreso.csv: {len(filas3)} de 50 pasos antes de cancelar")
        comprobar(m3["entrenamiento"]["dll_descarga"] == "al_terminar_el_proceso"
                  and m3["entrenamiento"]["modelo_liberado"] == "no_aplica"
                  and fin3[0]["dll_descarga"] == "al_terminar_el_proceso",
                  "cancelacion: ningun modelo vivo (la DLL libera lo creado al cancelar)")
        comprobar(not (dir3 / "modelo.bin").exists() and m3["artefactos"]["modelo"]["guardado"] is False
                  and m3["evaluacion"]["ejecutada"] is False and not eventos(r3, "evaluacion"),
                  "cancelacion: sin modelo guardado ni evaluacion (evaluacion.ejecutada = false)")
        comprobar(m3["artefactos"]["metricas_modelo"] is None and not (dir3 / "metricas_modelo.json").exists(),
                  "cancelada durante el entrenamiento: sin metricas del modelo (artefactos.metricas_modelo = null)")
        aud3 = json.loads((dir3 / "auditoria.json").read_text(encoding="utf-8"))
        est3 = [c for c in aud3["comprobaciones"] if c["id"] == "estado"][0]
        pasos3 = [c for c in aud3["comprobaciones"] if c["id"] == "pasos"][0]
        comprobar(aud3["veredicto"] == "con_advertencias" and est3["resultado"] == "advertencia"
                  and est3["motivo"] == "cancelada durante el entrenamiento: la ventana de entrenamiento es parcial"
                  and pasos3["resultado"] == "no_evaluable",
                  f"auditoria de la cancelada: con advertencias ('{est3['motivo']}'); pasos no evaluable")
        comprobar({x.name for x in dir3.iterdir()} == {"config.json", "metadata.json", "progreso.csv", "auditoria.json"},
                  "la corrida cancelada no deja archivos temporales")

        # ------------------------------------------------------------ 3c. cancelar en la fase posterior
        print("\n== 3c. Cancelacion durante la linea base posterior")
        c3c = config_base(raiz, dll, "cancela_post", 2, 50)
        c3c["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 5000}
        t0 = time.monotonic()
        r3c = correr(orq, escribir_config(raiz, "config3c.json", c3c), cancelar=True, cancelar_en=b'"fase_posterior"')
        dur3c = time.monotonic() - t0
        dir3c = raiz / "corridas" / "cancela_post" / "corrida_001"
        m3c = json.loads((dir3c / "metadata.json").read_text(encoding="utf-8"))
        comprobar(r3c.codigo == SALIDA_CANCELADA and m3c["estado"] == "cancelada" and eventos(r3c, "fin")[0]["estado"] == "cancelada"
                  and dur3c < 4.5,
                  f"cancelada en la fase posterior (codigo {r3c.codigo}, {m3c['estado']}, {dur3c:.1f} s)")
        ev3c = eventos(r3c, "evaluacion")
        comprobar(m3c["evaluacion"]["ejecutada"] is True and "val_accuracy" in m3c["evaluacion"]["metricas"] and len(ev3c) == 1,
                  "el entrenamiento termino bien: se evalua y la exactitud queda registrada")
        comprobar_metricas_modelo(dir3c, m3c, "cancelada en la fase posterior")
        comprobar(not (dir3c / "modelo.bin").exists() and m3c["artefactos"]["modelo"]["guardado"] is False
                  and m3c["artefactos"]["modelo"]["solicitado"] is True,
                  "no se guarda el modelo aunque se pidio (corrida cancelada)")
        comprobar(m3c["entrenamiento"]["modelo_liberado"] == "liberado" and eventos(r3c, "fin")[0]["modelo_liberado"] == "liberado",
                  "el modelo se libera")
        comprobar(m3c["tiempos"]["fase_posterior_iniciada"] is True, "la fase posterior habia empezado")
        aud3c = json.loads((dir3c / "auditoria.json").read_text(encoding="utf-8"))
        comprobar([c["motivo"] for c in aud3c["comprobaciones"] if c["id"] == "estado"]
                  == ["cancelada durante la línea base posterior: el entrenamiento está completo"]
                  and aud3c["veredicto"] == "con_advertencias",
                  "auditoria: con advertencias, 'cancelada durante la línea base posterior: el entrenamiento está completo'")

        # --postprocesar recalcula la auditoria aunque la corrida no tenga datos de energia
        (dir3c / "auditoria.json").unlink()
        rp = subprocess.run([str(orq), "--postprocesar", str(dir3c)], capture_output=True)
        comprobar(rp.returncode == 6 and (dir3c / "auditoria.json").is_file()
                  and json.loads((dir3c / "auditoria.json").read_text(encoding="utf-8"))["veredicto"] == "con_advertencias",
                  f"--postprocesar sin datos de energia: codigo 6, pero recalcula auditoria.json (codigo {rp.returncode})")

        # ------------------------------------------------------------ 3b. linea base por defecto
        # ------------------------------------------------------------ 3d. inferencia (D-014)
        print("\n== 3d. Corrida con inferencia: base_media de 1 s e inferencia de 2 s (lote grande)")
        c3d = config_base(raiz, dll, "inferencia", 3, 100)
        c3d["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 1000, "fase_media_ms": 1000}
        c3d["inferencia"] = {"activada": True, "duracion_min_s": 2}
        r3d = correr(orq, escribir_config(raiz, "config3d.json", c3d))
        comprobar(r3d.codigo == SALIDA_COMPLETADA, f"con inferencia: codigo {SALIDA_COMPLETADA} (obtenido {r3d.codigo})")
        comprobar_protocolo(r3d, "con inferencia")
        dir3d = raiz / "corridas" / "inferencia" / "corrida_001"
        m3d = json.loads((dir3d / "metadata.json").read_text(encoding="utf-8"))
        t3d, inf3d = m3d["tiempos"], m3d["inferencia"]
        fases3d = [e["fase"] for e in eventos(r3d, "fase")]
        comprobar(m3d["estado"] == "completada", "con inferencia: estado completada")
        comprobar("entrenando" in fases3d and "fase_media" in fases3d and "inferencia" in fases3d and "fase_posterior" in fases3d
                  and fases3d.index("entrenando") < fases3d.index("fase_media") < fases3d.index("inferencia")
                  < fases3d.index("fase_posterior") < fases3d.index("cerrando_modelo"),
                  f"fases en orden: entrenando, fase_media, inferencia, fase_posterior, cerrando_modelo ({fases3d})")
        ta, td = iso(t3d["t_antes"]), iso(t3d["t_despues"])
        tia, tid, tstop = iso(t3d["t_inf_antes"]), iso(t3d["t_inf_despues"]), iso(t3d["t_stop"])
        media3d = (tia - td).total_seconds() if tia and td else -1
        post3d = (tstop - tid).total_seconds() if tstop and tid else -1
        # base_media nunca empieza la inferencia antes de tiempo (lanzarInferencia lo corrige), y la
        # fase posterior tampoco termina antes (alVencerFasePosterior corrige el adelanto del QTimer)
        comprobar(None not in (ta, td, tia, tid, tstop) and ta <= td and media3d >= 1.0 and tia <= tid and post3d >= 1.0,
                  f"t_despues + base_media (1 s) <= t_inf_antes <= t_inf_despues; base_post de ~1 s hasta t_stop "
                  f"(base_media {media3d:.3f} s, base_post {post3d:.3f} s)")
        dur3d = (tid - tia).total_seconds() if tia and tid else -1
        comprobar(2.0 <= dur3d < 3.5, f"la inferencia dura entre la duracion minima (2 s) y ~1 s mas ({dur3d:.3f} s)")
        comprobar(t3d["fase_media_ms"] == 1000 and t3d["fase_media_iniciada"] is True and t3d["inferencia_iniciada"] is True
                  and t3d["fase_posterior_iniciada"] is True,
                  "metadata.tiempos: fase_media_ms 1000 y las marcas fase_media_iniciada, inferencia_iniciada y fase_posterior_iniciada")
        n_val = m3d["datos"]["n_val"]
        comprobar(inf3d["activada"] is True and inf3d["lote"] == 65536 and inf3d["duracion_min_s"] == 2
                  and inf3d["paso_min_ms"] == 1000 and inf3d["datos"] == "validacion" and inf3d["n_filas"] == n_val
                  and inf3d["status_dll"] == 0,
                  "metadata.inferencia: activada, lote por defecto 65536, duracion 2 s, paso_min_ms 1000, particion de validacion")
        enc3d, filas3d = leer_progreso(dir3d / "progreso_inferencia.csv")
        enc_ent, _ = leer_progreso(dir3d / "progreso.csv")
        comprobar(enc3d == enc_ent, "progreso_inferencia.csv tiene el mismo encabezado que progreso.csv")
        # Prueba obligatoria: pasadas de microsegundos con lote grande -> ~duracion pasos, no millones
        comprobar(2 <= len(filas3d) <= 3 and inf3d["grupos_registrados"] == len(filas3d),
                  f"2 s de inferencia con pasadas de microsegundos: {len(filas3d)} grupos (2 o 3), no millones")
        pasadas = sum(int(float(f[5])) for f in filas3d if f[4] == "pasadas")
        ok_grupos = all(int(f[2]) == i and int(f[3]) == 0 and f[4] == "pasadas" and f[6] == "predicciones"
                        and int(float(f[7])) == int(float(f[5])) * n_val
                        and (iso(f[1]) - iso(f[0])).total_seconds() >= 0.999 for i, f in enumerate(filas3d))
        comprobar(ok_grupos, "progreso_inferencia.csv: un grupo por fila (step_total 0), de al menos 1 s, con pasadas y predicciones")
        comprobar(inf3d["pasadas"] == pasadas and pasadas > 100 and inf3d["n_predicciones"] == pasadas * n_val,
                  f"metadata.inferencia: pasadas ({pasadas}) y n_predicciones = filas x pasadas completas")
        esperado_us = inf3d["duracion_ms"] * 1000 / inf3d["n_predicciones"] if inf3d["n_predicciones"] else -1
        comprobar(inf3d["duracion_ms"] == round(dur3d * 1000) and abs(inf3d["tiempo_por_prediccion_us"] - esperado_us) < 1e-9
                  and inf3d["motivo_sin_por_prediccion"] is None,
                  f"metadata.inferencia: duracion_ms y tiempo_por_prediccion_us = duracion_ms x 1000 / n_predicciones "
                  f"({inf3d['tiempo_por_prediccion_us']})")
        prog3d = [e for e in eventos(r3d, "progreso") if e.get("tarea") == "inferencia"]
        comprobar(len(prog3d) == len(filas3d) and all(e["momento"] == "fin_paso" and e["paso"] == i for i, e in enumerate(prog3d))
                  and prog3d and prog3d[-1]["pasadas"] == pasadas and prog3d[-1]["predicciones"] == pasadas * n_val,
                  "un evento progreso de la inferencia por grupo, con las pasadas y predicciones acumuladas")
        fin_inf = eventos(r3d, "inferencia_terminada")
        comprobar(len(fin_inf) == 1 and fin_inf[0]["status_dll"] == 0 and fin_inf[0]["n_predicciones"] == pasadas * n_val,
                  "evento inferencia_terminada con status 0 y n_predicciones")
        aud3d = {c["id"]: c for c in json.loads((dir3d / "auditoria.json").read_text(encoding="utf-8"))["comprobaciones"]}
        comprobar(aud3d["pasos_inferencia"]["resultado"] == "ok" and aud3d["pasos_inferencia"]["valor"] == pasadas,
                  f"auditoria: pasos_inferencia ok con las pasadas completas ({aud3d['pasos_inferencia']['motivo']})")
        comprobar(m3d["evaluacion"]["ejecutada"] and (dir3d / "modelo.bin").is_file()
                  and m3d["artefactos"]["progreso_inferencia"] == "progreso_inferencia.csv"
                  and m3d["entrenamiento"]["modelo_liberado"] == "liberado",
                  "con inferencia completa: se evalua, se guarda el modelo y se libera")

        print("\n== 3e. Cancelacion durante la linea base intermedia")
        c3e = dict(c3d, experimento="inferencia_cancelada_media")
        c3e["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 0, "fase_media_ms": 20000}
        r3e = correr(orq, escribir_config(raiz, "config3e.json", c3e), cancelar=True, cancelar_en=b'"fase_media"')
        m3e = json.loads((raiz / "corridas" / "inferencia_cancelada_media" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        comprobar(r3e.codigo == SALIDA_CANCELADA and m3e["estado"] == "cancelada",
                  f"cancelada en base_media: codigo {SALIDA_CANCELADA} y estado cancelada")
        comprobar(m3e["tiempos"]["fase_media_iniciada"] is True and m3e["tiempos"]["inferencia_iniciada"] is False
                  and m3e["tiempos"]["t_inf_antes"] is None and m3e["inferencia"]["status_dll"] is None
                  and m3e["artefactos"]["progreso_inferencia"] is None,
                  "cancelada en base_media: la inferencia no empezo (sin t_inf_antes ni progreso_inferencia.csv)")
        aud3e = {c["id"]: c for c in json.loads((raiz / "corridas" / "inferencia_cancelada_media" / "corrida_001"
                                                  / "auditoria.json").read_text(encoding="utf-8"))["comprobaciones"]}
        comprobar(aud3e["estado"]["motivo"] == "cancelada durante la línea base intermedia: el entrenamiento está completo",
                  f"auditoria: {aud3e['estado']['motivo']}")
        comprobar(m3e["evaluacion"]["ejecutada"] and m3e["artefactos"]["modelo"]["guardado"] is False
                  and m3e["entrenamiento"]["modelo_liberado"] == "liberado",
                  "cancelada en base_media: se evalua (el entrenamiento termino bien), no se guarda el modelo y se libera")

        print("\n== 3f. Cancelacion durante la inferencia")
        c3f = dict(c3d, experimento="inferencia_cancelada")
        c3f["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 0, "fase_media_ms": 0}
        c3f["inferencia"] = {"activada": True, "duracion_min_s": 30}
        r3f = correr(orq, escribir_config(raiz, "config3f.json", c3f), cancelar=True, cancelar_en=b'"tarea":"inferencia"')
        m3f = json.loads((raiz / "corridas" / "inferencia_cancelada" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        inf3f = m3f["inferencia"]
        comprobar(r3f.codigo == SALIDA_CANCELADA and m3f["estado"] == "cancelada" and inf3f["status_dll"] == 4,
                  "cancelada en la inferencia: estado cancelada e inferir devolvio CANCELADO")
        comprobar(m3f["tiempos"]["fase_media_iniciada"] is False and m3f["tiempos"]["inferencia_iniciada"] is True,
                  "fase_media_ms 0: sin base intermedia (fase_media_iniciada false); la inferencia empezo")
        dur3f = (iso(m3f["tiempos"]["t_inf_despues"]) - iso(m3f["tiempos"]["t_inf_antes"])).total_seconds()
        comprobar(dur3f < 10, f"la cancelacion corta la inferencia de 30 s ({dur3f:.1f} s)")
        n_val3f = m3f["datos"]["n_val"]
        comprobar(inf3f["n_predicciones"] > 0 and inf3f["n_predicciones"] == inf3f["pasadas"] * n_val3f
                  and inf3f["tiempo_por_prediccion_us"] is None and inf3f["motivo_sin_por_prediccion"] == "inferencia cancelada",
                  "cancelada: n_predicciones de las pasadas completas y tiempo por prediccion null ('inferencia cancelada')")
        aud3f = {c["id"]: c for c in json.loads((raiz / "corridas" / "inferencia_cancelada" / "corrida_001"
                                                  / "auditoria.json").read_text(encoding="utf-8"))["comprobaciones"]}
        comprobar(aud3f["estado"]["motivo"] == "cancelada durante la inferencia: el entrenamiento está completo"
                  and aud3f["pasos_inferencia"]["resultado"] == "no_evaluable",
                  f"auditoria: {aud3f['estado']['motivo']}; pasos_inferencia no evaluable")
        comprobar(m3f["evaluacion"]["ejecutada"] and m3f["artefactos"]["modelo"]["guardado"] is False
                  and m3f["entrenamiento"]["modelo_liberado"] == "liberado",
                  "cancelada en la inferencia: se evalua, no se guarda el modelo y se libera")

        print("\n== 3g. Error de inferir (la DLL de prueba falla en la pasada 1000)")
        c3g = dict(c3d, experimento="inferencia_error")
        c3g["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 0, "fase_media_ms": 0}
        os.environ["ENERGIA_PRUEBA_INFERIR_FALLA"] = "1000"
        try:
            r3g = correr(orq, escribir_config(raiz, "config3g.json", c3g))
        finally:
            del os.environ["ENERGIA_PRUEBA_INFERIR_FALLA"]
        m3g = json.loads((raiz / "corridas" / "inferencia_error" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        inf3g = m3g["inferencia"]
        comprobar(r3g.codigo == SALIDA_ERROR and m3g["estado"] == "error" and m3g["error"]["tipo"] == "inferencia"
                  and inf3g["status_dll"] == 3,
                  f"error de inferir: codigo {SALIDA_ERROR}, estado error con error.tipo = inferencia ({m3g['error']})")
        comprobar(inf3g["n_predicciones"] == 999 * m3g["datos"]["n_val"] and inf3g["tiempo_por_prediccion_us"] is None
                  and inf3g["motivo_sin_por_prediccion"] == "error de inferir",
                  "error de inferir: n_predicciones sin la pasada interrumpida y tiempo por prediccion null ('error de inferir')")
        aud3g = {c["id"]: c for c in json.loads((raiz / "corridas" / "inferencia_error" / "corrida_001"
                                                  / "auditoria.json").read_text(encoding="utf-8"))["comprobaciones"]}
        comprobar(aud3g["estado"]["motivo"].startswith("error de inferir: el entrenamiento está completo"),
                  f"auditoria: {aud3g['estado']['motivo']}")
        comprobar(m3g["evaluacion"]["ejecutada"] and m3g["artefactos"]["modelo"]["guardado"] is False
                  and m3g["entrenamiento"]["modelo_liberado"] == "liberado"
                  and m3g["tiempos"]["fase_posterior_iniciada"] is False,
                  "error de inferir: se evalua, no se guarda el modelo, se libera y no hay fase posterior")

        print("\n== 3h. predecir falla solo en el conjunto de entrenamiento (D-015)")
        c3h = config_base(raiz, dll, "predecir_error", 2, 50)
        os.environ["ENERGIA_PRUEBA_PREDECIR_FALLA"] = "2"  # la llamada 2: validacion va primero
        try:
            r3h = correr(orq, escribir_config(raiz, "config3h.json", c3h))
        finally:
            del os.environ["ENERGIA_PRUEBA_PREDECIR_FALLA"]
        dir3h = raiz / "corridas" / "predecir_error" / "corrida_001"
        m3h = json.loads((dir3h / "metadata.json").read_text(encoding="utf-8"))
        comprobar(r3h.codigo == SALIDA_COMPLETADA and m3h["estado"] == "completada" and m3h["error"] is None
                  and (dir3h / "modelo.bin").is_file(),
                  "predecir falla en entrenamiento: la corrida no cambia de estado (completada, con modelo)")
        mm3h = json.loads((dir3h / "metricas_modelo.json").read_text(encoding="utf-8"))
        val3h, ent3h = mm3h["validacion"], mm3h["entrenamiento"]
        comprobar(m3h["artefactos"]["metricas_modelo"] == {"archivo": "metricas_modelo.json", "tipo": "derivado"}
                  and val3h["status_dll"] == 0 and val3h["motivo"] is None and val3h["exactitud"] is not None,
                  "metricas_modelo.json existe y la validacion tiene sus metricas")
        comprobar(ent3h["status_dll"] == 3 and ent3h["motivo"] == "predecir devolvio: error interno de la DLL"
                  and all(ent3h[k] is None for k in ("matriz_confusion", "exactitud", "por_clase", "macro", "ponderado"))
                  and ent3h["n_filas"] == m3h["datos"]["n_train"],
                  f"el entrenamiento queda con status_dll 3, motivo y metricas en null ({ent3h['motivo']})")
        comprobar("predecir (entrenamiento): error interno de la DLL (codigo 3)" in r3h.stderr,
                  "diagnostico con el conjunto y el codigo")
        comprobar(mm3h["consistencia"]["coincide"] is True, "la validacion sigue coincidiendo con val_accuracy")

        print("\n== 3b. Sin 'tiempos' en config.json: linea base de 20 s (se cancela en la fase previa)")
        c3b = config_base(raiz, dll, "linea_base_defecto", 5, 100)
        del c3b["tiempos"]
        t0 = time.monotonic()
        r3b = correr(orq, escribir_config(raiz, "config3b.json", c3b), cancelar=True, cancelar_en=b'"fase_previa"')
        dur3b = time.monotonic() - t0
        m3b = json.loads((raiz / "corridas" / "linea_base_defecto" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        comprobar(m3b["tiempos"]["fase_previa_ms"] == 20000 and m3b["tiempos"]["fase_posterior_ms"] == 20000,
                  f"metadata: fases previa y posterior de 20000 ms por defecto ({m3b['tiempos']['fase_previa_ms']}, "
                  f"{m3b['tiempos']['fase_posterior_ms']})")
        comprobar(r3b.codigo == SALIDA_CANCELADA and m3b["estado"] == "cancelada" and not eventos(r3b, "entrenamiento_terminado")
                  and dur3b < 15.0,
                  f"cancelada durante la fase previa, sin entrenar, en {dur3b:.1f} s (codigo {r3b.codigo})")

        # ------------------------------------------------------------ 4. CSV
        print("\n== 4. Corrida con CSV (separador ';', coma decimal)")
        ruta_csv = raiz / "mini.csv"
        # La columna objetivo va en medio: columnas_entrada debe conservar el orden sin ella (D1)
        lineas = ["x1;etiqueta;x2"] + [f"{(i % 7) * 0.5:.1f};{i % 2};{i % 3:.1f}".replace(".", ",") for i in range(40)]
        ruta_csv.write_text("\n".join(lineas) + "\n", encoding="utf-8")
        c4 = config_base(raiz, dll, "prueba_csv", 2, 50)
        c4["datos"] = {"origen": "csv", "ruta": str(ruta_csv), "columna_objetivo": "etiqueta"}
        r4 = correr(orq, escribir_config(raiz, "config4.json", c4))
        m4 = json.loads((raiz / "corridas" / "prueba_csv" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        comprobar(r4.codigo == SALIDA_COMPLETADA and m4["estado"] == "completada", "corrida con CSV completada")
        d4 = m4["datos"]
        comprobar(d4["origen"] == "csv" and d4["columna_objetivo"] == "etiqueta" and d4["n_filas_total"] == 40
                  and d4["separador"] == ";" and d4["n_cols"] == 2, "metadata: CSV con ';', columna 'etiqueta', 40 filas, 2 columnas")
        comprobar(d4.get("columnas_entrada") == ["x1", "x2"] and d4.get("indice_objetivo") == 1
                  and d4.get("columnas_generadas") is False,
                  "metadata: columnas_entrada en orden sin la objetivo, indice_objetivo y columnas_generadas = false (D1)")
        comprobar(d4.get("sha256") == hashlib.sha256(ruta_csv.read_bytes()).hexdigest()
                  and d4.get("tam_bytes") == ruta_csv.stat().st_size and isinstance(d4.get("fecha_modificacion"), str)
                  and d4["fecha_modificacion"].endswith("Z"),
                  "metadata: sha256 del CSV (el mismo que hashlib), tam_bytes y fecha_modificacion, calculados al preparar")
        comprobar_protocolo(r4, "CSV")

        # ------------------------------------------------------------ 4b. nombre con tildes
        print("\n== 4b. Nombre del experimento con tildes y espacios")
        r4b = correr(orq, escribir_config(raiz, "config4b.json", config_base(raiz, dll, "Árbol HEPMASS", 2, 50)))
        dir4b = raiz / "corridas" / "_rbol_HEPMASS" / "corrida_001"
        comprobar(r4b.codigo == SALIDA_COMPLETADA and dir4b.is_dir(), "la carpeta usa el nombre seguro: corridas/_rbol_HEPMASS")
        m4b = json.loads((dir4b / "metadata.json").read_text(encoding="utf-8"))
        comprobar(m4b["experimento"] == "Árbol HEPMASS" and m4b["experimento_carpeta"] == "_rbol_HEPMASS"
                  and m4b["id_corrida"] == "_rbol_HEPMASS/corrida_001",
                  f"metadata: experimento original, experimento_carpeta e id_corrida de la carpeta ({m4b['experimento']}, "
                  f"{m4b['experimento_carpeta']}, {m4b['id_corrida']})")
        ini4b = eventos(r4b, "inicio")
        comprobar(bool(ini4b) and ini4b[0]["experimento"] == "Árbol HEPMASS" and ini4b[0]["experimento_carpeta"] == "_rbol_HEPMASS",
                  "evento inicio: nombre original y carpeta")

        # ------------------------------------------------------------ 5. configuracion invalida
        print("\n== 5. Configuraciones invalidas")
        c5 = config_base(raiz, dll, "invalido", 2, 50)
        c5["modelo"]["dll"] = str(raiz / "no_existe.dll")
        r5 = correr(orq, escribir_config(raiz, "config5.json", c5))
        comprobar(r5.codigo == SALIDA_ERROR, f"DLL inexistente: codigo {SALIDA_ERROR} (obtenido {r5.codigo})")
        comprobar(len(r5.eventos) == 1 and r5.eventos[0]["evento"] == "fin" and r5.eventos[0]["estado"] == "error"
                  and r5.eventos[0]["error"]["tipo"] == "configuracion" and r5.eventos[0]["dir_corrida"] is None,
                  "DLL inexistente: un unico evento fin con estado error y tipo configuracion")
        comprobar(not (raiz / "corridas" / "invalido").exists(), "DLL inexistente: no se crea carpeta de corrida")
        comprobar_protocolo(r5, "config invalida")

        c6 = config_base(raiz, dll, "hiper_invalido", 2, 50)
        c6["modelo"]["hiperparametros"]["pasos"] = 0  # rango 1..1000
        r6 = correr(orq, escribir_config(raiz, "config6.json", c6))
        comprobar(r6.codigo == SALIDA_ERROR and r6.eventos and r6.eventos[-1]["error"]["tipo"] == "configuracion"
                  and not (raiz / "corridas" / "hiper_invalido").exists(),
                  "hiperparametro fuera de rango: error de configuracion sin crear corrida")

        c7 = config_base(raiz, dll, "clave_desconocida", 2, 50)
        c7["modelo"]["hiperparametros"]["no_existe"] = 1
        r7 = correr(orq, escribir_config(raiz, "config7.json", c7))
        comprobar(r7.codigo == SALIDA_ERROR and not (raiz / "corridas" / "clave_desconocida").exists(),
                  "hiperparametro desconocido: rechazado sin crear corrida")

        # Seccion "inferencia" y tiempos.fase_media_ms (D-014): rechazadas sin crear la corrida
        invalidas_inf = [
            ("lote 0", {"inferencia": {"activada": True, "lote": 0}}),
            ("lote mayor que 1.000.000", {"inferencia": {"activada": True, "lote": 1000001}}),
            ("duracion_min_s 0", {"inferencia": {"activada": True, "duracion_min_s": 0}}),
            ("duracion_min_s mayor que 3600", {"inferencia": {"activada": True, "duracion_min_s": 3601}}),
            ("duracion_min_s no entera", {"inferencia": {"activada": True, "duracion_min_s": 1.5}}),
            ("activada no booleana", {"inferencia": {"activada": "si"}}),
            ("clave desconocida", {"inferencia": {"activada": True, "archivo": "otro.csv"}}),
            ("inferencia no es un objeto", {"inferencia": True}),
            ("fase_media_ms negativa", {"tiempos": {"fase_previa_ms": 0, "fase_posterior_ms": 0, "fase_media_ms": -1}}),
            ("fase_media_ms mayor que 3.600.000", {"tiempos": {"fase_previa_ms": 0, "fase_posterior_ms": 0,
                                                              "fase_media_ms": 3600001}}),
        ]
        for i, (que, extra) in enumerate(invalidas_inf):
            ci = config_base(raiz, dll, f"inf_invalida_{i}", 2, 50)
            ci.update(extra)
            ri = correr(orq, escribir_config(raiz, f"config_inf{i}.json", ci))
            comprobar(ri.codigo == SALIDA_ERROR and len(ri.eventos) == 1 and ri.eventos[0]["estado"] == "error"
                      and ri.eventos[0]["error"]["tipo"] == "configuracion"
                      and not (raiz / "corridas" / f"inf_invalida_{i}").exists(),
                      f"inferencia con {que}: error de configuracion sin crear corrida ({ri.eventos[0]['error']['mensaje'] if ri.eventos else ''})")

        r8 = correr(orq, escribir_config(raiz, "config8.json", {"version_config": 99}))
        comprobar(r8.codigo == SALIDA_ERROR and r8.eventos and r8.eventos[0]["estado"] == "error",
                  "version_config desconocida: rechazada")

        proc = subprocess.run([str(orq), "--ejecutar"], capture_output=True)
        comprobar(proc.returncode == 2, f"--ejecutar sin ruta: codigo 2 (obtenido {proc.returncode})")

        # ------------------------------------------------------------ 6. monitor de software (F5)
        print("\n== 6. Corrida con el monitor de software (system_meter, F5)")
        c9 = config_base(raiz, dll, "prueba_f5", 4, 1000)  # 4 pasos de 1 s de carga de un hilo
        c9["medidores"] = {"sistema": True}
        # Fase posterior de 1,5 s: sin ella, stop.flag se crea justo cuando el medidor escribe la
        # fila de t_start + 4 s, y en un equipo lento (runner del CI) el medidor ve antes stop.flag
        # y escribe solo 3 filas. Con la fase posterior, el numero de filas lo fijan los tiempos.
        c9["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 1500}
        r9 = correr(orq, escribir_config(raiz, "config9.json", c9), timeout_s=90)
        t9 = json.loads((raiz / "corridas" / "prueba_f5" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))["tiempos"]
        post9 = (iso(t9["t_stop"]) - iso(t9["t_despues"])).total_seconds()
        comprobar(post9 >= 1.5, f"la linea base posterior nunca dura menos de lo pedido (1,5 s pedidos: {post9:.3f} s)")
        comprobar(r9.codigo == SALIDA_COMPLETADA, f"corrida con medidor completada (codigo {r9.codigo})")
        comprobar_protocolo(r9, "monitor de software")
        ini9 = eventos(r9, "inicio")
        comprobar(bool(ini9) and ini9[0].get("medidores") == ["sistema"], "evento inicio: medidores ['sistema']")
        med9 = eventos(r9, "medidor")
        comprobar([(e["nombre"], e["estado"]) for e in med9] == [("sistema", "lanzado"), ("sistema", "terminado")]
                  and med9[0]["pid"] > 0 and med9[1]["codigo"] == 0,
                  f"eventos del medidor: lanzado y terminado con codigo 0 ({[(e['nombre'], e['estado']) for e in med9]})")
        fases9 = [e["fase"] for e in eventos(r9, "fase")]
        comprobar("deteniendo_medidores" in fases9 and fases9.index("deteniendo_medidores") < fases9.index("cerrando_modelo"),
                  "los medidores se detienen antes de evaluar y cerrar el modelo")
        dir9 = raiz / "corridas" / "prueba_f5" / "corrida_001"
        m9 = json.loads((dir9 / "metadata.json").read_text(encoding="utf-8"))
        med_meta = m9["medidores"]
        comprobar(len(med_meta) == 1 and med_meta[0]["nombre"] == "sistema" and med_meta[0]["codigo_salida"] == 0
                  and not med_meta[0]["termino_antes"] and not med_meta[0]["forzado"],
                  "metadata.json: medidor 'sistema' terminado con codigo 0, sin forzar")
        comprobar(m9["tiempos"]["margen_inicio_ms"] >= 2000 and m9["tiempos"]["margen_inicio_solicitado_ms"] == 0,
                  "con medidores, margen minimo de 2 s antes de t_start")
        info9 = json.loads((dir9 / "sistema_info.json").read_text(encoding="utf-8"))
        comprobar(info9["estado"] == "terminado" and info9["motivo_fin"] == "stop_flag",
                  f"sistema_info.json: terminado por stop.flag ({info9['estado']}, {info9['motivo_fin']})")
        t_start9 = iso(m9["tiempos"]["t_start"])
        comprobar(iso(info9["t_start"]) == t_start9 and not info9["inicio_tardio"],
                  "el medidor usa el t_start de la corrida y empezo a tiempo")
        with (dir9 / "sistema_metrics.csv").open(encoding="utf-8", newline="") as f:
            filas9 = list(csv.DictReader(f))
        t_antes9, t_despues9 = iso(m9["tiempos"]["t_antes"]), iso(m9["tiempos"]["t_despues"])
        t_stop9 = iso(m9["tiempos"]["t_stop"])
        # Toda fila de la rejilla que termina al menos 1 s antes de stop.flag debe estar escrita
        esperadas9 = int((t_stop9 - t_start9).total_seconds()) - 1
        comprobar(len(filas9) >= max(4, esperadas9),
                  f"sistema_metrics.csv: {len(filas9)} filas (al menos {max(4, esperadas9)} antes de t_stop)")
        ok_rejilla = all(iso(f["timestamp"]) == t_start9 + timedelta(seconds=i + 1) for i, f in enumerate(filas9))
        comprobar(ok_rejilla, "sistema_metrics.csv: filas a 1 s alineadas con t_start (D-007)")
        # Filas cuyo intervalo completo cae dentro del entrenamiento
        dentro = [f for f in filas9 if iso(f["timestamp"]) - timedelta(seconds=1) >= t_antes9
                  and iso(f["timestamp"]) <= t_despues9]
        cpu = [float(f["orq_cpu_s"]) for f in dentro if f["orq_cpu_s"]]
        # Mediana y no cada fila, con minimo 0,5: en un equipo compartido y saturado (runner de
        # GitHub Actions) el hilo de entrenamiento recibe menos de un nucleo (ver test_system_meter.py)
        comprobar(len(cpu) >= 2 and 0.5 <= statistics.median(cpu) <= 1.15 and max(cpu) <= 1.15,
                  f"durante el entrenamiento orq_cpu_s ~ 1,0 por fila: se mide el orquestador (D-010) ({cpu})")
        comprobar(all(f["med_cpu_s"] != "" for f in filas9), "consumo propio del medidor registrado (med_cpu_s)")
        comprobar((dir9 / "stop.flag").exists(), "stop.flag queda en la corrida como registro del fin de la medicion")

        c10 = config_base(raiz, dll, "medidor_desconocido", 2, 50)
        c10["medidores"] = {"inexistente": True}
        r10 = correr(orq, escribir_config(raiz, "config10.json", c10))
        comprobar(r10.codigo == SALIDA_ERROR and not (raiz / "corridas" / "medidor_desconocido").exists(),
                  "medidor desconocido: error de configuracion sin crear corrida")

        # ------------------------------------------------------------ 7. monitor fisico de energia (F6/F7)
        print("\n== 7. Monitor fisico de energia sin Atorch")
        c11 = config_base(raiz, dll, "energia_sin_entorno", 2, 50)
        c11["medidores"] = {"energia": True}
        c11["energia_ble"] = {"python": str(raiz / "no_existe" / "python.exe")}
        r11 = correr(orq, escribir_config(raiz, "config11.json", c11))
        fin11 = eventos(r11, "fin")
        comprobar(r11.codigo == SALIDA_ERROR and fin11 and fin11[0]["error"]["tipo"] == "medidor"
                  and "crear_entorno" in fin11[0]["error"]["mensaje"],
                  f"sin el entorno de Python: error de tipo medidor que indica crear_entorno.bat "
                  f"({fin11[0]['error'] if fin11 else None})")
        comprobar(not eventos(r11, "entrenamiento_terminado"), "sin el entorno de Python no se entrena")

        if script_ble:
            c12 = config_base(raiz, dll, "energia_sin_atorch", 2, 50)
            c12["medidores"] = {"energia": True}
            c12["energia_ble"] = {"python": sys.executable, "script": str(script_ble)}
            r12 = correr(orq, escribir_config(raiz, "config12.json", c12), timeout_s=60)
            fin12 = eventos(r12, "fin")
            comprobar(r12.codigo == SALIDA_ERROR and fin12 and fin12[0]["error"]["tipo"] == "medidor"
                      and "t_start" in fin12[0]["error"]["mensaje"],
                      f"energia_ble.py sin Atorch (ni bleak): la corrida se aborta en t_start ({fin12[0]['error'] if fin12 else None})")
            comprobar(not eventos(r12, "entrenamiento_terminado"), "la corrida abortada en t_start no entrena")
            estados12 = [e["estado"] for e in eventos(r12, "medidor")]
            # Sin bleak, energia_ble.py sale enseguida (codigo 4): termino antes de tiempo. Con bleak
            # instalado en este Python, sigue buscando el Atorch hasta que la corrida aborta en
            # t_start y lo detiene. En los dos casos la corrida no entrena (comprobado arriba).
            if importlib.util.find_spec("bleak") is None:
                comprobar(estados12[:1] == ["lanzado"] and "termino_antes" in estados12,
                          "el medidor se lanzo y termino antes de tiempo")
            else:
                comprobar(estados12[:1] == ["lanzado"] and len(estados12) >= 2,
                          f"con bleak instalado: el medidor se lanzo y se detuvo al abortar ({estados12})")
            comprobar_protocolo(r12, "energia sin Atorch")

        print("\n== 8. Orquestacion con el DOBLE DE PRUEBA del medidor de energia (potencia inventada)")
        doble = Path(__file__).with_name("medidor_energia_doble.py")
        c13 = config_base(raiz, dll, "energia_doble", 3, 1000)
        c13["medidores"] = {"energia": True}
        c13["energia_ble"] = {"python": sys.executable, "script": str(doble), "mac": "00:11:22:33:44:55"}
        r13 = correr(orq, escribir_config(raiz, "config13.json", c13), timeout_s=90)
        comprobar(r13.codigo == SALIDA_COMPLETADA, f"corrida con el doble completada (codigo {r13.codigo})")
        comprobar_protocolo(r13, "doble de energia")
        dir13 = raiz / "corridas" / "energia_doble" / "corrida_001"
        m13 = json.loads((dir13 / "metadata.json").read_text(encoding="utf-8"))
        comprobar(m13["tiempos"]["margen_inicio_ms"] >= 10000, "con el medidor de energia, margen minimo de 10 s")
        comprobar(m13["medidores"][0]["nombre"] == "energia" and m13["medidores"][0]["listo_en_t_start"] is True,
                  "metadata: medidor 'energia' listo en t_start (energia_lista.flag)")
        ev13 = eventos(r13, "energia_experimento")
        comprobar(len(ev13) == 1 and ev13[0]["archivo"] == "resultados.json", "evento energia_experimento")
        res = json.loads((dir13 / "resultados.json").read_text(encoding="utf-8"))
        ee = res["energia_experimento"]
        comprobar(ee["metodo"] == "trapecio_tramas_unicas" and ee["version_metodo"] == 1 and ee["fuente"] == "energia_tramas.csv",
                  "resultados.json: metodo, version y fuente (D-008)")
        ent = ee["ventanas"]["entrenamiento"]
        dur = ent["duracion_s"]
        # Sin fase posterior la medicion se detiene al terminar el entrenamiento: despues de la
        # ultima trama no llega otra y el final de la ventana queda como hueco 'borde_final'
        # (no se extrapola, D-008). La energia es exactamente 40 W por el tiempo cubierto.
        comprobar(abs(ent["energia_j"] - 40.0 * ent["segundos_cubiertos"]) < 1e-6
                  and all(h["tipo"] in ("borde_inicial", "borde_final") for h in ent["huecos"]),
                  f"entrenamiento sin fase posterior: {ent['energia_j']:.3f} J = 40 W x {ent['segundos_cubiertos']:.3f} s cubiertos "
                  f"de {dur:.3f} s (cobertura {ent['cobertura']:.3f}, sin huecos entre tramas)")
        comprobar("base_post" not in ee["ventanas"] and "base_pre" in ee["ventanas"],
                  "sin fase posterior no hay ventana base_post; base_pre existe aunque dure milisegundos")
        comprobar(m13["artefactos"]["resultados"]["archivo"] == "resultados.json"
                  and m13["artefactos"]["resultados"]["tipo"] == "derivado",
                  "metadata.json declara resultados.json como dato derivado")
        c14 = config_base(raiz, dll, "energia_doble_post", 3, 1000)
        c14["medidores"] = {"energia": True}
        c14["energia_ble"] = {"python": sys.executable, "script": str(doble)}
        c14["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 1500}
        r14 = correr(orq, escribir_config(raiz, "config14.json", c14), timeout_s=90)
        res14 = json.loads((raiz / "corridas" / "energia_doble_post" / "corrida_001" / "resultados.json").read_text(encoding="utf-8"))
        ent14 = res14["energia_experimento"]["ventanas"]["entrenamiento"]
        m14 = json.loads((raiz / "corridas" / "energia_doble_post" / "corrida_001" / "metadata.json").read_text(encoding="utf-8"))
        v14 = res14["energia_experimento"]["ventanas"]
        post14 = v14.get("base_post", {})
        comprobar(res14["version_resultados"] == 3 and abs(post14.get("duracion_s", 0) - 1.5) < 0.3
                  and abs(post14.get("energia_j", -1) - 40.0 * post14.get("segundos_cubiertos", 0)) < 1e-6
                  and "base_pre" in v14 and "base_media" not in v14 and "inferencia" not in v14,
                  f"resultados.json v3: base_post de la fase posterior ({post14.get('duracion_s')} s, "
                  f"{post14.get('energia_j')} J en bruto) y base_pre; sin inferencia, sin base_media ni inferencia")
        comprobar(res14["energia_experimento"]["fase_posterior_iniciada"] == {"valor": True, "origen": "metadata"},
                  "resultados.json registra fase_posterior_iniciada y su origen (metadata)")
        comprobar(m14["tiempos"]["fase_posterior_iniciada"] is True, "con 1,5 s de fase posterior: fase_posterior_iniciada = true")
        comprobar(r14.codigo == SALIDA_COMPLETADA and ent14["cobertura"] > 0.999 and not ent14["huecos"]
                  and abs(ent14["energia_j"] - 40.0 * ent14["duracion_s"]) < 1e-6,
                  f"con 1,5 s de fase posterior la ventana de entrenamiento queda cubierta: {ent14['energia_j']:.3f} J "
                  f"= 40 W x {ent14['duracion_s']:.3f} s (cobertura {ent14['cobertura']:.3f})")

        # Inferencia con el doble (D-014): ventanas base_media e inferencia y energia por prediccion
        c15 = config_base(raiz, dll, "energia_doble_inferencia", 2, 500)
        c15["medidores"] = {"energia": True}
        c15["energia_ble"] = {"python": sys.executable, "script": str(doble)}
        c15["tiempos"] = {"fase_previa_ms": 0, "fase_posterior_ms": 1500, "fase_media_ms": 1500}
        c15["inferencia"] = {"activada": True, "duracion_min_s": 2}
        r15 = correr(orq, escribir_config(raiz, "config15.json", c15), timeout_s=90)
        dir15 = raiz / "corridas" / "energia_doble_inferencia" / "corrida_001"
        res15 = json.loads((dir15 / "resultados.json").read_text(encoding="utf-8"))
        m15 = json.loads((dir15 / "metadata.json").read_text(encoding="utf-8"))
        v15, inf15 = res15["energia_experimento"]["ventanas"], m15["inferencia"]
        media15, vinf15 = v15.get("base_media", {}), v15.get("inferencia", {})
        comprobar(r15.codigo == SALIDA_COMPLETADA and abs(media15.get("duracion_s", 0) - 1.5) < 0.1
                  and abs(media15.get("energia_j", -1) - 40.0 * media15.get("segundos_cubiertos", 0)) < 1e-6,
                  f"con inferencia: ventana base_media de ~1,5 s ({media15.get('duracion_s')} s, {media15.get('energia_j')} J)")
        dur15 = (iso(m15["tiempos"]["t_inf_despues"]) - iso(m15["tiempos"]["t_inf_antes"])).total_seconds()
        comprobar(abs(vinf15.get("duracion_s", 0) - dur15) < 1e-6 and vinf15.get("n_predicciones") == inf15["n_predicciones"]
                  and vinf15.get("tiempo_por_prediccion_us") == inf15["tiempo_por_prediccion_us"]
                  and abs(vinf15.get("energia_por_prediccion_j", -1) - vinf15.get("energia_j", 0) / inf15["n_predicciones"]) < 1e-15
                  and vinf15.get("motivo_sin_por_prediccion", "falta") is None,
                  f"ventana inferencia [t_inf_antes, t_inf_despues]: energia por prediccion = energia_j / n_predicciones "
                  f"({vinf15.get('energia_por_prediccion_j')} J) y el tiempo por prediccion copiado de metadata")
        post15 = v15.get("base_post", {})
        comprobar(post15.get("inicio") == vinf15.get("fin") and abs(post15.get("duracion_s", 0) - 1.5) < 0.3,
                  "base_post empieza en t_inf_despues")
        r_post15 = subprocess.run([str(orq), "--postprocesar", str(dir15)], capture_output=True)
        post15r = json.loads(r_post15.stdout.decode("utf-8")) if r_post15.returncode == 0 else {}
        comprobar(post15r.get("energia_experimento", {}).get("ventanas") == v15,
                  "--postprocesar reproduce las ventanas de la inferencia (con metadata.inferencia)")

        r_post = subprocess.run([str(orq), "--postprocesar", str(dir13)], capture_output=True)
        post = json.loads(r_post.stdout.decode("utf-8")) if r_post.returncode == 0 else {}
        comprobar(r_post.returncode == 0 and post.get("energia_experimento", {}).get("ventanas") == ee["ventanas"],
                  "--postprocesar reproduce los mismos resultados")

        if args.conservar:
            import shutil
            shutil.rmtree(args.conservar, ignore_errors=True)
            shutil.copytree(raiz, args.conservar)

        tmps = [x for x in raiz.rglob("*") if x.suffix.lower() == ".tmp" or x.name.startswith("qt_temp")]
        comprobar(not tmps, f"no quedan archivos temporales en la salida ({len(tmps)})")

    # ---------------------------------------------------------------- procesos
    time.sleep(0.5)
    comprobar(procesos_orquestador() == procesos_antes,
              f"no quedan procesos energia_orquestador.exe vivos (antes {procesos_antes}, ahora {procesos_orquestador()})")

    print(f"\n{'HARNESS F3 OK' if FALLOS == 0 else 'HARNESS F3 CON FALLOS'}: {FALLOS} fallo(s)")
    return 0 if FALLOS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
