import QtQuick

Item {
    id: root
    property var values: []
    property color lineColor: "#22c55e"
    implicitHeight: 32

    onValuesChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            if (root.values.length < 2) return;
            var minV = Math.min.apply(Math, root.values);
            var maxV = Math.max.apply(Math, root.values);
            if (minV === maxV) maxV = minV + 1;
            ctx.strokeStyle = root.lineColor;
            ctx.lineWidth = 2;
            ctx.beginPath();
            for (var i = 0; i < root.values.length; i++) {
                var x = (width / (root.values.length - 1)) * i;
                var norm = (root.values[i] - minV) / (maxV - minV);
                var y = height - norm * height;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
        }
    }
}
