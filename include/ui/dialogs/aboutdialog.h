/*!
 * \file   aboutdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice K — About dialog with license browser. Driven by a JSON manifest
 * (resources/about/components.json) and per-component license text under
 * resources/licenses/<name>.txt so adding/updating a dependency is a data
 * change, not a code change.
 */
#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTreeView;

/*!
 * \class AboutDialog
 * \brief Two-pane modal: master list of every shipped component (left) +
 *        per-component license + metadata detail (right).
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:

    /*!
     * \brief One row in the manifest. Public so tests can construct one.
     */
    struct Component {
        QString category;     ///< e.g. "Geospatial"
        QString name;         ///< e.g. "GDAL"
        QString version;      ///< e.g. "3.9.2" (may be empty)
        QString role;         ///< what it's used for
        QString homepage;     ///< URL (may be empty)
        QString sourceUrl;    ///< URL to source download (may be empty)
        QString spdx;         ///< SPDX license identifier (e.g. "MIT")
        QString provenance;   ///< "vcpkg", "submodule", "in-tree copy", …
        QString licenseFile;  ///< Qt-resource path to verbatim license text
    };

    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override;

    /*!
     * \brief Parses a manifest JSON document into a vector of Component rows.
     * \param json    UTF-8 JSON bytes (the schema is documented in the spec).
     * \param errorOut  optional human-readable parse error.
     */
    [[nodiscard]] static QVector<Component>
    parseManifest(const QByteArray &json, QString *errorOut = nullptr);

    /*!
     * \brief Returns the list of components currently loaded into the dialog.
     */
    [[nodiscard]] const QVector<Component> &components() const { return m_components; }

private slots:
    void onSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onCopyLicense();
    void onOpenHomepage();
    void onOpenSource();

private:
    void buildUi();
    void loadManifest();
    void populateTree();
    void showComponent(const Component &c);
    void showApplicationOverview();

    QVector<Component>       m_components;

    // Header strip
    QLabel                  *m_headerLabel    = nullptr;
    QPushButton             *m_copyEnvButton  = nullptr;

    // Master list
    QLineEdit               *m_searchEdit     = nullptr;
    QStandardItemModel      *m_treeModel      = nullptr;
    QSortFilterProxyModel   *m_treeProxy      = nullptr;
    QTreeView               *m_tree           = nullptr;

    // Detail pane
    QLabel                  *m_nameLabel      = nullptr;
    QLabel                  *m_metaLabel      = nullptr;
    QPlainTextEdit          *m_licenseText    = nullptr;
    QPushButton             *m_copyButton     = nullptr;
    QPushButton             *m_homepageButton = nullptr;
    QPushButton             *m_sourceButton   = nullptr;

    int                      m_currentComponent = -1;   // index into m_components
};

#endif // ABOUTDIALOG_H
