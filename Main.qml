import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Controls.Material 2.15
import Qt.labs.platform 1.1 as Platform
import com.warehouse.optimizer 1.0
ApplicationWindow {
    id: mainRoot
    visible: true
    width: 1100
    height: 750
    title: "САПР Компоновки Склада (YandexGPT + AnyLogic)"

    // Добавлена структура стен (walls) в базовые данные
    property var lastGeneratedData: ({
        "walls": { "top": 0, "bottom": 40, "left": 0, "right": 60 },
        "layout": [], "nodes": [], "edges": []
    })
    property bool isDbConnected: false
    property string selectedObjectImage: "" // Добавь это свойство
    property real canvasScale: 20
    property real offsetX: 50
    property real offsetY: 50

    // --- УНИВЕРСАЛЬНАЯ СИСТЕМА ВЫДЕЛЕНИЯ ---
    property string selectedType: "none" // "object", "handle", "node", "wall", "none"
    property var selectedId: -1 // индекс в массиве, либо строка ("top", "bottom", "left", "right") для стен
    property bool isDragging: false
    property bool isRotating: false

    Material.theme: Material.Dark
    Material.accent: Material.Blue

    readonly property color colorPanel: "#1e1e1e"
    readonly property color colorBorder: "#333333"
    readonly property color colorText: "#e0e0e0"

    menuBar: MenuBar {
        Menu {
            title: "Файл"
            MenuItem { text: "Открыть базу данных..."; onTriggered: dbFileDialog.open() }
            MenuItem { text: "Выход"; onTriggered: Qt.quit() }
        }
        Menu {
            title: "Настройки"
            MenuItem { text: "Системный промпт"; onTriggered: promptDialog.open() }
            MenuItem { text: "Параметры ИИ"; onTriggered: aiSettingsDialog.open() }
        }
    }

    LayoutOptimizer {
        id: optimizer

        onOptimizationFinished: (updatedLayout, updatedNodes, updatedEdges) => {
            console.log("Оптимизация завершена!");
            mainRoot.lastGeneratedData.layout = updatedLayout;
            mainRoot.lastGeneratedData.nodes = updatedNodes;
            mainRoot.lastGeneratedData.edges = updatedEdges;

            // Explicitly force property changed event for QML bindings
            mainRoot.lastGeneratedData = mainRoot.lastGeneratedData;

            mainCanvas.requestPaint();
            busyLoading.running = false;
            statusLabel.text = "Оптимизация завершена";
            progressBar.value = 100;
            progressBar.visible = false;
        }

        onProgressUpdated: (percent) => {
            progressBar.value = percent;
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Справочная панель (информация об объекте)

        // ЛЕВАЯ ПАНЕЛЬ
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            color: colorPanel
            border.color: colorBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15

                Label { text: "Конфигурация"; font.bold: true; font.pixelSize: 18; color: Material.accent }

                TextField { id: inputWidth; placeholderText: "Ширина (м)"; text: "60"; Layout.fillWidth: true }
                TextField { id: inputLength; placeholderText: "Длина (м)"; text: "40"; Layout.fillWidth: true }

                TextField {
                    id: inputScale
                    text: mainRoot.canvasScale.toFixed(0)
                    Layout.fillWidth: true
                    horizontalAlignment: TextInput.AlignLeft
                    leftPadding: 35; rightPadding: 35
                    validator: IntValidator { bottom: 1; top: 500 }

                    Button { text: "◀"; width: 30; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; flat: true; onClicked: mainRoot.canvasScale = Math.max(1, mainRoot.canvasScale - 1) }
                    Button { text: "▶"; width: 30; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; flat: true; onClicked: mainRoot.canvasScale += 1 }
                    onEditingFinished: mainRoot.canvasScale = parseInt(text)
                }

                Label { text: "Инструкции:"; color: colorText }
                ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; TextArea { id: rulesArea; wrapMode: TextArea.Wrap; background: Rectangle { color: "#252525"; border.color: colorBorder } } }

                Button {
                    text: isDbConnected ? "СГЕНЕРИРОВАТЬ" : "БД НЕ ПОДКЛЮЧЕНА"
                    Layout.fillWidth: true
                    highlighted: true
                    enabled: isDbConnected
                    onClicked: {
                        statusLabel.text = "Генерация...";
                        let w = parseFloat(inputWidth.text) || 60;
                        let h = parseFloat(inputLength.text) || 40;

                        mainRoot.lastGeneratedData = {
                            "layout": [
                              {
                                "InstanceID": 1,
                                "ModelID": "m_cut_laser_pro",
                                "Type": "machine",
                                "Width": 4.5,
                                "Length": 2.2,
                                "Height": 2.0,
                                "CoordX": 10.0,
                                "CoordY": 5.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 2,
                                "ModelID": "f_mill_5ax",
                                "Type": "machine",
                                "Width": 3.5,
                                "Length": 2.8,
                                "Height": 3.0,
                                "CoordX": 10.0,
                                "CoordY": 15.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 3,
                                "ModelID": "sasta_ca6140",
                                "Type": "machine",
                                "Width": 2.8,
                                "Length": 1.4,
                                "Height": 1.6,
                                "CoordX": 10.0,
                                "CoordY": 25.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 4,
                                "ModelID": "rusdrill_compact",
                                "Type": "machine",
                                "Width": 1.2,
                                "Length": 1.0,
                                "Height": 1.8,
                                "CoordX": 15.0,
                                "CoordY": 25.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 5,
                                "ModelID": "bit_robotics_arm",
                                "Type": "robot",
                                "Width": 0.8,
                                "Length": 0.8,
                                "Height": 1.5,
                                "CoordX": 14.0,
                                "CoordY": 15.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 6,
                                "ModelID": "solos_pallet_heavy",
                                "Type": "rack",
                                "Width": 2.7,
                                "Length": 1.1,
                                "Height": 6.0,
                                "CoordX": 35.0,
                                "CoordY": 6.0,
                                "AngleRotation": 90.0
                              },
                              {
                                "InstanceID": 7,
                                "ModelID": "solos_pallet_heavy",
                                "Type": "rack",
                                "Width": 2.7,
                                "Length": 1.1,
                                "Height": 6.0,
                                "CoordX": 35.0,
                                "CoordY": 10.0,
                                "AngleRotation": 90.0
                              },
                              {
                                "InstanceID": 8,
                                "ModelID": "solos_pallet_heavy",
                                "Type": "rack",
                                "Width": 2.7,
                                "Length": 1.1,
                                "Height": 6.0,
                                "CoordX": 35.0,
                                "CoordY": 14.0,
                                "AngleRotation": 90.0
                              },
                              {
                                "InstanceID": 9,
                                "ModelID": "solos_pallet_heavy",
                                "Type": "rack",
                                "Width": 2.7,
                                "Length": 1.1,
                                "Height": 6.0,
                                "CoordX": 35.0,
                                "CoordY": 18.0,
                                "AngleRotation": 90.0
                              },
                              {
                                "InstanceID": 10,
                                "ModelID": "shelving_r30_light",
                                "Type": "rack",
                                "Width": 1.0,
                                "Length": 0.6,
                                "Height": 2.2,
                                "CoordX": 45.0,
                                "CoordY": 6.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 11,
                                "ModelID": "shelving_r30_light",
                                "Type": "rack",
                                "Width": 1.0,
                                "Length": 0.6,
                                "Height": 2.2,
                                "CoordX": 45.0,
                                "CoordY": 10.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 12,
                                "ModelID": "st_cons_rack",
                                "Type": "rack",
                                "Width": 4.0,
                                "Length": 1.5,
                                "Height": 3.5,
                                "CoordX": 42.0,
                                "CoordY": 25.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 13,
                                "ModelID": "robotech_agv_500",
                                "Type": "robot",
                                "Width": 1.2,
                                "Length": 0.8,
                                "Height": 0.4,
                                "CoordX": 25.0,
                                "CoordY": 2.0,
                                "AngleRotation": 0.0
                              },
                              {
                                "InstanceID": 14,
                                "ModelID": "robotech_agv_500",
                                "Type": "robot",
                                "Width": 1.2,
                                "Length": 0.8,
                                "Height": 0.4,
                                "CoordX": 25.0,
                                "CoordY": 28.0,
                                "AngleRotation": 0.0
                              }
                            ],
                            "nodes": [
                              { "NodeID": 0, "NodeX": 25.0, "NodeY": 2.0, "MarkerType": "start" },
                              { "NodeID": 1, "NodeX": 25.0, "NodeY": 6.0, "MarkerType": "path" },
                              { "NodeID": 2, "NodeX": 25.0, "NodeY": 15.0, "MarkerType": "path" },
                              { "NodeID": 3, "NodeX": 25.0, "NodeY": 25.0, "MarkerType": "path" },
                              { "NodeID": 4, "NodeX": 25.0, "NodeY": 28.0, "MarkerType": "path" },
                              { "NodeID": 5, "NodeX": 18.0, "NodeY": 5.0, "MarkerType": "path" },
                              { "NodeID": 6, "NodeX": 18.0, "NodeY": 15.0, "MarkerType": "path" },
                              { "NodeID": 7, "NodeX": 18.0, "NodeY": 25.0, "MarkerType": "path" },
                              { "NodeID": 8, "NodeX": 33.0, "NodeY": 4.0, "MarkerType": "path" },
                              { "NodeID": 9, "NodeX": 33.0, "NodeY": 8.0, "MarkerType": "path" },
                              { "NodeID": 10, "NodeX": 33.0, "NodeY": 12.0, "MarkerType": "path" },
                              { "NodeID": 11, "NodeX": 33.0, "NodeY": 16.0, "MarkerType": "path" },
                              { "NodeID": 12, "NodeX": 40.0, "NodeY": 4.0, "MarkerType": "path" },
                              { "NodeID": 13, "NodeX": 40.0, "NodeY": 8.0, "MarkerType": "path" },
                              { "NodeID": 14, "NodeX": 40.0, "NodeY": 25.0, "MarkerType": "path" },
                                { "NodeID": 15, "NodeX": 42.0, "NodeY": 6.0, "MarkerType": "path" },
                                { "NodeID": 16, "NodeX": 42.0, "NodeY": 10.0, "MarkerType": "path" }
                            ],
                            "edges": [
                                { "EdgeID": 0, "StartNodeID": 0, "EndNodeID": 1, "TwoWayTraffic": true },
                                { "EdgeID": 1, "StartNodeID": 1, "EndNodeID": 2, "TwoWayTraffic": true },
                                { "EdgeID": 2, "StartNodeID": 2, "EndNodeID": 3, "TwoWayTraffic": true },
                                { "EdgeID": 3, "StartNodeID": 3, "EndNodeID": 4, "TwoWayTraffic": true },
                                { "EdgeID": 4, "StartNodeID": 1, "EndNodeID": 5, "TwoWayTraffic": true },
                                { "EdgeID": 5, "StartNodeID": 2, "EndNodeID": 6, "TwoWayTraffic": true },
                                { "EdgeID": 6, "StartNodeID": 3, "EndNodeID": 7, "TwoWayTraffic": true },
                                { "EdgeID": 7, "StartNodeID": 1, "EndNodeID": 8, "TwoWayTraffic": true },
                                { "EdgeID": 8, "StartNodeID": 8, "EndNodeID": 9, "TwoWayTraffic": true },
                                { "EdgeID": 9, "StartNodeID": 9, "EndNodeID": 10, "TwoWayTraffic": true },
                                { "EdgeID": 10, "StartNodeID": 10, "EndNodeID": 11, "TwoWayTraffic": true },
                                { "EdgeID": 11, "StartNodeID": 8, "EndNodeID": 12, "TwoWayTraffic": true },
                                { "EdgeID": 12, "StartNodeID": 9, "EndNodeID": 13, "TwoWayTraffic": true },
                                { "EdgeID": 13, "StartNodeID": 12, "EndNodeID": 15, "TwoWayTraffic": true },
                                { "EdgeID": 14, "StartNodeID": 13, "EndNodeID": 16, "TwoWayTraffic": true },
                                { "EdgeID": 15, "StartNodeID": 3, "EndNodeID": 14, "TwoWayTraffic": true }
                            ],
                            "walls": {
                              "left": 0.0,
                              "top": 0.0,
                              "right": w,
                              "bottom": h
                            }
                          }
                        statusLabel.text = "Генерация...";
                        mainRoot.selectedType = "none";
                        mainRoot.selectedId = -1;
                        statusLabel.text = "Готово. Кликните на объект, стену или узел.";
                        btnSaveToDB.enabled = true;
                        mainCanvas.requestPaint(); gridCanvas.requestPaint();
                    }
                }

                Button {
                    id: btnOptimize
                    text: "ОПТИМИЗИРОВАТЬ ОТЖИГОМ"
                    Layout.fillWidth: true
                    enabled: isDbConnected && mainRoot.lastGeneratedData.layout.length > 0 && !busyLoading.running
                    onClicked: {
                        busyLoading.running = true;
                        progressBar.visible = true;
                        progressBar.value = 0;
                        statusLabel.text = "Оптимизация...";

                        optimizer.setEnvironment(
                            mainRoot.lastGeneratedData.walls,
                            mainRoot.lastGeneratedData.nodes,
                            mainRoot.lastGeneratedData.edges
                        );

                        optimizer.prepareLayout(mainRoot.lastGeneratedData.layout, dbManager);
                        optimizer.runAsyncOptimization(1.2);
                    }
                }

                Button {
                    id: btnSaveToDB
                    text: "ЗАПИСАТЬ В БД"
                    Layout.fillWidth: true; enabled: false
                    onClicked: {
                        // При сохранении берем актуальную ширину/длину из стен
                        let wls = mainRoot.lastGeneratedData.walls;
                        let actualW = wls.right - wls.left;
                        let actualH = wls.bottom - wls.top;
                        let res = dbManager.createProjectWithFullData("Проект " + Qt.formatDateTime(new Date(), "hh:mm"),
                                  actualW, actualH, mainRoot.lastGeneratedData);
                        statusLabel.text = (res !== -1) ? "✅ ID: " + res : "❌ Ошибка БД";
                    }
                }
            }
        }

        // ОБЛАСТЬ ХОЛСТА
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#161616"
            clip: true

            MouseArea {
                id: canvasMouseArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                property point lastPos

                function screenToWorld(x, y) {
                    return { x: (x - mainRoot.offsetX) / mainRoot.canvasScale, y: (y - mainRoot.offsetY) / mainRoot.canvasScale }
                }

                function hitTest(mx, my) {
                    let w = screenToWorld(mx, my);
                    let s = mainRoot.canvasScale;
                    let d = mainRoot.lastGeneratedData;
                    if (!d) return null;
                    // Main.qml ~строка 348 (внутри MouseArea)
                    let hitThreshold = 10 / s; // 10 пикселей терпимость для клика (адаптируется к масштабу)
                    // 1. Проверка ползунка вращения выделенного объекта
                    if (mainRoot.selectedType === "object" && d.layout[mainRoot.selectedId]) {
                        let obj = d.layout[mainRoot.selectedId];
                        let rad = obj.AngleRotation * Math.PI / 180;
                        let dx = w.x - obj.CoordX, dy = w.y - obj.CoordY;
                        let lx = dx * Math.cos(-rad) - dy * Math.sin(-rad);
                        let ly = dx * Math.sin(-rad) + dy * Math.cos(-rad);
                        if (Math.hypot(lx * s, (ly + 1.5) * s) <= 15) return { type: 'handle', id: mainRoot.selectedId };
                    }

                    // 2. Проверка узлов (точек пути)
                    for (let i = d.nodes.length - 1; i >= 0; i--) {
                        let n = d.nodes[i];
                        if (Math.hypot(w.x - n.NodeX, w.y - n.NodeY) <= (8 / s)) return { type: 'node', id: i };
                    }

                    // 3. Проверка клика по оборудованию
                    for (let i = d.layout.length - 1; i >= 0; i--) {
                            let obj = d.layout[i];
                            let rad = obj.AngleRotation * Math.PI / 180;
                            let dx = w.x - obj.CoordX, dy = w.y - obj.CoordY;

                            let lx = dx * Math.cos(-rad) - dy * Math.sin(-rad);
                            let ly = dx * Math.sin(-rad) + dy * Math.cos(-rad);

                            if (Math.abs(lx) <= obj.Width / 2 && Math.abs(ly) <= obj.Length / 2) {
                                return { type: 'object', id: i };
                            }
                        }

                    // 4. Проверка клика по стенам
                    if (d.walls) {
                        let wls = d.walls;
                        if (Math.abs(w.y - wls.top) <= hitThreshold && w.x >= wls.left && w.x <= wls.right) return { type: 'wall', id: 'top' };
                        if (Math.abs(w.y - wls.bottom) <= hitThreshold && w.x >= wls.left && w.x <= wls.right) return { type: 'wall', id: 'bottom' };
                        if (Math.abs(w.x - wls.left) <= hitThreshold && w.y >= wls.top && w.y <= wls.bottom) return { type: 'wall', id: 'left' };
                        if (Math.abs(w.x - wls.right) <= hitThreshold && w.y >= wls.top && w.y <= wls.bottom) return { type: 'wall', id: 'right' };
                    }

                    return null;
                }

                onPressed: (mouse) => {
                    lastPos = Qt.point(mouse.x, mouse.y)
                    if (mouse.button === Qt.LeftButton) {
                        let hit = hitTest(mouse.x, mouse.y);
                        if (hit) {
                            mainRoot.selectedType = hit.type;
                            mainRoot.selectedId = hit.id;
                            if (hit.type === 'handle')
                                       mainRoot.isRotating = true;
                            else mainRoot.isDragging = true;
                                       if (hit.type === 'object') {
                                                           let obj = mainRoot.lastGeneratedData.layout[hit.id];
                                                           if (obj) {
                                                               // Запрашиваем путь к картинке из C++
                                                               let dbData = dbManager.getEquipmentInfo(obj.ModelID);
                                                               // Сохраняем в свойство, которое мы создали в mainRoot
                                                               mainRoot.selectedObjectImage = dbData.imageUrl || "";
                                                           }
                                                       }
                        } else {
                            mainRoot.selectedType = "none";
                            mainRoot.selectedId = -1;
                            mainRoot.selectedObjectImage = "";
                        }
                        mainCanvas.requestPaint();
                    }
                }

                onPositionChanged: (mouse) => {
                    let w = screenToWorld(mouse.x, mouse.y);

                    // Смена курсора
                    if (!mainRoot.isDragging && !mainRoot.isRotating && !(mouse.buttons & Qt.RightButton)) {
                        let hit = hitTest(mouse.x, mouse.y);
                        if (!hit) cursorShape = Qt.ArrowCursor;
                        else if (hit.type === 'handle') cursorShape = Qt.CrossCursor;
                        else if (hit.type === 'node') cursorShape = Qt.PointingHandCursor;
                        else if (hit.type === 'wall') cursorShape = (hit.id === 'top' || hit.id === 'bottom') ? Qt.SplitVCursor : Qt.SplitHCursor;
                        else cursorShape = Qt.SizeAllCursor;
                    }

                    if (mainRoot.isDragging) {
                        let dx = (mouse.x - lastPos.x) / mainRoot.canvasScale;
                        let dy = (mouse.y - lastPos.y) / mainRoot.canvasScale;
                        let d = mainRoot.lastGeneratedData;

                        if (mainRoot.selectedType === 'object') {
                            d.layout[mainRoot.selectedId].CoordX += dx;
                            d.layout[mainRoot.selectedId].CoordY += dy;
                        } else if (mainRoot.selectedType === 'node') {
                            d.nodes[mainRoot.selectedId].NodeX += dx;
                            d.nodes[mainRoot.selectedId].NodeY += dy;
                        } else if (mainRoot.selectedType === 'wall') {
                            let wls = d.walls;
                            let minSpace = 2.0; // Минимальное расстояние между стенами (2 метра)

                            // ОГРАНИЧЕНИЯ ДВИЖЕНИЯ СТЕН
                            if (mainRoot.selectedId === 'top') {
                                wls.top = Math.min(w.y, wls.bottom - minSpace);
                            } else if (mainRoot.selectedId === 'bottom') {
                                wls.bottom = Math.max(w.y, wls.top + minSpace);
                            } else if (mainRoot.selectedId === 'left') {
                                wls.left = Math.min(w.x, wls.right - minSpace);
                            } else if (mainRoot.selectedId === 'right') {
                                wls.right = Math.max(w.x, wls.left + minSpace);
                            }

                            // Автообновление текстовых полей слева
                            inputWidth.text = (wls.right - wls.left).toFixed(1);
                            inputLength.text = (wls.bottom - wls.top).toFixed(1);
                        }

                        lastPos = Qt.point(mouse.x, mouse.y);
                        mainCanvas.requestPaint();

                    } else if (mainRoot.isRotating) {
                        let obj = mainRoot.lastGeneratedData.layout[mainRoot.selectedId];
                        // Вычисляем угол между центром объекта и мышкой
                        let angleRad = Math.atan2(w.y - obj.CoordY, w.x - obj.CoordX);
                        // +90 градусов, так как хэндл у нас "сверху" (по оси Y)
                        obj.AngleRotation = (angleRad * 180 / Math.PI) + 90;
                        mainCanvas.requestPaint();

                    } else if (mouse.buttons & Qt.RightButton) {
                        mainRoot.offsetX += mouse.x - lastPos.x;
                        mainRoot.offsetY += mouse.y - lastPos.y;
                        lastPos = Qt.point(mouse.x, mouse.y);
                    }
                }

                onReleased: (mouse) => {
                    if (mouse.button === Qt.LeftButton) {
                        mainRoot.isDragging = false;
                        mainRoot.isRotating = false;
                        // Force property change to register manual modifications so SA respects the new initial position
                        mainRoot.lastGeneratedData = mainRoot.lastGeneratedData;
                    }
                }

                onWheel: (wheel) => {
                    let oldScale = mainRoot.canvasScale
                    let zoom = wheel.angleDelta.y > 0 ? 1.1 : 0.9
                    let newScale = Math.min(Math.max(oldScale * zoom, 2), 200)
                    let mouseSceneX = (wheel.x - mainRoot.offsetX) / oldScale
                    let mouseSceneY = (wheel.y - mainRoot.offsetY) / oldScale
                    mainRoot.canvasScale = newScale
                    mainRoot.offsetX = wheel.x - (mouseSceneX * newScale)
                    mainRoot.offsetY = wheel.y - (mouseSceneY * newScale)
                }
            }

            Connections {
                target: mainRoot
                function onCanvasScaleChanged() { mainCanvas.requestPaint(); gridCanvas.requestPaint(); }
                function onOffsetXChanged() { mainCanvas.requestPaint(); gridCanvas.requestPaint(); }
                function onOffsetYChanged() { mainCanvas.requestPaint(); gridCanvas.requestPaint(); }
            }
            Connections {
                target: dbManager // Убедись, что имя совпадает с тем, как ты регистрировал класс в main.cpp

                function onDatabaseConnected() {
                    console.log("QML: Сигнал от БД получен!");
                    mainRoot.isDbConnected = true;
                    statusLabel.text = "✅ База данных подключена";
                }
            }
            Canvas {
                id: gridCanvas
                anchors.fill: parent; opacity: 0.15
                onPaint: {
                    var ctx = getContext("2d"); ctx.clearRect(0, 0, width, height);
                    let s = mainRoot.canvasScale; let step = s * 5;
                    ctx.save(); ctx.translate(mainRoot.offsetX, mainRoot.offsetY);
                    ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 1;
                    ctx.beginPath();
                    for (var x = -2000; x < 5000; x += step) { ctx.moveTo(x, -2000); ctx.lineTo(x, 5000); }
                    for (var y = -2000; y < 5000; y += step) { ctx.moveTo(-2000, y); ctx.lineTo(5000, y); }
                    ctx.stroke(); ctx.restore();
                }
            }

            Canvas {
                id: mainCanvas
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d"); ctx.clearRect(0, 0, width, height);
                    let s = mainRoot.canvasScale;
                    ctx.save(); ctx.translate(mainRoot.offsetX, mainRoot.offsetY);
                    let d = mainRoot.lastGeneratedData;

                    // 1. ОТРИСОВКА СТЕН
                    if (d.walls) {
                        let wls = d.walls;
                        let x = wls.left * s, y = wls.top * s;
                        let w = (wls.right - wls.left) * s, h = (wls.bottom - wls.top) * s;

                        // Заливка пола склада (полупрозрачная)
                        ctx.fillStyle = "rgba(255, 255, 255, 0.04)";
                        ctx.fillRect(x, y, w, h);

                        // Функция рисования одной стены
                        let drawWall = function(x1, y1, x2, y2, id) {
                            ctx.beginPath();
                            ctx.moveTo(x1, y1);
                            ctx.lineTo(x2, y2);
                            let isSelected = (mainRoot.selectedType === 'wall' && mainRoot.selectedId === id);
                            ctx.lineWidth = isSelected ? 4 : 2;
                            ctx.strokeStyle = isSelected ? "#3498db" : "#888";
                            ctx.stroke();
                        };

                        drawWall(x, y, x + w, y, 'top');
                        drawWall(x, y + h, x + w, y + h, 'bottom');
                        drawWall(x, y, x, y + h, 'left');
                        drawWall(x + w, y, x + w, y + h, 'right');
                    }

                    // 2. ОТРИСОВКА ПУТЕЙ (Edges)
                    d.edges.forEach(e => {
                        let n1 = d.nodes.find(n => n.NodeID === e.StartNodeID);
                        let n2 = d.nodes.find(n => n.NodeID === e.EndNodeID);
                        if (n1 && n2) {
                            let x1 = n1.NodeX * s, y1 = n1.NodeY * s;
                            let x2 = n2.NodeX * s, y2 = n2.NodeY * s;
                            ctx.beginPath(); ctx.strokeStyle = e.TwoWayTraffic ? "#472a3f" : "#3498db";
                            ctx.lineWidth = 2; ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();

                            let angle = Math.atan2(y2 - y1, x2 - x1);
                            ctx.save(); ctx.translate(x2 - (x2 - x1) * 0.1, y2 - (y2 - y1) * 0.1);
                            ctx.rotate(angle); ctx.fillStyle = ctx.strokeStyle;
                            ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(-10, -5); ctx.lineTo(-10, 5); ctx.fill();
                            ctx.restore();
                        }
                    });

                    // 3. ОТРИСОВКА ОБОРУДОВАНИЯ
                    d.layout.forEach((obj, idx) => {
                        ctx.save();
                        ctx.translate(obj.CoordX * s, obj.CoordY * s);
                        ctx.rotate(obj.AngleRotation * Math.PI / 180);
                        let realW = obj.Width * s;
                        let realL = obj.Length * s;
                        switch(obj.Type){
                            case "machine":
                                ctx.fillStyle = "#0000ff";
                                break;
                            case "rack":
                                ctx.fillStyle = "#e67e22";
                                break;
                            default:
                                ctx.fillStyle = "#8b00ff";
                        }
                        //ctx.fiilStyle = obj.Type ==="machine" ? "#3498db":"#8b00ff";
                        //ctx.fillStyle = obj.Type === "rack" ? "#e67e22" : "#8b00ff";
                        ctx.fillRect(-realW / 2, -realL / 2, realW, realL);

                        if (mainRoot.selectedType === 'object' && mainRoot.selectedId === idx) {
                            ctx.strokeStyle = "#f1c40f";
                            ctx.lineWidth = 3;
                            ctx.strokeRect(-realW / 2, -realL / 2, realW, realL);

                        // Хэндл вращения (выносим его за пределы объекта)
                            let handleOffset = (realL / 2) + 15; // 15 пикселей от края
                            ctx.beginPath();
                            ctx.lineWidth = 1;
                            ctx.moveTo(0, -realL / 2);
                            ctx.lineTo(0, -handleOffset);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.fillStyle = "#2ecc71";
                            ctx.arc(0, -handleOffset, 6, 0, Math.PI * 2);
                            ctx.fill();
                        } else {
                            ctx.strokeStyle = "white";
                            ctx.lineWidth = 1;
                            ctx.strokeRect(-realW / 2, -realL / 2, realW, realL);
                        }
                        ctx.restore();
                    });

                    // 4. ОТРИСОВКА УЗЛОВ (Точек)
                    d.nodes.forEach((n, idx) => {
                        ctx.beginPath();
                        ctx.fillStyle = n.MarkerType === "start" ? "#2ecc71" : "#f1c40f";
                        ctx.arc(n.NodeX * s, n.NodeY * s, 6, 0, Math.PI * 2);
                        ctx.fill();

                        // Если узел выделен, делаем жирную обводку
                        ctx.lineWidth = (mainRoot.selectedType === 'node' && mainRoot.selectedId === idx) ? 3 : 1;
                        ctx.strokeStyle = (mainRoot.selectedType === 'node' && mainRoot.selectedId === idx) ? "#fff" : "black";
                        ctx.stroke();
                    });

                    ctx.restore();
                }
            }

            // Main.qml — размещаем после Canvas
            Rectangle {
                id: infoPanel
                width: 280
                // Автоматическая высота по содержимому
                height: infoColumn.implicitHeight + 40

                // Позиционирование: прижимаем к правому нижнему углу холста
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20

                // Полупрозрачный фон (чистый QML)
                // 0.9 — это 90% непрозрачности. Текст будет идеально читаемым.
                color: "#2d2d2d"
                opacity: 0.95
                radius: 8

                // Вместо тени используем четкую светлую границу,
                // чтобы панель визуально отделялась от темного холста
                border.color: "#444444"
                border.width: 1

                visible: selectedType === "object" && selectedId !== -1

                // Чтобы панель не перехватывала клики, предназначенные для холста под ней
                // (если кликнули мимо кнопок на панели)
                MouseArea {
                    anchors.fill: parent
                    onClicked: (mouse) => mouse.accepted = true
                }

                ColumnLayout {
                    id: infoColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Label {
                        text: "ИНФОРМАЦИЯ"
                        font.bold: true
                        font.pixelSize: 10
                        color: "#666666" // Приглушенный заголовок
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            // Берем InstanceID из данных, которые сгенерировала нейронка
                            text: mainRoot.lastGeneratedData.layout[selectedId] ? mainRoot.lastGeneratedData.layout[selectedId].InstanceID : ""
                            color: "white"
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Label {
                            text: "Модель: " + (mainRoot.lastGeneratedData.layout[selectedId] ? mainRoot.lastGeneratedData.layout[selectedId].ModelID : "")
                            color: Material.accent
                            font.pixelSize: 13
                        }
                    }

                    // Линия-разделитель
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#3d3d3d"
                        visible: objImage.visible
                    }

                    // Картинка из MS Access
                    Image {
                        id: objImage
                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        fillMode: Image.PreserveAspectFit

                        // Если путь пустой, Image сам поймет это.
                        // Префикс file:/// важен для локальных путей из БД
                        source: selectedObjectImage !== "" ? "file:///" + selectedObjectImage : ""
                        visible: selectedObjectImage !== ""

                        // Рамка вокруг картинки, чтобы она не сливалась с фоном
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#3d3d3d"
                            z: -1
                        }
                    }
                }
            }
         }
    }



    footer: ToolBar {
        background: Rectangle { color: colorPanel; border.color: colorBorder }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 15; anchors.rightMargin: 15
            Label { id: statusLabel; text: "Система готова"; color: "#888"; font.pixelSize: 12 }
            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                Layout.maximumWidth: 300
                from: 0
                to: 100
                visible: false
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { id: busyLoading; running: false; implicitHeight: 24; implicitWidth: 24 }
        }
    }

    Dialog { id: promptDialog; title: "Настройка системного промпта"; width: 600; height: 450; modal: true; anchors.centerIn: parent; standardButtons: Dialog.Ok; ColumnLayout { anchors.fill: parent; spacing: 10; CheckBox { id: editSwitch; text: "Разрешить редактирование"; checked: false } ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; TextArea { id: systemPromptArea; text: "Ты — инженер-проектировщик. Твоя задача — рассчитать координаты оборудования склада..."; readOnly: !editSwitch.checked; wrapMode: TextArea.Wrap; font.family: "Monospace"; background: Rectangle { color: systemPromptArea.readOnly ? "#1a1a1a" : "#252525"; border.color: colorBorder } } } } }

    Dialog { id: aiSettingsDialog; title: "Параметры соединения с Yandex Cloud"; width: 500; height: Math.min(600, mainRoot.height * 0.9); modal: true; anchors.centerIn: parent; standardButtons: Dialog.Save | Dialog.Cancel; ScrollView { anchors.fill: parent; clip: true; ScrollBar.vertical.policy: ScrollBar.AsNeeded; ColumnLayout { width: parent.width - 20; spacing: 15; Label { text: "Авторизация"; font.bold: true; font.pixelSize: 16; color: Material.accent } TextField { id: apiKeyField; placeholderText: "API Key / OAuth Token"; echoMode: TextInput.Password; Layout.fillWidth: true } TextField { id: folderIdField; placeholderText: "Folder ID"; Layout.fillWidth: true } Rectangle { Layout.fillWidth: true; height: 1; color: colorBorder; Layout.topMargin: 5; Layout.bottomMargin: 5 } Label { text: "Настройки модели"; font.bold: true; font.pixelSize: 16; color: Material.accent } ComboBox { id: modelSelector; model: ["YandexGPT Pro", "YandexGPT Lite"]; Layout.fillWidth: true } ColumnLayout { Layout.fillWidth: true; spacing: 2; RowLayout { Layout.fillWidth: true; Label { text: "Плотность размещения:"; color: colorText } Item { Layout.fillWidth: true } Label { text: (densitySlider.value * 100).toFixed(0) + "%"; color: Material.accent } } Slider { id: densitySlider; from: 0.1; to: 1.0; value: 0.7; Layout.fillWidth: true } } ColumnLayout { Layout.fillWidth: true; spacing: 2; RowLayout { Layout.fillWidth: true; Label { text: "Температура (вариативность):"; color: colorText } Item { Layout.fillWidth: true } Label { text: tempSlider.value.toFixed(1); color: Material.accent } } Slider { id: tempSlider; from: 0; to: 1; value: 0.6; Layout.fillWidth: true } } Item { Layout.preferredHeight: 20 } } } }

    Platform.FileDialog { id: dbFileDialog; title: "Открыть базу данных"; onAccepted: dbManager.connectToDatabase(file) }
}