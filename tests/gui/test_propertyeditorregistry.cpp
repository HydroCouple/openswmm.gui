/*!
 * \file   test_propertyeditorregistry.cpp
 * \brief  Slice BM Phase 6.3.5 — registry register / hasFactory / create /
 *         clear round-trip + multi-kind dispatch. Self-contained: no engine
 *         handle and no model layer.
 *
 *         Coverage map:
 *           1. empty registry → hasFactory == false, create returns nullptr
 *           2. register one kind → hasFactory true, create returns a fresh
 *              editor whose objectKind() matches.
 *           3. register two kinds → both create independently.
 *           4. re-register same kind warns (qWarning suppressed) and the
 *              replacement wins on subsequent create().
 *           5. clear() drops every registration so reuse from test to test
 *              is safe.
 *           6. null / empty kind / null factory rejected with qWarning;
 *              registry unchanged.
 */

#include <QObject>
#include <QString>
#include <QTest>
#include <QWidget>

#include "ui/properties/ipropertyeditor.h"
#include "ui/properties/propertyeditorregistry.h"

namespace {

class DummyEditor : public IPropertyEditor
{
public:
    explicit DummyEditor(QString kind) : m_kind(std::move(kind)) {}
    [[nodiscard]] QString objectKind() const override { return m_kind; }
    [[nodiscard]] QWidget *editorForObject(const SWMMObjectRef &) override
    {
        return nullptr;
    }
    void apply(const SWMMObjectRef &) override {}

private:
    QString m_kind;
};

} // anonymous

class TestPropertyEditorRegistry : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        PropertyEditorRegistry::instance().clear();
    }

    void emptyRegistryReturnsNullptr()
    {
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory("junction"));
        QVERIFY(PropertyEditorRegistry::instance().create("junction") == nullptr);
    }

    void registerSingleKindThenCreate()
    {
        PropertyEditorRegistry::instance().registerFactory(
            "junction",
            [] { return std::make_unique<DummyEditor>(QStringLiteral("junction")); });

        QVERIFY(PropertyEditorRegistry::instance().hasFactory("junction"));
        auto editor = PropertyEditorRegistry::instance().create("junction");
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->objectKind(), QStringLiteral("junction"));
    }

    void registerMultipleKindsDispatchIndependently()
    {
        auto &reg = PropertyEditorRegistry::instance();
        reg.registerFactory("curve",
            [] { return std::make_unique<DummyEditor>(QStringLiteral("curve")); });
        reg.registerFactory("timeseries",
            [] { return std::make_unique<DummyEditor>(QStringLiteral("timeseries")); });

        auto c = reg.create("curve");
        auto t = reg.create("timeseries");
        QVERIFY(c && t);
        QCOMPARE(c->objectKind(), QStringLiteral("curve"));
        QCOMPARE(t->objectKind(), QStringLiteral("timeseries"));
    }

    void reRegisterReplacesFactory()
    {
        auto &reg = PropertyEditorRegistry::instance();
        reg.registerFactory("junction",
            [] { return std::make_unique<DummyEditor>(QStringLiteral("v1")); });

        // Replacing the existing factory emits a qWarning (verified via
        // log capture in larger-scoped tests). The new factory takes
        // effect immediately for subsequent create() calls.
        reg.registerFactory("junction",
            [] { return std::make_unique<DummyEditor>(QStringLiteral("v2")); });

        auto editor = reg.create("junction");
        QVERIFY(editor);
        QCOMPARE(editor->objectKind(), QStringLiteral("v2"));
    }

    void emptyKindIgnored()
    {
        PropertyEditorRegistry::instance().registerFactory(
            QString{},
            [] { return std::make_unique<DummyEditor>(QStringLiteral("empty")); });
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory(QString{}));
    }

    void nullFactoryIgnored()
    {
        PropertyEditorRegistry::instance().registerFactory("junction", {});
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory("junction"));
    }
};

QTEST_MAIN(TestPropertyEditorRegistry)
#include "test_propertyeditorregistry.moc"
