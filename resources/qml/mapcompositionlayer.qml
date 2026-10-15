import QtQuick

Item {
    id: wrapper
    property alias contentItem: content
    property alias staticItem: staticContent
    property bool cacheStaticContent: false
    property Item maskItem
    property bool maskEnabled: false

    Item {
        id: content
        anchors.fill: parent
        Item {
            id: staticContent
            anchors.fill: parent
            layer.enabled: wrapper.cacheStaticContent
            layer.smooth: true
            layer.samples: 4
        }
        layer.enabled: wrapper.maskEnabled
        layer.smooth: true
        layer.effect: ShaderEffect {
            property variant maskTexture: maskSource
            fragmentShader: "qrc:/resources/shaders/mapmask.frag.qsb"
        }
    }
    ShaderEffectSource {
        id: maskSource
        sourceItem: wrapper.maskItem
        hideSource: true
        live: true
        visible: false
    }
}
