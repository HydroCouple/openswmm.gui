// spike_qsgnode.cpp — Phase B.RHI.1 of docs/RENDERING_5M_PLAN.md.
//
// Minimal QQuickWindow hosting a custom QQuickItem whose
// updatePaintNode() builds one QSGGeometryNode rendering a green
// triangle on a dark-blue background. Verifies that Qt's scene graph
// + QRhi backend (Metal on macOS) renders correctly in this Qt build
// before we commit to swapping the SWMM layer renderer over.
//
// QQuickWindow is used here for spike simplicity — the production
// integration will host the same QQuickItem inside a QQuickWidget
// embedded in MapCanvas. The QSG rendering path is identical in
// both, so this verifies the GPU pipeline either way.
//
// Pass criteria:
//   - A green triangle on dark-blue background renders in the window
//   - The log reports the scene-graph backend (Metal on macOS)
//   - No GL / Metal / Vulkan warnings
//
// Run: ./spike_qsgnode  (interactive — close the window to exit)

#include <QApplication>
#include <QDebug>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGRendererInterface>

namespace {

const char *graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Metal:        return "Metal";
    case QSGRendererInterface::Vulkan:       return "Vulkan";
    case QSGRendererInterface::Direct3D11:   return "Direct3D 11";
    case QSGRendererInterface::Direct3D12:   return "Direct3D 12";
    case QSGRendererInterface::OpenGL:       return "OpenGL";
    case QSGRendererInterface::Software:     return "Software";
    case QSGRendererInterface::Null:         return "Null";
    default:                                 return "Unknown";
    }
}

class TriangleItem : public QQuickItem
{
    Q_OBJECT

public:
    explicit TriangleItem(QQuickItem *parent = nullptr)
        : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);
    }

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *) override
    {
        ++m_paintCount;

        auto *node = static_cast<QSGGeometryNode *>(oldNode);
        if (!node) {
            node = new QSGGeometryNode();

            auto *geo = new QSGGeometry(
                QSGGeometry::defaultAttributes_Point2D(), 3);
            geo->setDrawingMode(QSGGeometry::DrawTriangles);
            node->setGeometry(geo);
            node->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial();
            mat->setColor(QColor(60, 220, 90));
            node->setMaterial(mat);
            node->setFlag(QSGNode::OwnsMaterial);
        }

        auto *geo = node->geometry();
        auto *v   = geo->vertexDataAsPoint2D();
        v[0].set(float(width()) * 0.5f, 10.0f);
        v[1].set(20.0f,                 float(height()) - 20.0f);
        v[2].set(float(width()) - 20.0f, float(height()) - 20.0f);
        node->markDirty(QSGNode::DirtyGeometry);

        if (m_paintCount == 1)
            qInfo() << "[spike] first updatePaintNode() OK — QSGGeometryNode draw works";

        return node;
    }

private:
    int m_paintCount = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QQuickWindow window;
    window.setColor(QColor("#1a2e4a"));
    window.resize(800, 600);
    window.setTitle("Phase B.RHI.1 spike — QSGGeometryNode");

    auto *triangle = new TriangleItem(window.contentItem());
    triangle->setSize(window.size());
    QObject::connect(&window, &QQuickWindow::widthChanged, triangle,
                     [triangle, &window]() {
                         triangle->setSize(window.size());
                     });
    QObject::connect(&window, &QQuickWindow::heightChanged, triangle,
                     [triangle, &window]() {
                         triangle->setSize(window.size());
                     });

    QObject::connect(&window, &QQuickWindow::sceneGraphInitialized,
                     &window, [&window]() {
                         auto *iface = window.rendererInterface();
                         qInfo() << "[spike] scene graph backend:"
                                 << graphicsApiName(iface->graphicsApi());
                     });

    window.show();
    qInfo() << "[spike] window shown — close it to exit";
    return app.exec();
}

#include "spike_qsgnode.moc"
