/*!
 * \file   labeledcontrols.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BM Phase 6.3.3 — common form-row helpers shared by every concrete
 * editor in BN / BO / BP / BQ / BR / BS / BT.
 *
 * Each helper is a tiny QWidget composing a `QLabel` and one editor
 * control on a `QHBoxLayout`. They exist to keep concrete editors
 * declarative — instead of writing the addRow boilerplate 50 times,
 * editors do:
 *
 *   auto *area = new LabeledDoubleSpin(tr("Area (ac)"), 0.0, 1e6, 2);
 *   form->addRow(area);
 *
 * Scope cap (CLAUDE.md §2 — Simplicity First): only the four controls
 * actually consumed by the first wave of concrete editors are exposed.
 * `LabeledFilePicker`, `ConditionalSubEditor`, and `SubsectionGroupBox`
 * referenced by the plan's later phases land with the slices that
 * need them.
 */
#ifndef LABELEDCONTROLS_H
#define LABELEDCONTROLS_H

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QToolButton;

/*! Label + QLineEdit pair. */
class LabeledLineEdit : public QWidget
{
    Q_OBJECT
public:
    explicit LabeledLineEdit(const QString &labelText,
                             QWidget       *parent = nullptr);

    [[nodiscard]] QString text() const;
    void                  setText(const QString &v);
    [[nodiscard]] QLineEdit *lineEdit() const { return m_edit; }

signals:
    void textChanged(const QString &text);

private:
    QLabel    *m_label = nullptr;
    QLineEdit *m_edit  = nullptr;
};

/*! Label + QDoubleSpinBox pair with `[min, max]` range and `decimals`. */
class LabeledDoubleSpin : public QWidget
{
    Q_OBJECT
public:
    LabeledDoubleSpin(const QString &labelText,
                      double         minimum,
                      double         maximum,
                      int            decimals = 3,
                      QWidget       *parent   = nullptr);

    [[nodiscard]] double value() const;
    void                 setValue(double v);
    [[nodiscard]] QDoubleSpinBox *spin() const { return m_spin; }

signals:
    void valueChanged(double v);

private:
    QLabel         *m_label = nullptr;
    QDoubleSpinBox *m_spin  = nullptr;
};

/*! Label + QSpinBox pair (integer). */
class LabeledIntSpin : public QWidget
{
    Q_OBJECT
public:
    LabeledIntSpin(const QString &labelText,
                   int            minimum,
                   int            maximum,
                   QWidget       *parent = nullptr);

    [[nodiscard]] int value() const;
    void              setValue(int v);
    [[nodiscard]] QSpinBox *spin() const { return m_spin; }

signals:
    void valueChanged(int v);

private:
    QLabel   *m_label = nullptr;
    QSpinBox *m_spin  = nullptr;
};

/*!
 * Label + QComboBox pair. Items are added via `addItem(text, data)` in
 * insertion order — concrete editors typically map an engine enum value
 * to a user-facing label and pass the enum integer as `data`.
 */
class LabeledCombo : public QWidget
{
    Q_OBJECT
public:
    explicit LabeledCombo(const QString &labelText,
                          QWidget       *parent = nullptr);

    void addItem(const QString &text, const QVariant &data = {});

    [[nodiscard]] int      currentIndex() const;
    [[nodiscard]] QVariant currentData() const;
    void                   setCurrentIndex(int idx);

    /*! Select the item whose data() equals \p data, or no-op if none match. */
    void setCurrentData(const QVariant &data);

    [[nodiscard]] QComboBox *combo() const { return m_combo; }

signals:
    void currentIndexChanged(int idx);

private:
    QLabel    *m_label = nullptr;
    QComboBox *m_combo = nullptr;
};

/*!
 * Data-object picker: QComboBox listing existing instances of a SWMM
 * data object (time series, pattern, unit-hydrograph group, ...) plus
 * a small "..." button beside it. Caller fills the combo via setItems()
 * and connects `pickerClicked` to whatever creation flow (typically
 * `NewDataObjectDialog::getNew(...)` + `SWMMModelLayer::createDataObject(...)`)
 * is appropriate for that data kind. After creation the caller calls
 * setItems again with the refreshed list and setCurrentText(spec.name).
 *
 * Used 7× in NodeCompoundEditDialog (DB.4): Inflows TS, Inflows Pattern,
 * DWF Monthly/Daily/Hourly/Weekend, RDII UH.
 */
class LabeledPickerCombo : public QWidget
{
    Q_OBJECT
public:
    /*! \param labelText  Optional label text; pass an empty string to
     *                    embed the picker in a form layout that already
     *                    supplies its own label (no left-pane label
     *                    column then). */
    explicit LabeledPickerCombo(const QString &labelText = {},
                                 QWidget       *parent    = nullptr);

    /*! Replace the combo's items with \p items, optionally selecting
     *  the entry matching \p selected. An empty `items` list yields an
     *  empty combo (with the single "(none)" placeholder still selectable). */
    void                  setItems(const QStringList &items,
                                   const QString     &selected = {});
    [[nodiscard]] QString currentText() const;
    void                  setCurrentText(const QString &v);
    [[nodiscard]] QComboBox  *combo()  const { return m_combo; }
    [[nodiscard]] QToolButton *button() const { return m_btn;   }

signals:
    /*! Fired when the user clicks the "..." button. Caller opens the
     *  matching creation dialog. */
    void pickerClicked();
    /*! Mirror of the combo's currentTextChanged for caller convenience. */
    void currentTextChanged(const QString &text);

private:
    QLabel      *m_label = nullptr;
    QComboBox   *m_combo = nullptr;
    QToolButton *m_btn   = nullptr;
};

#endif // LABELEDCONTROLS_H
