import QtQuick

Item {
    id: root
    property var series: []  // [{ name, color, values: [numbers] }]
    property int gridLines: 4
    // Unidad del eje vertical; con ella se rotulan el mínimo y el máximo de la escala y los
    // segundos del eje horizontal (un valor por segundo, rejilla de 1 s)
    property string unidad: ""
    implicitHeight: 220

    onSeriesChanged: canvas.requestPaint()
    onUnidadChanged: canvas.requestPaint()

    function rotulo(v, rango) {
        var dec = rango >= 100 ? 0 : (rango >= 10 ? 1 : 2);
        return Number(v).toFixed(dec) + (root.unidad !== "" ? " " + root.unidad : "");
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var w = width, h = height;
            var rotular = root.unidad !== "";
            var padding = 20;
            var padIzq = rotular ? 58 : padding;
            var padAbajo = rotular ? 30 : padding;
            var plotW = w - padIzq - padding;
            var plotH = h - padding - padAbajo;

            // Los valores ausentes (null) son huecos: la línea se corta, no se interpola
            var allValues = [];
            var largo = 0;
            for (var s = 0; s < root.series.length; s++) {
                largo = Math.max(largo, (root.series[s].values || []).length);
                allValues = allValues.concat((root.series[s].values || []).filter(function (v) {
                    return v !== null && v !== undefined && !isNaN(v);
                }));
            }
            var minV = allValues.length ? Math.min.apply(Math, allValues) : 0;
            var maxV = allValues.length ? Math.max.apply(Math, allValues) : 1;
            if (minV === maxV) maxV = minV + 1;

            ctx.strokeStyle = "#21262d";
            ctx.lineWidth = 1;
            for (var g = 0; g <= root.gridLines; g++) {
                var y = padding + (plotH / root.gridLines) * g;
                ctx.beginPath();
                ctx.moveTo(padIzq, y);
                ctx.lineTo(w - padding, y);
                ctx.stroke();
            }

            if (rotular && allValues.length > 0) {
                ctx.fillStyle = "#8b949e";
                ctx.font = "10px sans-serif";
                ctx.textAlign = "right";
                ctx.textBaseline = "middle";
                ctx.fillText(root.rotulo(maxV, maxV - minV), padIzq - 6, padding);
                ctx.fillText(root.rotulo(minV, maxV - minV), padIzq - 6, padding + plotH);
                ctx.textBaseline = "top";
                ctx.textAlign = "left";
                ctx.fillText("0 s", padIzq, padding + plotH + 8);
                ctx.textAlign = "right";
                ctx.fillText(Math.max(0, largo - 1) + " s", w - padding, padding + plotH + 8);
            }

            for (var i = 0; i < root.series.length; i++) {
                var serie = root.series[i];
                var values = serie.values;
                if (!values || values.length < 2) continue;
                ctx.strokeStyle = serie.color;
                ctx.lineWidth = 2;
                ctx.beginPath();
                var enTramo = false;
                for (var p = 0; p < values.length; p++) {
                    var v = values[p];
                    if (v === null || v === undefined || isNaN(v)) { enTramo = false; continue; }
                    // Escala común: una serie más corta (corrida más breve) no se estira
                    var x = padIzq + (plotW / (largo - 1)) * p;
                    var norm = (v - minV) / (maxV - minV);
                    var yPt = padding + plotH - norm * plotH;
                    if (!enTramo) ctx.moveTo(x, yPt);
                    else ctx.lineTo(x, yPt);
                    enTramo = true;
                }
                ctx.stroke();
            }
        }
    }
}
