.pragma library

// Textos breves de ayuda sobre cada métrica, su unidad y su fuente. Los mismos en todas las
// vistas (Ejecución, Resultados, Dashboard, Comparar). Separan las tres familias:
// desempeño del modelo, energía (monitor físico) y recursos (monitor de software), y la
// duración como información del experimento (D-002, D-003, D-004).

var textos = {
    // Información del experimento
    duracion: "Tiempo que tardó el entrenamiento (de t_antes a t_despues). Es información del experimento: no es una métrica de energía ni de recursos (D-003).",

    // Desempeño del modelo
    val_accuracy: "Exactitud en validación (accuracy): proporción de filas del conjunto de validación que el modelo clasifica bien (de 0 a 1; se muestra en %). Se calcula al final, con los monitores ya detenidos.",
    val_loss: "Pérdida (loss) en validación: error medio del modelo sobre el conjunto de validación. Cuanto menor, mejor. No tiene unidad física.",
    modelo: "Métricas que la DLL del modelo calcula sobre los datos de validación. Miden la calidad del modelo, no su consumo.",

    // Energía: monitor físico (Atorch S1BW)
    grupo_energia: "Medidas del monitor físico de energía: el medidor Atorch S1BW, conectado entre el enchufe y el cargador o la fuente del equipo, mide el consumo eléctrico de todo el equipo (no solo del modelo).",
    potencia_w: "Potencia (W, vatios): ritmo de consumo eléctrico del equipo en cada instante, medido por el Atorch. 1 W = 1 julio por segundo.",
    energia: "Energía del experimento (Wh, vatios-hora): potencia acumulada en el tiempo durante el entrenamiento, calculada por EnergIA integrando la potencia medida (D-008). 1 Wh = 3600 J. No es el contador interno del medidor.",
    voltaje_v: "Voltaje (V, voltios): tensión de la red eléctrica en el enchufe, medida por el Atorch. Suele variar poco; no indica consumo por sí solo.",
    corriente_a: "Corriente (A, amperios): intensidad eléctrica que toma el equipo de la red, medida por el Atorch. Potencia ≈ voltaje × corriente × factor de potencia.",

    // Recursos: monitor de software (sistema operativo)
    grupo_recursos: "Medidas del monitor de software: el sistema operativo informa cuánto procesador (CPU) y memoria (RAM) se usan. No son energía: un mismo uso de CPU puede consumir distinta energía en otro equipo.",
    cpu_proceso_pct: "CPU del proceso (% del equipo): parte de la capacidad total del procesador (todos los núcleos) que usa el proceso de ejecución, que incluye la DLL del modelo. 100 % = todos los núcleos ocupados.",
    cpu_sistema_pct: "CPU del sistema (%): uso del procesador de todo el equipo, incluidos los demás programas abiertos.",
    ram_proceso_mb: "RAM del proceso (MB): memoria física que ocupa el proceso de ejecución (conjunto de trabajo de Windows), en megabytes.",
    ram_sistema_pct: "RAM del sistema (%): porcentaje de la memoria física total del equipo que está en uso, por todos los programas."
};

function texto(clave) {
    return textos.hasOwnProperty(clave) ? textos[clave] : "";
}
