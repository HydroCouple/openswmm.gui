/*!
 * \file reactionsystemeditordialog.cpp
 * \brief Implementation of the reaction-system editor (G-B2 + G-C1 + G-D1).
 * \see include/ui/dialogs/reactionsystemeditordialog.h
 */

#include "ui/dialogs/reactionsystemeditordialog.h"

#include "ui/widgets/reactionexpressionedit.h"

#include <openswmm/engine/openswmm_process_components.h>
#include <openswmm/engine/openswmm_reactions.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>

namespace OpenSWMMVis
{

namespace {

constexpr const char *kReactionsId = "org.hydrocouple.openswmm.reactions";

QString engineText(SWMM_Engine e)
{
    int need = 0;
    if (swmm_reactions_serialize(e, nullptr, 0, &need) != SWMM_OK || need <= 1)
        return {};
    std::vector<char> buf(static_cast<std::size_t>(need));
    if (swmm_reactions_serialize(e, buf.data(), need, &need) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf.data());
}

/*! Whole-file .rxn highlighter (G-C1): comments, section headers, row
 *  keywords, numbers; palette-role themed like the expression stack. */
class RxnFileHighlighter : public QSyntaxHighlighter
{
public:
    explicit RxnFileHighlighter(QTextDocument *doc, const QPalette &pal)
        : QSyntaxHighlighter(doc)
    {
        const QColor link  = pal.color(QPalette::Active, QPalette::Link);
        const QColor lv    = pal.color(QPalette::Active, QPalette::LinkVisited);
        const QColor ph    = pal.color(QPalette::Active, QPalette::PlaceholderText);
        const QColor text  = pal.color(QPalette::Active, QPalette::Text);
        m_fmtSection.setForeground(lv.isValid() ? lv : QColor("#6A1B9A"));
        m_fmtSection.setFontWeight(QFont::Bold);
        m_fmtKeyword.setForeground(link.isValid() ? link : QColor("#00695C"));
        m_fmtKeyword.setFontWeight(QFont::Bold);
        m_fmtComment.setForeground(ph.isValid() ? ph : QColor("#9E9E9E"));
        m_fmtComment.setFontItalic(true);
        m_fmtNumber.setForeground(text.isValid() ? text : QColor("#37474F"));
    }

protected:
    void highlightBlock(const QString &line) override
    {
        // Comment from ';' to end of line.
        const int semi = static_cast<int>(line.indexOf(QLatin1Char(';')));
        const int end = (semi >= 0) ? semi : static_cast<int>(line.size());
        if (semi >= 0)
            setFormat(semi, static_cast<int>(line.size()) - semi,
                      m_fmtComment);

        // Section header.
        static const QRegularExpression kSection(
            QStringLiteral("^\\s*\\[[A-Za-z0-9_]+\\]"));
        const auto sm = kSection.match(line.left(end));
        if (sm.hasMatch()) {
            setFormat(static_cast<int>(sm.capturedStart(0)),
                      static_cast<int>(sm.capturedLength(0)), m_fmtSection);
            return;
        }

        // Row keywords + numbers.
        static const QSet<QString> kKeywords = {
            QStringLiteral("GLOBAL"), QStringLiteral("NODE"),
            QStringLiteral("LINK"), QStringLiteral("BULK"),
            QStringLiteral("WALL"), QStringLiteral("RATE"),
            QStringLiteral("EQUIL"), QStringLiteral("FORMULA"),
            QStringLiteral("PARAMETER"), QStringLiteral("CONSTANT"),
            QStringLiteral("SOLVER"), QStringLiteral("COUPLING"),
            QStringLiteral("RATE_UNITS"), QStringLiteral("AREA_UNITS"),
            QStringLiteral("TIMESTEP"), QStringLiteral("ATOL"),
            QStringLiteral("RTOL"),
        };
        static const QRegularExpression kWord(
            QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
        auto it = kWord.globalMatch(line.left(end));
        while (it.hasNext()) {
            const auto m = it.next();
            if (kKeywords.contains(m.captured(0).toUpper()))
                setFormat(static_cast<int>(m.capturedStart(0)),
                          static_cast<int>(m.capturedLength(0)),
                          m_fmtKeyword);
        }
        static const QRegularExpression kNum(QStringLiteral(
            "\\b[0-9]+(?:\\.[0-9]*)?(?:[eE][+-]?[0-9]+)?\\b"));
        auto nt = kNum.globalMatch(line.left(end));
        while (nt.hasNext()) {
            const auto m = nt.next();
            setFormat(static_cast<int>(m.capturedStart(0)),
                      static_cast<int>(m.capturedLength(0)), m_fmtNumber);
        }
    }

private:
    QTextCharFormat m_fmtSection, m_fmtKeyword, m_fmtComment, m_fmtNumber;
};

QDoubleSpinBox *makeValueSpin(QWidget *parent, double lo = 0.0,
                              double hi = 1.0e9, int decimals = 6)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    return s;
}

} // namespace

ReactionSystemEditorDialog::ReactionSystemEditorDialog(SWMM_Engine engine,
                                                       QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Reaction System"));
    setObjectName(QStringLiteral("reactionSystemEditorDialog"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(760, 560);
    buildUi();
    reloadAll();
    refreshBinding();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

void ReactionSystemEditorDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("rx_tabs"));
    m_tabs->addTab(buildOptionsTab(),        tr("Options"));
    m_tabs->addTab(buildSpeciesTab(),        tr("Species"));
    m_tabs->addTab(buildCoefficientsTab(),   tr("Coefficients"));
    m_tabs->addTab(buildTermsTab(),          tr("Terms"));
    m_tabs->addTab(buildExpressionsTab(),    tr("Expressions"));
    m_tabs->addTab(buildInitialQualityTab(), tr("Initial Quality"));
    m_fileTabIndex = m_tabs->addTab(buildFileTab(), tr("File"));
    const int srcIdx =
        m_tabs->addTab(buildSourcesPlaceholderTab(), tr("Sources"));
    m_tabs->setTabEnabled(srcIdx, false);
    m_tabs->setTabToolTip(srcIdx,
                          tr("Requires engine phase R-sources — the engine "
                             "rejects [REACTION_SOURCES] today."));
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &ReactionSystemEditorDialog::onTabChanged);
    vlay->addWidget(m_tabs, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("rx_status"));
    m_statusLabel->setWordWrap(true);
    vlay->addWidget(m_statusLabel);

    auto *btnRow = new QHBoxLayout;
    m_saveBtn = new QPushButton(tr("&Save to File"), this);
    m_saveBtn->setObjectName(QStringLiteral("rx_saveBtn"));
    connect(m_saveBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onSave);
    btnRow->addWidget(m_saveBtn);
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(tr("&Close"), this);
    closeBtn->setObjectName(QStringLiteral("rx_closeBtn"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_tabs->currentIndex() == m_fileTabIndex && !applyFileTab())
            return;                     // bad text gates the close
        accept();
    });
    btnRow->addWidget(closeBtn);
    vlay->addLayout(btnRow);
}

