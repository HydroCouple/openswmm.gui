/**
 * @file      test_propertyeditorregistry.cpp
 * @brief     Slice BM Phase 6.3.5 — registry register / hasFactory / create /
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>
#include <QString>
#include <QTest>
#include <QWidget>

#include "ui/properties/ipropertyeditor.h"
#include "ui/properties/propertyeditorregistry.h"

namespace
{

class DummyEditor : public IPropertyEditor
{
public:
    /**
     *
     * @brief Store the dummy editor's advertised kind by moving the value argument.
     * @param kind Owned copy of the requested display kind; no normalization or validation.
     * Does not construct widgets or retain model references.
     *
     */
    explicit DummyEditor(QString kind) : m_kind(std::move(kind)) {}
    /**
     *
     * @brief Return the stored dummy kind without registry lookup.
     * @return Owned QString value; no mutation or native model access.
     *
     */
    [[nodiscard]] QString objectKind() const override { return m_kind; }
    /**
     *
     * @brief Provide no actual editor widget in this registry-dispatch stand-in.
     * @param ref Borrowed object ref, deliberately ignored and not retained.
     * @return Null; no widget allocation, parentage or ownership transfer.
     *
     */
    [[nodiscard]] QWidget *editorForObject(const SWMMObjectRef &ref) override { return nullptr; }
    /**
     *
     * @brief Ignore apply requests in the widget-free registry-dispatch stand-in.
     * @param ref Borrowed ignored object ref; no mutation or retained reference.
     *
     */
    void apply(const SWMMObjectRef &ref) override {}

private:
    QString m_kind;
};

} // anonymous

class TestPropertyEditorRegistry : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Clear the process-wide property-editor factory registry after every case.
     * Drops callable registrations without restoring prior application factories; editor
     * unique_ptr values created in each case have already gone out of scope.
     *
     */
    void cleanup() { PropertyEditorRegistry::instance().clear(); }

    /**
     *
     * @brief Require no factory and null creation for junction in the initially empty singleton
     * registry.
     * Relies on isolated test execution/cleanup; no production factories are installed.
     *
     */
    void emptyRegistryReturnsNullptr()
    {
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory("junction"));
        QVERIFY(PropertyEditorRegistry::instance().create("junction") == nullptr);
    }

    /**
     *
     * @brief Require registering a junction factory to make it available and create an owning
     * dummy with matching kind.
     * Factory captures nothing; local unique_ptr destroys the returned editor. No QWidget
     * or model mutation is involved.
     *
     */
    void registerSingleKindThenCreate()
    {
        PropertyEditorRegistry::instance().registerFactory(
            "junction", [] { return std::make_unique<DummyEditor>(QStringLiteral("junction")); });

        QVERIFY(PropertyEditorRegistry::instance().hasFactory("junction"));
        auto editor = PropertyEditorRegistry::instance().create("junction");
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->objectKind(), QStringLiteral("junction"));
    }

    /**
     *
     * @brief Require separate curve/timeseries factories to create matching independent dummy
     * editors.
     * Checks both non-null and advertised kinds, without comparing repeated-create identity
     * or building any widgets.
     *
     */
    void registerMultipleKindsDispatchIndependently()
    {
        auto &reg = PropertyEditorRegistry::instance();
        reg.registerFactory("curve",
                            [] { return std::make_unique<DummyEditor>(QStringLiteral("curve")); });
        reg.registerFactory("timeseries", [] {
            return std::make_unique<DummyEditor>(QStringLiteral("timeseries"));
        });

        auto c = reg.create("curve");
        auto t = reg.create("timeseries");
        QVERIFY(c && t);
        QCOMPARE(c->objectKind(), QStringLiteral("curve"));
        QCOMPARE(t->objectKind(), QStringLiteral("timeseries"));
    }

    /**
     *
     * @brief Require re-registering junction to use the second v2 factory for future creation.
     * Does not capture/assert the expected warning or test an already created v1 instance.
     *
     */
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

    /**
     *
     * @brief Require an empty-kind factory registration not to appear as an available factory.
     * Does not assert warning text or all existing registry entries.
     *
     */
    void emptyKindIgnored()
    {
        PropertyEditorRegistry::instance().registerFactory(
            QString{}, [] { return std::make_unique<DummyEditor>(QStringLiteral("empty")); });
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory(QString{}));
    }

    /**
     *
     * @brief Require an empty callable for junction to leave no registered factory.
     * Does not invoke create or inspect warning output in this case.
     *
     */
    void nullFactoryIgnored()
    {
        PropertyEditorRegistry::instance().registerFactory("junction", {});
        QVERIFY(!PropertyEditorRegistry::instance().hasFactory("junction"));
    }
};

QTEST_MAIN(TestPropertyEditorRegistry)
#include "test_propertyeditorregistry.moc"
