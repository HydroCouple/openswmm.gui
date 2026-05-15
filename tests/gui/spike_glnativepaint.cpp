// spike_glnativepaint.cpp — Phase B.1 of docs/RENDERING_5M_PLAN.md
//
// Confirms `painter->beginNativePainting()` integrates cleanly on
// macOS Qt 6.10 + Apple Silicon before we commit to swapping the
// MapCanvas viewport. Opens a QGraphicsView with a QOpenGLWidget
// viewport, plus one custom item whose paint() drops into raw GL via
// beginNativePainting and draws a triangle directly through
// `glDrawArrays(GL_TRIANGLES, …)` over a small VBO. The triangle is
// what the production SWMM layer item will be (only with N_links
// segments and `GL_LINES`).
//
// Pass criteria: a green-on-blue triangle renders without crashing or
// printing GL errors. Fail = blank/black widget, or qWarning output.
//
// Run:  ./spike_glnativepaint   (interactive — close the window)

#include <QApplication>
#include <QDebug>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTimer>

namespace {

// Vertex shader: pass-through 2D position, no MVP needed (we draw in
// clip space directly so the triangle survives whatever transform
// QGraphicsView applied before beginNativePainting).
constexpr const char *kVertSrc = R"(
#version 120
attribute vec2 position;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char *kFragSrc = R"(
#version 120
void main() {
    gl_FragColor = vec4(0.2, 0.85, 0.3, 1.0);
}
)";

class GLTriangleItem : public QGraphicsItem
{
public:
    explicit GLTriangleItem() {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    }

    QRectF boundingRect() const override {
        // Whole scene — we paint in clip space, so positioning doesn't
        // matter; we just want our paint() to be invoked every frame.
        return QRectF(-1e6, -1e6, 2e6, 2e6);
    }

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        ++m_paintCount;

        // Drop into raw GL. After endNativePainting, QPainter resumes.
        painter->beginNativePainting();

        auto *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
            qWarning() << "[spike] no current GL context inside beginNativePainting";
            painter->endNativePainting();
            return;
        }
        if (m_paintCount == 1) {
            qInfo() << "[spike] GL vendor :"
                    << reinterpret_cast<const char *>(
                           ctx->functions()->glGetString(GL_VENDOR));
            qInfo() << "[spike] GL renderer:"
                    << reinterpret_cast<const char *>(
                           ctx->functions()->glGetString(GL_RENDERER));
            qInfo() << "[spike] GL version :"
                    << reinterpret_cast<const char *>(
                           ctx->functions()->glGetString(GL_VERSION));
        }

        QOpenGLExtraFunctions *gl = ctx->extraFunctions();

        if (!m_program) {
            m_program = std::make_unique<QOpenGLShaderProgram>();
            if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc)
             || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc)
             || !m_program->link()) {
                qWarning() << "[spike] shader link failed:" << m_program->log();
                m_program.reset();
                painter->endNativePainting();
                return;
            }

            m_vbo.create();
            m_vbo.bind();
            const float verts[6] = {
                 0.0f,  0.5f,
                -0.5f, -0.5f,
                 0.5f, -0.5f,
            };
            m_vbo.allocate(verts, sizeof(verts));
            m_vbo.release();
            qInfo() << "[spike] shader + VBO created";
        }

        // Clear behind the triangle so we can tell paint actually ran.
        gl->glClearColor(0.10f, 0.18f, 0.30f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        m_program->bind();
        m_vbo.bind();
        const int loc = m_program->attributeLocation("position");
        m_program->enableAttributeArray(loc);
        m_program->setAttributeBuffer(loc, GL_FLOAT, 0, 2);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        m_program->disableAttributeArray(loc);
        m_vbo.release();
        m_program->release();

        const GLenum err = gl->glGetError();
        if (err != GL_NO_ERROR)
            qWarning() << "[spike] glGetError after draw:" << QString::number(err, 16);

        painter->endNativePainting();

        if (m_paintCount == 1)
            qInfo() << "[spike] first paint OK — beginNativePainting + drawArrays works";
    }

private:
    int m_paintCount = 0;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QGraphicsScene scene;
    scene.addItem(new GLTriangleItem());

    QGraphicsView view(&scene);
    view.setViewport(new QOpenGLWidget());
    view.setRenderHint(QPainter::Antialiasing);
    view.setSceneRect(-100, -100, 200, 200);
    view.resize(640, 480);
    view.setWindowTitle("Phase B.1 spike — beginNativePainting");
    view.show();

    qInfo() << "[spike] window shown — close it to exit";
    return app.exec();
}