QWidget *ReactionSystemEditorDialog::buildOptionsTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    auto addCombo = [&](const char *objName, const QString &label,
                        const QStringList &items, const char *key)
        -> QComboBox * {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, w));
        auto *c = new QComboBox(w);
        c->setObjectName(QLatin1String(objName));
        c->addItems(items);
        row->addWidget(c, 1);
        lay->addLayout(row);
        connect(c, &QComboBox::currentTextChanged, this,
                [this, key](const QString &v) {
                    if (m_loading || !m_engine) return;
                    if (swmm_reaction_option_set(
                            m_engine, key, v.toUtf8().constData()) == SWMM_OK)
                        bumpWrites();
                });
        return c;
    };
    m_solverCombo = addCombo("rx_solver", tr("Solver:"),
                             {QStringLiteral("EUL"), QStringLiteral("RK5"),
                              QStringLiteral("ROS2"), QStringLiteral("BDF2")},
                             "SOLVER");
    m_couplingCombo = addCombo("rx_coupling", tr("Coupling:"),
                               {QStringLiteral("NONE"),
                                QStringLiteral("FULL")},
                               "COUPLING");
    m_rateUnitsCombo = addCombo("rx_rateUnits", tr("Rate units:"),
                                {QStringLiteral("SEC"), QStringLiteral("MIN"),
                                 QStringLiteral("HR"), QStringLiteral("DAY")},
                                "RATE_UNITS");
    m_areaUnitsCombo = addCombo("rx_areaUnits", tr("Area units:"),
                                {QStringLiteral("FT2"), QStringLiteral("M2"),
                                 QStringLiteral("CM2")},
                                "AREA_UNITS");

    auto addSpin = [&](const char *objName, const QString &label,
                       const char *key, double lo, int dec)
        -> QDoubleSpinBox * {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, w));
        auto *s = makeValueSpin(w, lo, 1.0e9, dec);
        s->setObjectName(QLatin1String(objName));
        row->addWidget(s, 1);
        lay->addLayout(row);
        connect(s, &QDoubleSpinBox::valueChanged, this,
                [this, key](double v) {
                    if (m_loading || !m_engine) return;
                    if (swmm_reaction_option_set(
                            m_engine, key,
                            QString::number(v, 'g', 16).toUtf8()
                                .constData()) == SWMM_OK)
                        bumpWrites();
                });
        return s;
    };
    m_timestepSpin = addSpin("rx_timestep",
                             tr("Reaction step (s, 0 = quality step):"),
                             "TIMESTEP", 0.0, 3);
    m_atolSpin = addSpin("rx_atol", tr("Absolute tolerance:"), "ATOL",
                         1.0e-14, 14);
    m_rtolSpin = addSpin("rx_rtol", tr("Relative tolerance:"), "RTOL",
                         1.0e-14, 14);
    lay->addStretch();
    return w;
}

