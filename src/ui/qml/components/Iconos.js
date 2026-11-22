.pragma library

// Iconos vectoriales de EnergIA: diseño propio del proyecto (sin bibliotecas de terceros), en
// una cuadrícula de 24x24 con trazo redondeado y sin relleno, como datos de ruta SVG.
// Los dibuja Icono.qml (QtQuick.Shapes, sin dependencias de red ni plugins de imagen) y
// scripts/generar_iconos_svg.py los vuelca como archivos .svg en src/ui/qml/icons/.
// Detalle y licencia: docs/mockups/ICONOS.md

var trazos = {
    // Navegación
    panel: "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z",
    nuevo: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 8v8 M8 12h8",
    matraz: "M9 3h6 M10 3v6l-5.3 9.3A1.8 1.8 0 0 0 6.3 21h11.4a1.8 1.8 0 0 0 1.6-2.7L14 9V3 M7.6 15h8.8",
    barras: "M3 20h18 M5 20v-7h3v7 M10.5 20V5h3v15 M16 20V10h3v10",
    base_datos: "M4 6a8 3 0 1 0 16 0a8 3 0 1 0-16 0z M4 6v12a8 3 0 0 0 16 0V6 M4 12a8 3 0 0 0 16 0",
    ajustes: "M4 7h8 M18 7h2 M15 4a3 3 0 1 0 0 6a3 3 0 1 0 0-6z M4 17h2 M12 17h8 M9 14a3 3 0 1 0 0 6a3 3 0 1 0 0-6z",

    // Monitores y métricas
    rayo: "M13 2L4 14h7l-1 8l9-12h-7l1-8z",
    monitor: "M3 4h18v12H3z M8 20h8 M12 16v4",
    cpu: "M7 7h10v10H7z M10 10h4v4h-4z M9 2v3 M15 2v3 M9 19v3 M15 19v3 M2 9h3 M2 15h3 M19 9h3 M19 15h3",
    memoria: "M3 6h18v10H3z M7 9.5h4v3H7z M13 9.5h4v3h-4z M7 16v3 M12 16v3 M17 16v3",
    enchufe: "M9 2v5 M15 2v5 M6 7h12v4a6 6 0 0 1-12 0z M12 17v5",
    // Voltímetro y amperímetro: los símbolos de los esquemas eléctricos
    voltaje: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M8.5 8l3.5 8.5L15.5 8",
    corriente: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M8.5 16.5L12 7.5l3.5 9 M9.8 13.4h4.4",
    modelo: "M5 3.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M5 15.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M12 9.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M19 9.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M6.9 7.6l3.2 2.8 M6.9 16.4l3.2-2.8 M14.5 12h2",
    reloj: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 7v5l3 2",
    linea_base: "M3 3v18h18 M7 13h2.5 M12 13h2.5 M17 13h2.5",

    // Archivos y datos
    archivo: "M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z M14 3v6h6 M8 13h8 M8 17h5",
    carpeta: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",

    // Notificaciones y estados
    campana: "M18 16v-5a6 6 0 0 0-12 0v5l-2 2h16z M10 21a2 2 0 0 0 4 0",
    check: "M5 12.5l4.5 4.5L19 7.5",
    // Solo el signo, para dentro de un círculo de color (notificaciones)
    exclamacion: "M12 6v8 M12 17.4a.6.6 0 1 0 0 1.2a.6.6 0 1 0 0-1.2z",
    letra_i: "M12 10.5v7 M12 6.4a.6.6 0 1 0 0 1.2a.6.6 0 1 0 0-1.2z",
    cerrar: "M6 6l12 12 M18 6L6 18",
    alerta: "M12 3.5L2.5 20h19z M12 10v4.5 M12 16.9a.6.6 0 1 0 0 1.2a.6.6 0 1 0 0-1.2z",
    ayuda: "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M9.5 9.5a2.5 2.5 0 1 1 3.6 2.2c-.7.4-1.1 1-1.1 1.8v.5 M12 16.9a.6.6 0 1 0 0 1.2a.6.6 0 1 0 0-1.2z",

    // Acciones
    reproducir: "M7 4v16l13-8z",
    detener: "M6 6h12v12H6z",
    subir: "M12 16V4 M7 9l5-5l5 5 M4 16v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3",
    descargar: "M12 4v12 M7 11l5 5l5-5 M4 16v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3",
    recargar: "M21 12a9 9 0 1 1-3-6.7L21 8 M21 3v5h-5",
    papelera: "M4 7h16 M9 7V4h6v3 M6 7l1 13h10l1-13 M10 11v6 M14 11v6",
    comparar: "M4 8h14 M14 4l4 4l-4 4 M20 16H6 M10 12l-4 4l4 4",
    atras: "M19 12H5 M11 6l-6 6l6 6",
    mas: "M12 5v14 M5 12h14"
};

function trazo(nombre) {
    return trazos.hasOwnProperty(nombre) ? trazos[nombre] : "";
}
