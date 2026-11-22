"""Iconos de la interfaz: genera los .svg desde Iconos.js y comprueba que todo esté en orden.

Iconos.js (src/ui/qml/components) es la fuente: la interfaz dibuja esas rutas con Icono.qml.
Este script vuelca cada icono como archivo SVG en src/ui/qml/icons/ (para verlos, reutilizarlos
o documentarlos) y, con --comprobar, verifica sin escribir nada que:
  - cada ruta solo use comandos SVG válidos con la cantidad correcta de números;
  - los .svg estén al día con Iconos.js (ni de más ni de menos);
  - todo nombre de icono usado en el QML exista en Iconos.js (sin iconos rotos);
  - el QML no use emojis ni símbolos pictográficos como iconos.

Uso:  python scripts/generar_iconos_svg.py [--comprobar]
"""
import os
import re
import sys

RAIZ = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DIR_QML = os.path.join(RAIZ, "src", "ui", "qml")
ICONOS_JS = os.path.join(DIR_QML, "components", "Iconos.js")
DIR_SVG = os.path.join(DIR_QML, "icons")

PLANTILLA = ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
             'fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" '
             'stroke-linejoin="round">\n'
             '  <!-- EnergIA: icono "{nombre}" (diseño propio, ver docs/mockups/ICONOS.md) -->\n'
             '  <path d="{ruta}"/>\n'
             '</svg>\n')

# Números que pide cada comando de ruta SVG
ARGUMENTOS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7, "Z": 0}

# Pictogramas que no deben aparecer en el QML: emojis, dingbats, flechas y símbolos técnicos.
# Se permiten los signos tipográficos (—, …, –, ·, •) y las letras acentuadas.
PICTOGRAMAS = re.compile("[←-⇿⌀-⏿■-➿⤀-⥿⬀-⯿"
                         "〰\U0001f000-\U0001faff️]")


def leer_iconos():
    texto = open(ICONOS_JS, encoding="utf-8").read()
    cuerpo = texto[texto.index("var trazos"):]
    cuerpo = cuerpo[:cuerpo.index("};")]
    return dict(re.findall(r'^\s*(\w+):\s*"([^"]*)"', cuerpo, re.MULTILINE))


def errores_de_ruta(ruta):
    """Valida la sintaxis de una ruta SVG; devuelve una lista de errores."""
    fichas = re.findall(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", ruta)
    sobrante = re.sub(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|[\s,]", "", ruta)
    errores = []
    if sobrante:
        errores.append(f"caracteres no válidos: {sobrante!r}")
    if not fichas or fichas[0] not in "Mm":
        errores.append("debe empezar con M")
    i = 0
    while i < len(fichas):
        comando = fichas[i]
        if comando.upper() not in ARGUMENTOS:
            errores.append(f"comando desconocido o número suelto: {comando!r}")
            break
        i += 1
        numeros = []
        while i < len(fichas) and not fichas[i].isalpha():
            numeros.append(fichas[i])
            i += 1
        n = ARGUMENTOS[comando.upper()]
        if (n == 0 and numeros) or (n > 0 and (not numeros or len(numeros) % n)):
            errores.append(f"{comando} con {len(numeros)} número(s): {' '.join(numeros)}")
    return errores


def usos_en_qml():
    """Nombres de icono usados en el QML: literales en las propiedades icon/icono/nombre/kindIcon."""
    usos = {}
    patron = re.compile(r"(?<![.\w])(icon|icono|nombre|kindIcon)\s*:(.*)")
    for carpeta, _, archivos in os.walk(DIR_QML):
        for archivo in archivos:
            if not archivo.endswith(".qml"):
                continue
            ruta = os.path.join(carpeta, archivo)
            for n, linea in enumerate(open(ruta, encoding="utf-8"), 1):
                m = patron.search(linea)
                if m:
                    # Solo la expresión de esa propiedad (hasta ';' o la siguiente propiedad) y sin los literales que
                    # se comparan (x === "valor"): quedan los posibles nombres de icono
                    # (kindIcon es un mapa { tipo: "icono", ... }: cuentan todos sus valores)
                    corte = r";" if m.group(1) == "kindIcon" else r";|,\s*\w+\s*:"
                    expresion = re.split(corte, m.group(2))[0]
                    for lit in re.finditer(r'"([^"]*)"', expresion):
                        if expresion[:lit.start()].rstrip().endswith("="):
                            continue
                        literal = lit.group(1)
                        usos.setdefault(literal, []).append(f"{os.path.relpath(ruta, RAIZ)}:{n}")
    return usos


def pictogramas_en_qml():
    hallados = []
    for carpeta, _, archivos in os.walk(DIR_QML):
        for archivo in archivos:
            if archivo.endswith((".qml", ".js")):
                ruta = os.path.join(carpeta, archivo)
                for n, linea in enumerate(open(ruta, encoding="utf-8"), 1):
                    for c in PICTOGRAMAS.findall(linea):
                        hallados.append(f"{os.path.relpath(ruta, RAIZ)}:{n}: U+{ord(c):04X}")
    return hallados


def main():
    comprobar = "--comprobar" in sys.argv[1:]
    iconos = leer_iconos()
    fallos = []
    if not iconos:
        fallos.append("no se encontraron iconos en Iconos.js")

    for nombre, ruta in iconos.items():
        fallos += [f"icono {nombre}: {e}" for e in errores_de_ruta(ruta)]

    esperados = {f"{nombre}.svg": PLANTILLA.format(nombre=nombre, ruta=ruta) for nombre, ruta in iconos.items()}
    if comprobar:
        existentes = set(os.listdir(DIR_SVG)) if os.path.isdir(DIR_SVG) else set()
        for archivo, contenido in esperados.items():
            ruta = os.path.join(DIR_SVG, archivo)
            if archivo not in existentes:
                fallos.append(f"falta {os.path.relpath(ruta, RAIZ)}")
            elif open(ruta, encoding="utf-8", newline="").read().replace("\r\n", "\n") != contenido:
                fallos.append(f"{os.path.relpath(ruta, RAIZ)} no coincide con Iconos.js")
        for archivo in sorted(existentes - set(esperados)):
            if archivo.endswith(".svg"):
                fallos.append(f"sobra icons/{archivo}: no está en Iconos.js")
    elif not fallos:
        os.makedirs(DIR_SVG, exist_ok=True)
        for archivo in os.listdir(DIR_SVG):
            if archivo.endswith(".svg") and archivo not in esperados:
                os.remove(os.path.join(DIR_SVG, archivo))
        for archivo, contenido in esperados.items():
            with open(os.path.join(DIR_SVG, archivo), "w", encoding="utf-8", newline="\n") as f:
                f.write(contenido)
        print(f"{len(esperados)} iconos escritos en {os.path.relpath(DIR_SVG, RAIZ)}")

    for literal, lugares in sorted(usos_en_qml().items()):
        if literal and literal not in iconos:
            fallos.append(f"icono inexistente \"{literal}\" en {', '.join(lugares)}")

    fallos += [f"pictograma usado como icono: {p}" for p in pictogramas_en_qml()]

    for f in fallos:
        print("ERROR:", f)
    if fallos:
        return 1
    if comprobar:
        print(f"OK: {len(iconos)} iconos válidos, .svg al día, sin iconos rotos ni pictogramas en el QML")
    return 0


if __name__ == "__main__":
    sys.exit(main())