QWidget *ReactionSystemEditorDialog::buildSpeciesTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_speciesTable = new QTableWidget(0, 5, w);
    m_speciesTable->setObjectName(QStringLiteral("rx_speciesTable"));
    m_speciesTable->setHorizontalHeaderLabels(
        {tr("Kind"), tr("Name"), tr("Units"), tr("Atol"), tr("Rtol")});
    m_speciesTable->verticalHeader()->setVisible(false);
    m_speciesTable->horizontalHeader()->setStretchLastSection(true);
    m_speciesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_speciesTable, 1);

    auto *addRow = new QHBoxLayout;
    m_newSpeciesKind = new QComboBox(w);
    m_newSpeciesKind->setObjectName(QStringLiteral("rx_newSpeciesKind"));
    m_newSpeciesKind->addItem(QStringLiteral("BULK"), 0);
    m_newSpeciesKind->addItem(QStringLiteral("WALL"), 1);
    m_newSpeciesName = new QLineEdit(w);
    m_newSpeciesName->setObjectName(QStringLiteral("rx_newSpeciesName"));
    m_newSpeciesName->setPlaceholderText(tr("Name"));
    m_newSpeciesUnits = new QLineEdit(QStringLiteral("MG"), w);
    m_newSpeciesUnits->setObjectName(QStringLiteral("rx_newSpeciesUnits"));
    auto *addBtn = new QPushButton(tr("&Add"), w);
    addBtn->setObjectName(QStringLiteral("rx_addSpeciesBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), w);
    remBtn->setObjectName(QStringLiteral("rx_removeSpeciesBtn"));
    addRow->addWidget(m_newSpeciesKind);
    addRow->addWidget(m_newSpeciesName, 1);
    addRow->addWidget(m_newSpeciesUnits);
    addRow->addWidget(addBtn);
    addRow->addWidget(remBtn);
    lay->addLayout(addRow);
    connect(addBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onAddSpecies);
    connect(remBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onRemoveSpecies);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildCoefficientsTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_coeffTable = new QTableWidget(0, 3, w);
    m_coeffTable->setObjectName(QStringLiteral("rx_coeffTable"));
    m_coeffTable->setHorizontalHeaderLabels(
        {tr("Kind"), tr("Name"), tr("Value")});
    m_coeffTable->verticalHeader()->setVisible(false);
    m_coeffTable->horizontalHeader()->setStretchLastSection(true);
    m_coeffTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_coeffTable, 1);

    auto *addRow = new QHBoxLayout;
    m_newCoeffKind = new QComboBox(w);
    m_newCoeffKind->setObjectName(QStringLiteral("rx_newCoeffKind"));
    m_newCoeffKind->addItem(QStringLiteral("PARAMETER"), 1);
    m_newCoeffKind->addItem(QStringLiteral("CONSTANT"), 0);
    m_newCoeffName = new QLineEdit(w);
    m_newCoeffName->setObjectName(QStringLiteral("rx_newCoeffName"));
    m_newCoeffName->setPlaceholderText(tr("Name"));
    m_newCoeffValue = makeValueSpin(w, -1.0e9, 1.0e9, 8);
    m_newCoeffValue->setObjectName(QStringLiteral("rx_newCoeffValue"));
    auto *addBtn = new QPushButton(tr("&Add"), w);
    addBtn->setObjectName(QStringLiteral("rx_addCoeffBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), w);
    remBtn->setObjectName(QStringLiteral("rx_removeCoeffBtn"));
    addRow->addWidget(m_newCoeffKind);
    addRow->addWidget(m_newCoeffName, 1);
    addRow->addWidget(m_newCoeffValue);
    addRow->addWidget(addBtn);
    addRow->addWidget(remBtn);
    lay->addLayout(addRow);
    connect(addBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onAddCoefficient);
    connect(remBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onRemoveCoefficient);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildTermsTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_termTable = new QTableWidget(0, 2, w);
    m_termTable->setObjectName(QStringLiteral("rx_termTable"));
    m_termTable->setHorizontalHeaderLabels({tr("Name"), tr("Expression")});
    m_termTable->verticalHeader()->setVisible(false);
    m_termTable->horizontalHeader()->setStretchLastSection(true);
    auto *del = new openswmmvis::ui::ReactionExpressionDelegate(
        m_engine, SWMM_RXN_SCOPE_TERM, m_termTable);
    m_termTable->setItemDelegateForColumn(1, del);
    connect(del,
            &openswmmvis::ui::ReactionExpressionDelegate::validationChanged,
            this, [this](bool ok, const QString &msg, int) {
                setStatus(ok ? QString() : msg, !ok);
            });
    connect(m_termTable, &QTableWidget::cellChanged, this,
            [this](int row, int col) {
                if (m_loading || col != 1 || !m_engine) return;
                const QString expr =
                    m_termTable->item(row, col)->text().trimmed();
                if (swmm_reaction_term_set_expr(
                        m_engine, row, expr.toUtf8().constData()) == SWMM_OK) {
                    bumpWrites();
                    setStatus(QString(), false);
                } else {
                    setStatus(tr("Expression rejected — the previous one is "
                                 "kept."),
                              true);
                    loadTerms();
                }
            });
    lay->addWidget(m_termTable, 1);

    auto *addRow = new QHBoxLayout;
    m_newTermName = new QLineEdit(w);
    m_newTermName->setObjectName(QStringLiteral("rx_newTermName"));
    m_newTermName->setPlaceholderText(tr("Name"));
    auto *addBtn = new QPushButton(tr("&Add"), w);
    addBtn->setObjectName(QStringLiteral("rx_addTermBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), w);
    remBtn->setObjectName(QStringLiteral("rx_removeTermBtn"));
    addRow->addWidget(m_newTermName, 1);
    addRow->addWidget(addBtn);
    addRow->addWidget(remBtn);
    lay->addLayout(addRow);
    connect(addBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onAddTerm);
    connect(remBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onRemoveTerm);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildExpressionsTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    auto *hint = new QLabel(
        tr("One row per species and scope. RATE integrates dφ/dt = expr; "
           "EQUIL solves 0 = expr; FORMULA assigns φ = expr."),
        w);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_exprTable = new QTableWidget(0, 4, w);
    m_exprTable->setObjectName(QStringLiteral("rx_exprTable"));
    m_exprTable->setHorizontalHeaderLabels(
        {tr("Species"), tr("Scope"), tr("Form"), tr("Expression")});
    m_exprTable->verticalHeader()->setVisible(false);
    m_exprTable->horizontalHeader()->setStretchLastSection(true);
    auto *del = new openswmmvis::ui::ReactionExpressionDelegate(
        m_engine, SWMM_RXN_SCOPE_PIPE, m_exprTable);
    m_exprTable->setItemDelegateForColumn(3, del);
    connect(del,
            &openswmmvis::ui::ReactionExpressionDelegate::validationChanged,
            this, [this](bool ok, const QString &msg, int) {
                setStatus(ok ? QString() : msg, !ok);
            });
    connect(m_exprTable, &QTableWidget::cellChanged, this,
            [this](int row, int col) {
                if (m_loading || col != 3 || !m_engine) return;
                const int species = row / 2;
                const int scope = (row % 2 == 0) ? SWMM_RXN_SCOPE_PIPE
                                                 : SWMM_RXN_SCOPE_TANK;
                auto *formCombo = qobject_cast<QComboBox *>(
                    m_exprTable->cellWidget(row, 2));
                const QString expr =
                    m_exprTable->item(row, col)->text().trimmed();
                int form = formCombo ? formCombo->currentData().toInt()
                                     : SWMM_RXN_FORM_RATE;
                if (expr.isEmpty()) form = SWMM_RXN_FORM_NONE;
                else if (form == SWMM_RXN_FORM_NONE)
                    form = SWMM_RXN_FORM_RATE;
                if (swmm_reaction_expr_set(m_engine, scope, species, form,
                                           expr.toUtf8().constData())
                        == SWMM_OK) {
                    bumpWrites();
                    setStatus(QString(), false);
                    if (formCombo) {
                        const int fi = formCombo->findData(form);
                        if (fi >= 0) formCombo->setCurrentIndex(fi);
                    }
                } else {
                    setStatus(tr("Expression rejected — the previous one is "
                                 "kept."),
                              true);
                    loadExpressions();
                }
            });
    lay->addWidget(m_exprTable, 1);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildInitialQualityTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    lay->addWidget(new QLabel(tr("Global initial values:"), w));
    m_initGlobalTable = new QTableWidget(0, 2, w);
    m_initGlobalTable->setObjectName(QStringLiteral("rx_initGlobalTable"));
    m_initGlobalTable->setHorizontalHeaderLabels(
        {tr("Species"), tr("Value")});
    m_initGlobalTable->verticalHeader()->setVisible(false);
    m_initGlobalTable->horizontalHeader()->setStretchLastSection(true);
    m_initGlobalTable->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(m_initGlobalTable);

    lay->addWidget(new QLabel(
        tr("Per-node / per-link overrides ([REACTION_QUALITY] NODE|LINK):"),
        w));
    m_initOverrideTable = new QTableWidget(0, 4, w);
    m_initOverrideTable->setObjectName(
        QStringLiteral("rx_initOverrideTable"));
    m_initOverrideTable->setHorizontalHeaderLabels(
        {tr("Scope"), tr("Element"), tr("Species"), tr("Value")});
    m_initOverrideTable->verticalHeader()->setVisible(false);
    m_initOverrideTable->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(m_initOverrideTable, 1);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("&Add"), w);
    addBtn->setObjectName(QStringLiteral("rx_addInitBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), w);
    remBtn->setObjectName(QStringLiteral("rx_removeInitBtn"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onAddInitOverride);
    connect(remBtn, &QPushButton::clicked,
            this, &ReactionSystemEditorDialog::onRemoveInitOverride);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildFileTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_fileStatus = new QLabel(w);
    m_fileStatus->setObjectName(QStringLiteral("rx_fileStatus"));
    m_fileStatus->setWordWrap(true);
    lay->addWidget(m_fileStatus);

    m_fileEdit = new QPlainTextEdit(w);
    m_fileEdit->setObjectName(QStringLiteral("rx_fileEdit"));
    m_fileEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_fileEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    new RxnFileHighlighter(m_fileEdit->document(), palette());
    lay->addWidget(m_fileEdit, 1);

    // Debounced dry-run validation while typing (D-GC1).
    auto *debounce = new QTimer(m_fileEdit);
    debounce->setSingleShot(true);
    debounce->setInterval(300);
    connect(debounce, &QTimer::timeout, this, [this]() {
        if (!m_engine || m_loading) return;
        const QString text = m_fileEdit->toPlainText();
        if (text.trimmed().isEmpty() || text == m_fileBaseline) {
            m_fileStatus->clear();
            return;
        }
        char err[512] = {};
        if (swmm_reactions_check_text(m_engine, text.toUtf8().constData(),
                                      err, 512) == SWMM_OK)
            m_fileStatus->setText(tr("Valid."));
        else
            m_fileStatus->setText(QString::fromUtf8(err));
    });
    connect(m_fileEdit, &QPlainTextEdit::textChanged, this,
            [this, debounce]() {
                if (!m_loading) debounce->start();
            });

    auto *discardBtn = new QPushButton(tr("&Discard text edits"), w);
    discardBtn->setObjectName(QStringLiteral("rx_discardTextBtn"));
    connect(discardBtn, &QPushButton::clicked, this, [this]() {
        loadFileTab();
    });
    lay->addWidget(discardBtn);
    return w;
}

QWidget *ReactionSystemEditorDialog::buildSourcesPlaceholderTab()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    auto *note = new QLabel(
        tr("[REACTION_SOURCES] arrives with engine phase R-sources; the "
           "engine rejects the section today, so there is nothing to edit."),
        w);
    note->setObjectName(QStringLiteral("rx_sourcesNote"));
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addStretch();
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Loading
// ─────────────────────────────────────────────────────────────────────────────

QStringList ReactionSystemEditorDialog::speciesNames() const
{
    QStringList out;
    if (!m_engine) return out;
    const int n = swmm_reaction_species_count(m_engine);
    for (int i = 0; i < n; ++i) {
        char name[128], units[32];
        int wall = 0;
        double a = 0, r = 0;
        if (swmm_reaction_species_get(m_engine, i, name, 128, &wall, units,
                                      32, &a, &r) == SWMM_OK)
            out << QString::fromUtf8(name);
    }
    return out;
}

void ReactionSystemEditorDialog::reloadAll()
{
    m_loading = true;
    loadOptions();
    loadSpecies();
    loadCoefficients();
    loadTerms();
    loadExpressions();
    loadInitialQuality();
    loadFileTab();
    m_loading = false;
}

void ReactionSystemEditorDialog::loadOptions()
{
    if (!m_engine) return;
    char v[64];
    auto setCombo = [&](QComboBox *c, const char *key) {
        if (swmm_reaction_option_get(m_engine, key, v, 64) == SWMM_OK) {
            const int i = c->findText(QString::fromUtf8(v));
            if (i >= 0) c->setCurrentIndex(i);
        }
    };
    setCombo(m_solverCombo, "SOLVER");
    setCombo(m_couplingCombo, "COUPLING");
    setCombo(m_rateUnitsCombo, "RATE_UNITS");
    setCombo(m_areaUnitsCombo, "AREA_UNITS");
    auto setSpin = [&](QDoubleSpinBox *s, const char *key) {
        if (swmm_reaction_option_get(m_engine, key, v, 64) == SWMM_OK)
            s->setValue(QString::fromUtf8(v).toDouble());
    };
    setSpin(m_timestepSpin, "TIMESTEP");
    setSpin(m_atolSpin, "ATOL");
    setSpin(m_rtolSpin, "RTOL");
}

void ReactionSystemEditorDialog::loadSpecies()
{
    if (!m_engine) return;
    const int n = swmm_reaction_species_count(m_engine);
    m_speciesTable->setRowCount(0);
    for (int i = 0; i < n; ++i) {
        char name[128], units[32];
        int wall = 0;
        double a = 0, r = 0;
        if (swmm_reaction_species_get(m_engine, i, name, 128, &wall, units,
                                      32, &a, &r) != SWMM_OK)
            continue;
        const int row = m_speciesTable->rowCount();
        m_speciesTable->insertRow(row);
        m_speciesTable->setItem(row, 0, new QTableWidgetItem(
            wall ? QStringLiteral("WALL") : QStringLiteral("BULK")));
        m_speciesTable->setItem(row, 1,
                                new QTableWidgetItem(QString::fromUtf8(name)));
        m_speciesTable->setItem(row, 2, new QTableWidgetItem(
                                            QString::fromUtf8(units)));
        m_speciesTable->setItem(row, 3,
                                new QTableWidgetItem(QString::number(a)));
        m_speciesTable->setItem(row, 4,
                                new QTableWidgetItem(QString::number(r)));
    }
}

void ReactionSystemEditorDialog::loadCoefficients()
{
    if (!m_engine) return;
    const int n = swmm_reaction_coeff_count(m_engine);
    m_coeffTable->setRowCount(0);
    for (int i = 0; i < n; ++i) {
        char name[128];
        int is_param = 0;
        double v = 0;
        if (swmm_reaction_coeff_get(m_engine, i, name, 128, &is_param, &v)
                != SWMM_OK)
            continue;
        const int row = m_coeffTable->rowCount();
        m_coeffTable->insertRow(row);
        m_coeffTable->setItem(row, 0, new QTableWidgetItem(
            is_param ? QStringLiteral("PARAMETER")
                     : QStringLiteral("CONSTANT")));
        m_coeffTable->setItem(row, 1,
                              new QTableWidgetItem(QString::fromUtf8(name)));
        auto *spin = makeValueSpin(m_coeffTable, -1.0e9, 1.0e9, 8);
        spin->setObjectName(QStringLiteral("rx_coeffValue_%1").arg(i));
        spin->setValue(v);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, i](double nv) {
                    if (m_loading || !m_engine) return;
                    if (swmm_reaction_coeff_set_value(m_engine, i, nv)
                            == SWMM_OK)
                        bumpWrites();
                });
        m_coeffTable->setCellWidget(row, 2, spin);
    }
}

void ReactionSystemEditorDialog::loadTerms()
{
    if (!m_engine) return;
    m_loading = true;
    const int n = swmm_reaction_term_count(m_engine);
    m_termTable->setRowCount(0);
    for (int i = 0; i < n; ++i) {
        char name[128], expr[512];
        if (swmm_reaction_term_get(m_engine, i, name, 128, expr, 512)
                != SWMM_OK)
            continue;
        const int row = m_termTable->rowCount();
        m_termTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(QString::fromUtf8(name));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_termTable->setItem(row, 0, nameItem);
        m_termTable->setItem(row, 1,
                             new QTableWidgetItem(QString::fromUtf8(expr)));
    }
    m_loading = false;
}

void ReactionSystemEditorDialog::loadExpressions()
{
    if (!m_engine) return;
    m_loading = true;
    const QStringList names = speciesNames();
    m_exprTable->setRowCount(0);
    for (int s = 0; s < names.size(); ++s) {
        for (int half = 0; half < 2; ++half) {
            const int scope = (half == 0) ? SWMM_RXN_SCOPE_PIPE
                                          : SWMM_RXN_SCOPE_TANK;
            int form = SWMM_RXN_FORM_NONE;
            char expr[512] = {};
            swmm_reaction_expr_get(m_engine, scope, s, &form, expr, 512);

            const int row = m_exprTable->rowCount();
            m_exprTable->insertRow(row);
            auto *spItem = new QTableWidgetItem(names[s]);
            spItem->setFlags(spItem->flags() & ~Qt::ItemIsEditable);
            m_exprTable->setItem(row, 0, spItem);
            auto *scItem = new QTableWidgetItem(
                half == 0 ? tr("Pipes") : tr("Tanks"));
            scItem->setFlags(scItem->flags() & ~Qt::ItemIsEditable);
            m_exprTable->setItem(row, 1, scItem);

            auto *formCombo = new QComboBox(m_exprTable);
            formCombo->setObjectName(
                QStringLiteral("rx_form_%1_%2").arg(s).arg(half));
            formCombo->addItem(tr("None"), SWMM_RXN_FORM_NONE);
            formCombo->addItem(QStringLiteral("RATE"), SWMM_RXN_FORM_RATE);
            formCombo->addItem(QStringLiteral("EQUIL"), SWMM_RXN_FORM_EQUIL);
            formCombo->addItem(QStringLiteral("FORMULA"),
                               SWMM_RXN_FORM_FORMULA);
            const int fi = formCombo->findData(form);
            if (fi >= 0) formCombo->setCurrentIndex(fi);
            connect(formCombo, &QComboBox::currentIndexChanged, this,
                    [this, s, scope, formCombo, row]() {
                        if (m_loading || !m_engine) return;
                        const int nf = formCombo->currentData().toInt();
                        auto *cell = m_exprTable->item(row, 3);
                        const QString expr = cell ? cell->text().trimmed()
                                                  : QString();
                        const int rc = swmm_reaction_expr_set(
                            m_engine, scope, s, nf,
                            expr.toUtf8().constData());
                        if (rc == SWMM_OK) {
                            bumpWrites();
                            setStatus(QString(), false);
                        } else if (nf != SWMM_RXN_FORM_NONE
                                   && expr.isEmpty()) {
                            setStatus(tr("Enter an expression for the new "
                                         "form."),
                                      true);
                        } else {
                            setStatus(tr("Form change rejected."), true);
                            loadExpressions();
                        }
                    });
            m_exprTable->setCellWidget(row, 2, formCombo);

            auto *exprItem = new QTableWidgetItem(QString::fromUtf8(expr));
            exprItem->setData(
                openswmmvis::ui::ReactionExpressionDelegate::ScopeRole,
                scope);
            m_exprTable->setItem(row, 3, exprItem);
        }
    }
    m_loading = false;
}

void ReactionSystemEditorDialog::loadInitialQuality()
{
    if (!m_engine) return;
    m_loading = true;
    const QStringList names = speciesNames();

    m_initGlobalTable->setRowCount(0);
    for (int s = 0; s < names.size(); ++s) {
        const int row = m_initGlobalTable->rowCount();
        m_initGlobalTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(names[s]);
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_initGlobalTable->setItem(row, 0, nameItem);
        auto *spin = makeValueSpin(m_initGlobalTable);
        spin->setObjectName(QStringLiteral("rx_initGlobal_%1").arg(s));
        double v = 0;
        swmm_reaction_init_global_get(m_engine, s, &v);
        spin->setValue(v);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, s](double nv) {
                    if (m_loading || !m_engine) return;
                    if (swmm_reaction_init_global_set(m_engine, s, nv)
                            == SWMM_OK)
                        bumpWrites();
                });
        m_initGlobalTable->setCellWidget(row, 1, spin);
    }

    m_initOverrideTable->setRowCount(0);
    const int n = swmm_reaction_init_elem_count(m_engine);
    for (int i = 0; i < n; ++i) {
        int is_link = 0, elem = -1, sp = -1;
        double v = 0;
        if (swmm_reaction_init_elem_get(m_engine, i, &is_link, &elem, &sp,
                                        &v) != SWMM_OK)
            continue;
        onAddInitOverride();                      // builds the row widgets
        const int row = m_initOverrideTable->rowCount() - 1;
        if (auto *c = qobject_cast<QComboBox *>(
                m_initOverrideTable->cellWidget(row, 0))) {
            const int idx = c->findData(is_link);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        // Repopulate the element combo for the (possibly changed) scope.
        if (auto *c = qobject_cast<QComboBox *>(
                m_initOverrideTable->cellWidget(row, 1))) {
            const int idx = c->findData(elem);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        if (auto *c = qobject_cast<QComboBox *>(
                m_initOverrideTable->cellWidget(row, 2))) {
            const int idx = c->findData(sp);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        if (auto *sBox = qobject_cast<QDoubleSpinBox *>(
                m_initOverrideTable->cellWidget(row, 3)))
            sBox->setValue(v);
    }
    m_loading = false;
}

void ReactionSystemEditorDialog::loadFileTab()
{
    if (!m_engine) return;
    const bool was = m_loading;
    m_loading = true;
    m_fileBaseline = engineText(m_engine);
    m_fileEdit->setPlainText(m_fileBaseline);
    m_fileStatus->clear();
    m_loading = was;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutations
// ─────────────────────────────────────────────────────────────────────────────

void ReactionSystemEditorDialog::onAddSpecies()
{
    if (!m_engine) return;
    const QString name = m_newSpeciesName->text().trimmed();
    if (name.isEmpty()) {
        setStatus(tr("Enter a species name."), true);
        return;
    }
    const int rc = swmm_reaction_species_add(
        m_engine, name.toUtf8().constData(),
        m_newSpeciesKind->currentData().toInt(),
        m_newSpeciesUnits->text().trimmed().toUtf8().constData(), 0.0, 0.0);
    if (rc != SWMM_OK) {
        setStatus(tr("Species '%1' was refused (duplicate or invalid "
                     "name).").arg(name), true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    m_newSpeciesName->clear();
    reloadAll();
}

void ReactionSystemEditorDialog::onRemoveSpecies()
{
    if (!m_engine) return;
    const int row = m_speciesTable->currentRow();
    if (row < 0) return;
    const int rc = swmm_reaction_species_remove(m_engine, row);
    if (rc != SWMM_OK) {
        setStatus(tr("Species removal refused — an expression still "
                     "references it."),
                  true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    reloadAll();
}

void ReactionSystemEditorDialog::onAddCoefficient()
{
    if (!m_engine) return;
    const QString name = m_newCoeffName->text().trimmed();
    if (name.isEmpty()) {
        setStatus(tr("Enter a coefficient name."), true);
        return;
    }
    if (swmm_reaction_coeff_add(m_engine, name.toUtf8().constData(),
                                m_newCoeffKind->currentData().toInt(),
                                m_newCoeffValue->value()) != SWMM_OK) {
        setStatus(tr("Coefficient '%1' was refused (duplicate name).")
                      .arg(name),
                  true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    m_newCoeffName->clear();
    reloadAll();
}

void ReactionSystemEditorDialog::onRemoveCoefficient()
{
    if (!m_engine) return;
    const int row = m_coeffTable->currentRow();
    if (row < 0) return;
    if (swmm_reaction_coeff_remove(m_engine, row) != SWMM_OK) {
        setStatus(tr("Coefficient removal refused — an expression still "
                     "references it."),
                  true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    reloadAll();
}

void ReactionSystemEditorDialog::onAddTerm()
{
    if (!m_engine) return;
    const QString name = m_newTermName->text().trimmed();
    if (name.isEmpty()) {
        setStatus(tr("Enter a term name."), true);
        return;
    }
    if (swmm_reaction_term_add(m_engine, name.toUtf8().constData(), "0")
            != SWMM_OK) {
        setStatus(tr("Term '%1' was refused (duplicate name).").arg(name),
                  true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    m_newTermName->clear();
    reloadAll();
}

void ReactionSystemEditorDialog::onRemoveTerm()
{
    if (!m_engine) return;
    const int row = m_termTable->currentRow();
    if (row < 0) return;
    if (swmm_reaction_term_remove(m_engine, row) != SWMM_OK) {
        setStatus(tr("Term removal refused — an expression still references "
                     "it."),
                  true);
        return;
    }
    bumpWrites();
    setStatus(QString(), false);
    reloadAll();
}

void ReactionSystemEditorDialog::onAddInitOverride()
{
    if (!m_engine) return;
    const int row = m_initOverrideTable->rowCount();
    m_initOverrideTable->insertRow(row);

    auto commit = [this](int r) {
        if (m_loading || !m_engine) return;
        auto *sc = qobject_cast<QComboBox *>(
            m_initOverrideTable->cellWidget(r, 0));
        auto *ec = qobject_cast<QComboBox *>(
            m_initOverrideTable->cellWidget(r, 1));
        auto *pc = qobject_cast<QComboBox *>(
            m_initOverrideTable->cellWidget(r, 2));
        auto *vs = qobject_cast<QDoubleSpinBox *>(
            m_initOverrideTable->cellWidget(r, 3));
        if (!sc || !ec || !pc || !vs || ec->currentIndex() < 0 ||
            pc->currentIndex() < 0)
            return;
        if (swmm_reaction_init_elem_set(m_engine, sc->currentData().toInt(),
                                        ec->currentData().toInt(),
                                        pc->currentData().toInt(),
                                        vs->value()) == SWMM_OK)
            bumpWrites();
    };

    auto *scopeCombo = new QComboBox(m_initOverrideTable);
    scopeCombo->addItem(tr("Node"), 0);
    scopeCombo->addItem(tr("Link"), 1);
    m_initOverrideTable->setCellWidget(row, 0, scopeCombo);

    auto *elemCombo = new QComboBox(m_initOverrideTable);
    m_initOverrideTable->setCellWidget(row, 1, elemCombo);
    auto populateElems = [this, scopeCombo, elemCombo]() {
        const bool link = scopeCombo->currentData().toInt() == 1;
        elemCombo->clear();
        const int n = link ? swmm_link_count(m_engine)
                           : swmm_node_count(m_engine);
        for (int i = 0; i < n; ++i) {
            const char *id = link ? swmm_link_id(m_engine, i)
                                  : swmm_node_id(m_engine, i);
            elemCombo->addItem(id ? QString::fromUtf8(id)
                                  : QStringLiteral("#%1").arg(i), i);
        }
    };
    populateElems();
    connect(scopeCombo, &QComboBox::currentIndexChanged, this,
            [populateElems]() { populateElems(); });

    auto *spCombo = new QComboBox(m_initOverrideTable);
    const QStringList names = speciesNames();
    for (int s = 0; s < names.size(); ++s) spCombo->addItem(names[s], s);
    m_initOverrideTable->setCellWidget(row, 2, spCombo);

    auto *spin = makeValueSpin(m_initOverrideTable);
    m_initOverrideTable->setCellWidget(row, 3, spin);
    connect(spin, &QDoubleSpinBox::valueChanged, this,
            [commit, row]() { commit(row); });
}

void ReactionSystemEditorDialog::onRemoveInitOverride()
{
    if (!m_engine) return;
    const int row = m_initOverrideTable->currentRow();
    if (row < 0) return;
    // The engine list mirrors the table order (both hydrate in entry
    // order and appends land at the end), so the row index IS the entry
    // index while the dialog is the only writer.
    if (row < swmm_reaction_init_elem_count(m_engine)) {
        if (swmm_reaction_init_elem_remove(m_engine, row) == SWMM_OK)
            bumpWrites();
    }
    m_initOverrideTable->removeRow(row);
}

// ─────────────────────────────────────────────────────────────────────────────
// File tab sync + save + binding
// ─────────────────────────────────────────────────────────────────────────────

void ReactionSystemEditorDialog::onTabChanged(int index)
{
    if (m_loading) { m_lastTabIndex = index; return; }
    if (index == m_fileTabIndex) {
        // Entering: regenerate from engine state so structured edits are
        // always visible (D-GC1) — unless we are bouncing back from a
        // failed apply, where reloading would destroy the user's text
        // and the diagnostic they need to fix it.
        if (!m_gatingBack) loadFileTab();
    } else if (m_lastTabIndex == m_fileTabIndex) {
        // Leaving: apply edits; failure gates the switch.
        if (!applyFileTab()) {
            m_gatingBack = true;
            m_tabs->setCurrentIndex(m_fileTabIndex);
            m_gatingBack = false;
            m_lastTabIndex = m_fileTabIndex;
            return;
        }
        reloadAll();
    }
    m_lastTabIndex = index;
}

bool ReactionSystemEditorDialog::applyFileTab()
{
    if (!m_engine || !m_fileEdit) return true;
    const QString text = m_fileEdit->toPlainText();
    if (text == m_fileBaseline) return true;      // untouched: zero calls
    char err[512] = {};
    if (swmm_reactions_apply_text(m_engine, text.toUtf8().constData(), err,
                                  512) != SWMM_OK) {
        m_fileStatus->setText(QString::fromUtf8(err));
        setStatus(QString::fromUtf8(err), true);
        return false;
    }
    bumpWrites();
    m_fileBaseline = engineText(m_engine);
    setStatus(QString(), false);
    return true;
}

void ReactionSystemEditorDialog::onSave()
{
    if (!m_engine) return;
    if (m_tabs->currentIndex() == m_fileTabIndex && !applyFileTab()) return;

    if (swmm_process_component_find(m_engine, kReactionsId) < 0) {
        // G-D1: one-step "create component + config file".
        const auto rc = QMessageBox::question(
            this, tr("Create reactions component"),
            tr("No reactions component is bound to this model. Register "
               "one and create its config file (model.rxn beside the "
               "project input)?"));
        if (rc != QMessageBox::Yes) return;
        if (swmm_process_component_register(m_engine, kReactionsId,
                                            "model.rxn") != SWMM_OK) {
            setStatus(tr("Component registration failed."), true);
            return;
        }
        bumpWrites();
    }
    if (swmm_reactions_save(m_engine, nullptr) == SWMM_OK) {
        setStatus(tr("Saved."), false);
        bumpWrites();
        refreshBinding();
    } else {
        setStatus(tr("Save failed — is a config path bound?"), true);
    }
}

void ReactionSystemEditorDialog::refreshBinding()
{
    if (!m_engine) return;
    const int i = swmm_process_component_find(m_engine, kReactionsId);
    if (i >= 0) {
        char id[128], cfg[512], res[512];
        if (swmm_process_component_get(m_engine, i, id, 128, cfg, 512, res,
                                       512) == SWMM_OK) {
            const QString path = QString::fromUtf8(*res ? res : cfg);
            setWindowTitle(tr("Reaction System — %1")
                               .arg(QFileInfo(path).fileName()));
            return;
        }
    }
    setWindowTitle(tr("Reaction System — (no file bound)"));
}

void ReactionSystemEditorDialog::setStatus(const QString &msg, bool error)
{
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(
        error ? QStringLiteral("color: palette(link);") : QString());
}

} // namespace OpenSWMMVis
