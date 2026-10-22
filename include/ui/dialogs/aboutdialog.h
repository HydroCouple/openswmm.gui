/**
 * @file      aboutdialog.h
 * @brief     About dialog with license browser.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice K — About dialog with license browser. Driven by a JSON manifest
 * (resources/about/components.json) and per-component license text under
 * resources/licenses/NAME.txt so adding/updating a dependency is a data
 * change, not a code change.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
        QString category;    ///< e.g. "Geospatial"
        QString name;        ///< e.g. "GDAL"
        QString version;     ///< e.g. "3.9.2" (may be empty)
        QString role;        ///< what it's used for
        QString homepage;    ///< URL (may be empty)
        QString sourceUrl;   ///< URL to source download (may be empty)
        QString spdx;        ///< SPDX license identifier (e.g. "MIT")
        QString provenance;  ///< "vcpkg", "submodule", "in-tree copy", …
        QString licenseFile; ///< Qt-resource path to verbatim license text
    };

    /**
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build the GUI, read embedded manifest, then select the first available component and
     *   show its license.
     * @note Missing/invalid manifest leaves application overview with no components. Named
     *   dialog/splitter permit host persistence; construction itself installs no watcher and
     *   performs no network requests.
     */
    explicit AboutDialog(QWidget *parent = nullptr);
    /**
     * @brief Release owned widgets and manifest values; no settings write or external-resource
     *   ownership.
     */
    ~AboutDialog() override;

    /*!
     * \brief Parses a manifest JSON document into a vector of Component rows.
     * \param json    UTF-8 JSON bytes (the schema is documented in the spec).
     * \param errorOut  optional human-readable parse error.
     * @return Owned rows in array order; nonobject entries and objects with empty name are skipped.
     *   All fields use JSON string conversion, so absent/wrong-type fields become empty; no SPDX,
     *   path, URL or required-field validation beyond name.
     * @note Nonarray or invalid JSON returns empty and writes optional errorOut. Successful
     *   parsing, including empty result, leaves previous errorOut text unchanged. Reads bytes only,
     *   does not load license files or open links.
     */
    [[nodiscard]] static QVector<Component> parseManifest(const QByteArray &json,
                                                          QString *errorOut = nullptr);

    /*!
     * \brief Returns the list of components currently loaded into the dialog.
     * @return Borrowed immutable vector owned by this dialog; valid until destruction or internal
     *   manifest replacement. No copy, reload or ownership transfer; each row may contain
     *   empty/unvalidated metadata.
     */
    [[nodiscard]] const QVector<Component> &components() const { return m_components; }

private slots:
    /**
     * @brief Map current proxy index to a component and display its details.
     * @note Invalid/group/out-of-range current index leaves existing detail and current component
     *   unchanged, so filtering away a selection does not necessarily clear the pane. No
     *   model/project edits.
     */
    void onSelectionChanged();
    /**
     * @param[in] text Literal untrimmed filter string; empty clears filtering.
     * @brief Apply case-insensitive fixed-string recursive filtering to displayed
     *   category/component labels and expand all visible groups.
     * @note Does not search license text or metadata fields, reload the manifest or explicitly
     *   clear current details.
     */
    void onSearchTextChanged(const QString &text);
    /**
     * @brief Replace application clipboard text with the currently displayed license pane,
     *   including missing-file explanatory text.
     * @note No selection or emptiness check inside slot; GUI/clipboard access, no file save.
     */
    void onCopyLicense();
    /**
     * @brief Ask desktop services to open the current component homepage URL.
     * @note No-op when current component is negative or URL empty; current nonnegative index must
     *   still refer to m_components. Raw URL is handed to QUrl without scheme validation; external
     *   handler can open a browser/application. Return/failure is ignored, no fetch inside this
     *   dialog.
     */
    void onOpenHomepage();
    /**
     * @brief Ask desktop services to open the current component source URL.
     * @note No-op when current component is negative or URL empty; current nonnegative index must
     *   still refer to m_components. Raw URL is handed to QUrl without scheme validation; external
     *   handler can open a browser/application. Return/failure is ignored, no fetch inside this
     *   dialog.
     */
    void onOpenSource();

protected:
    /**
     * @param[in] event Required borrowed Qt show event, forwarded to base and not retained.
     * @brief After base handling, fit the hidden tree column to contents, make that width its
     *   minimum and stretch remaining width.
     * @note Runs on every show, overriding header sizing restored by host layout watcher; does not
     *   reload metadata or change current selection.
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * @brief Create owned searchable component tree, detail/license pane and action buttons on the
     *   GUI thread.
     * @note Proxy filters case-insensitively with recursive category visibility. Environment copy
     *   captures a fresh summary at click time; metadata hyperlinks and open buttons can invoke
     *   external handlers. Close accepts dialog. Does not populate manifest tree or connect
     *   current-index handling until populateTree.
     */
    void buildUi();
    /**
     * @brief Replace component values from the embedded :/about/components.json resource.
     * @note Missing file or parse error clears vector silently; no network fallback or UI refresh.
     *   Parse error starts empty for this call. Existing tree/current index must be rebuilt by
     *   caller after replacement.
     */
    void loadManifest();
    /**
     * @brief Replace items grouped by category in first-occurrence order, preserving manifest order
     *   within groups; empty category becomes Other.
     * @note Leaves store vector indices; groups are nonselectable. Expand all, add a currentChanged
     *   connection and select the first component if present, which can replace overview with its
     *   license. Repeated calls add connections; does not reload manifest or explicitly reset
     *   current details when empty.
     */
    void populateTree();
    /**
     * @param[in] c Borrowed metadata read only during call; strings copied into labels, license
     *   path read synchronously.
     * @brief Display escaped metadata and UTF-8 license file content, or a readable
     *   missing/undeclared-license message.
     * @note Enables copy for any nonempty displayed text, and link buttons for nonempty URLs
     *   regardless of validity. Does not set m_currentComponent itself, so caller must maintain
     *   index for button targets. No network request or resource ownership retained.
     */
    void showComponent(const Component &c);
    /**
     * @brief Reset current component to -1, display application version/help and clear license pane
     *   while disabling its three action buttons.
     * @note Does not clear tree selection, manifest or search filter, and does not read license
     *   resources.
     */
    void showApplicationOverview();

    QVector<Component> m_components;

    // Header strip
    QLabel *m_headerLabel = nullptr;
    QPushButton *m_copyEnvButton = nullptr;

    // Master list
    QLineEdit *m_searchEdit = nullptr;
    QStandardItemModel *m_treeModel = nullptr;
    QSortFilterProxyModel *m_treeProxy = nullptr;
    QTreeView *m_tree = nullptr;

    // Detail pane
    QLabel *m_nameLabel = nullptr;
    QLabel *m_metaLabel = nullptr;
    QPlainTextEdit *m_licenseText = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_homepageButton = nullptr;
    QPushButton *m_sourceButton = nullptr;

    int m_currentComponent = -1; // index into m_components
};

#endif // ABOUTDIALOG_H
