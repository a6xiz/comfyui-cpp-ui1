import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtCore

ApplicationWindow {
    id: window
    width: 400          // narrow and tall, like a phone
    height: 800
    minimumWidth: 300
    minimumHeight: 500
    visible: true
    title: qsTr("ComfyUI Client")
    color: "#141414"

    // colors for all the controls inside
    palette.windowText: "white"
    palette.text: "white"
    palette.buttonText: "white"
    palette.button: "#3a3a3a"
    palette.base: "#2b2b2b"
    palette.window: "#2b2b2b"
    palette.highlight: "#7c5cff"
    palette.highlightedText: "white"

    readonly property color accent: "#7c5cff"

    // ---------- state ----------
    property string uploadedUrl: ""      // the picked image (empty = none)
    property string resultUrl: ""        // the last generated image
    property bool busy: false
    property bool manualSize: false      // true after you touch the width/height boxes
    property int sampValue: 0            // KSampler progress
    property int sampMax: 0

    // the 5 size presets
    property var presets: [
        { name: "Square",   w: 1024, h: 1024 },
        { name: "Portrait", w: 832,  h: 1024 },
        { name: "Tall",     w: 832,  h: 1216 },
        { name: "Phone",    w: 768,  h: 1344 },
        { name: "Wide",     w: 1216, h: 832  }
    ]

    function applyPreset(p) {
        widthBox.text = String(p.w)
        heightBox.text = String(p.h)
        widthBox.focus = false
        heightBox.focus = false
        Qt.inputMethod.hide()
        manualSize = false
    }

    function num(t, fallback) {
        var v = parseInt(t)
        return isNaN(v) ? fallback : v
    }

    // remembers things between runs
    Settings {
        id: store
        category: "ComfyClient"
        property string server: "127.0.0.1:8188"
        property real blur: 0.8
        property string modelName: ""
        property int steps: 12
        property real cfg: 3.8
        property bool autoSave: true
    }

    Component.onCompleted: {
        comfy.setServer(store.server)
        comfy.setAutoSave(store.autoSave)
        comfy.refreshModels()
    }

    // ---------- reusable look for text boxes ----------
    component Field: TextField {
        Layout.fillWidth: true
        background: Rectangle {
            radius: 10
            color: "#aa2b2b2b"
            border.width: 1
            border.color: parent.activeFocus ? window.accent : "#33ffffff"
        }
    }

    // ---------- blurred background = a live "screenshot" of the whole window ----------
    Item {
        anchors.fill: parent

        // takes a picture of the whole screen (smaller, so it is fast and smooth)
        ShaderEffectSource {
            id: windowShot
            anchors.fill: parent
            sourceItem: scroll
            live: true
            hideSource: false
            visible: false
            textureSize: Qt.size(Math.max(1, width / 3), Math.max(1, height / 3))
        }

        MultiEffect {
            anchors.fill: parent
            source: windowShot
            blurEnabled: true
            blurMax: 64
            blur: store.blur
            autoPaddingEnabled: false
        }

        // darken it a bit so the controls stay readable
        Rectangle {
            anchors.fill: parent
            color: "#66000000"
        }
    }

    // ---------- file picker (works on PC and Android) ----------
    FileDialog {
        id: picker
        title: "Choose an image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            var local = comfy.importImage(selectedFile)
            if (local !== "")
                window.uploadedUrl = local
            else
                statusText.text = "Could not read that image"
        }
    }

    // ---------- the screen ----------
    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: content
            readonly property real cw: Math.min(scroll.availableWidth, 600)
            width: cw
            x: (scroll.availableWidth - cw) / 2
            spacing: 12

            // 1. Result image
            Rectangle {
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.preferredHeight: (content.cw - 24) * 1.25
                radius: 12
                color: "#66000000"
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    source: window.resultUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }

                Label {
                    anchors.centerIn: parent
                    visible: window.resultUrl === "" && !window.busy
                    text: "Result image will appear here"
                    opacity: 0.5
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: window.busy
                    visible: window.busy
                }
            }

            // 2. Prompt box + small add-image button
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 8

                TextArea {
                    id: promptBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    placeholderText: "Positive prompt..."
                    wrapMode: TextArea.Wrap
                    background: Rectangle {
                        radius: 10
                        color: "#aa2b2b2b"
                        border.width: 1
                        border.color: promptBox.activeFocus ? window.accent : "#33ffffff"
                    }
                }

                // add-image button: becomes the thumbnail once you picked a picture
                Item {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 78
                    Layout.alignment: Qt.AlignTop

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: "#aa3a3a3a"
                        border.width: 1
                        border.color: "#33ffffff"
                        clip: true

                        Label {
                            anchors.centerIn: parent
                            visible: window.uploadedUrl === ""
                            text: "+"
                            font.pixelSize: 28
                        }

                        Image {
                            anchors.fill: parent
                            visible: window.uploadedUrl !== ""
                            source: window.uploadedUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                        }

                        // tap = open the picker (add or replace)
                        MouseArea {
                            anchors.fill: parent
                            onClicked: picker.open()
                        }
                    }

                    // small x to remove the uploaded image
                    Rectangle {
                        visible: window.uploadedUrl !== ""
                        width: 24
                        height: 24
                        radius: 12
                        color: "#dd000000"
                        border.width: 1
                        border.color: "#88ffffff"
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 3

                        Label {
                            anchors.centerIn: parent
                            text: "×"
                            font.pixelSize: 18
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            onClicked: window.uploadedUrl = ""
                        }
                    }
                }
            }

            // 3. Generate button
            Button {
                id: genBtn
                text: window.busy ? "Working..." : "Generate"
                enabled: !window.busy
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.preferredHeight: 48
                font.bold: true
                background: Rectangle {
                    radius: 12
                    color: genBtn.enabled ? window.accent : "#444444"
                }
                onClicked: {
                    Qt.inputMethod.hide()
                    window.busy = true
                    window.sampValue = 0
                    window.sampMax = 0
                    statusText.text = "Sending..."
                    comfy.generate(promptBox.text, negBox.text, window.uploadedUrl,
                                   window.num(seedBox.text, -1),
                                   Math.round(stepsSlider.value),
                                   cfgSlider.value,
                                   window.num(widthBox.text, 832),
                                   window.num(heightBox.text, 1024),
                                   modelBox.currentText)
                }
            }

            // KSampler progress (live)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 4
                visible: window.busy

                Label {
                    text: window.sampMax > 0
                          ? "KSampler: step " + window.sampValue + " / " + window.sampMax
                          : "Waiting for KSampler..."
                    font.pixelSize: 12
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    radius: 4
                    color: "#44ffffff"

                    Rectangle {
                        width: window.sampMax > 0 ? parent.width * window.sampValue / window.sampMax : 0
                        height: parent.height
                        radius: 4
                        color: window.accent
                        Behavior on width { NumberAnimation { duration: 120 } }
                    }
                }
            }

            // status message
            Label {
                id: statusText
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.Wrap
                text: ""
            }

            // 4. Settings (scroll down)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 24
                spacing: 6

                // ---- model ----
                Label { text: "Model"; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ComboBox {
                        id: modelBox
                        Layout.fillWidth: true
                        model: []
                        onActivated: store.modelName = currentText
                    }
                    Button {
                        text: "Refresh"
                        onClicked: comfy.refreshModels()
                    }
                }

                // ---- size presets ----
                Label { text: "Image size"; font.bold: true; Layout.topMargin: 8 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: window.presets
                        delegate: Rectangle {
                            id: cell
                            required property var modelData
                            readonly property bool selected: !window.manualSize
                                && window.num(widthBox.text, 0) === modelData.w
                                && window.num(heightBox.text, 0) === modelData.h

                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 86
                            radius: 10
                            color: selected ? "#cc3a3560" : "#aa2b2b2b"
                            border.width: selected ? 2 : 1
                            border.color: selected ? window.accent : "#33ffffff"
                            // darkened while you type your own size, but still tappable
                            opacity: window.manualSize ? 0.45 : 1.0

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 3

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34

                                    // little rectangle with the same shape as the image
                                    Rectangle {
                                        readonly property real r: cell.modelData.w / cell.modelData.h
                                        anchors.centerIn: parent
                                        width: r >= 1 ? 32 : 32 * r
                                        height: r >= 1 ? 32 / r : 32
                                        radius: 3
                                        color: "transparent"
                                        border.width: 2
                                        border.color: cell.selected ? window.accent : "#aaaaaa"
                                    }
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: cell.modelData.name
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: cell.modelData.w + "×" + cell.modelData.h
                                    font.pixelSize: 9
                                    opacity: 0.7
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: window.applyPreset(cell.modelData)
                            }
                        }
                    }
                }

                // ---- manual size ----
                Label { text: "Custom size (px)"; Layout.topMargin: 4 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Field {
                        id: widthBox
                        text: "832"
                        placeholderText: "Width"
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 64; top: 4096 }
                        onActiveFocusChanged: if (activeFocus) window.manualSize = true
                    }
                    Label { text: "×" }
                    Field {
                        id: heightBox
                        text: "1024"
                        placeholderText: "Height"
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 64; top: 4096 }
                        onActiveFocusChanged: if (activeFocus) window.manualSize = true
                    }
                }

                Label { text: "Steps: " + Math.round(stepsSlider.value); Layout.topMargin: 8 }
                Slider {
                    id: stepsSlider
                    Layout.fillWidth: true
                    from: 1
                    to: 50
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: store.steps
                    onMoved: store.steps = Math.round(value)
                }

                Label { text: "CFG Scale: " + cfgSlider.value.toFixed(1) }
                Slider {
                    id: cfgSlider
                    Layout.fillWidth: true
                    from: 2
                    to: 7
                    stepSize: 0.1
                    snapMode: Slider.SnapAlways
                    value: store.cfg
                    onMoved: store.cfg = Math.round(value * 10) / 10
                }

                Label { text: "Seed (-1 for random)" }
                Field { id: seedBox; text: "-1" }

                Label { text: "Negative prompt" }
                Field { id: negBox }

                // ---- Android: save images to the gallery ----
                Switch {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    visible: Qt.platform.os === "android"
                    text: "Save images to Pictures/ComfyClient"
                    checked: store.autoSave
                    onToggled: {
                        store.autoSave = checked
                        comfy.setAutoSave(checked)
                    }
                }

                // ---- connection ----
                Label { text: "ComfyUI address (on a phone: your PC's IP, like 192.168.1.10:8188)"
                        wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.topMargin: 8 }
                Field {
                    id: serverBox
                    text: store.server
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                    onEditingFinished: {
                        store.server = text
                        comfy.setServer(text)
                        comfy.refreshModels()
                    }
                }

                // ---- blur: the very last setting ----
                Label { text: "Background blur"; Layout.topMargin: 8 }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: store.blur
                    onMoved: store.blur = value
                }
            }
        }
    }

    // ---------- messages coming back from the C++ code ----------
    Connections {
        target: comfy

        function onModelsReady(models, current) {
            modelBox.model = models
            var want = store.modelName !== "" ? store.modelName : current
            var i = models.indexOf(want)
            modelBox.currentIndex = i >= 0 ? i : 0
        }
        function onModelsFailed(msg) {
            statusText.text = "Models: " + msg
        }
        function onProgress(text) {
            statusText.text = text
        }
        function onImageReady(url) {
            window.resultUrl = url
            window.busy = false
            statusText.text = "Done"
        }
        function onFailed(msg) {
            window.busy = false
            statusText.text = "Error: " + msg
        }
        function onSamplerProgress(value, max) {
            window.sampValue = value
            window.sampMax = max
        }
        function onSavedToGallery(ok, where) {
            statusText.text = ok ? "Done - saved to " + where
                                 : "Done (could not save to the gallery)"
        }
    }
}
