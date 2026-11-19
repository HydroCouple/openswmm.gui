/**
 * @file      labeledcontrols.h
 * @brief     Slice BM Phase 6.3.3 — common form-row helpers shared by every concrete editor in BN
 *            / BO / BP / BQ / BR / BS / BT.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     * @brief Create a label/editor row whose child controls are owned by this widget.
     * @param[in] parent Optional QWidget owner; access the controls on the GUI thread.
     * @param[in] labelText Display label with Qt mnemonic support; no unit conversion is inferred from the label.
     */
    explicit LabeledLineEdit(const QString &labelText, QWidget *parent = nullptr);

    /**
     * @brief Read the line editor's current text.
     * @return Text copy, including empty text.
     */
    [[nodiscard]] QString text() const;
    /**
     * @brief Replace the line editor text using QLineEdit's normal notification behavior.
     * @param[in] v Text, including empty; no validation is added by this wrapper.
     */
    void setText(const QString &v);
    /**
     * @brief Access the child editor for validators and editor-specific configuration.
     * @return Borrowed child pointer; do not delete it independently.
     */
    [[nodiscard]] QLineEdit *lineEdit() const { return m_edit; }

signals:
    /**
     * @brief Forward the child line editor's textChanged notification, including programmatic edits.
     * @param[in] text Current text.
     */
    void textChanged(const QString &text);

private:
    QLabel *m_label = nullptr;
    QLineEdit *m_edit = nullptr;
};

/*! Label + QDoubleSpinBox pair with `[min, max]` range and `decimals`. */
class LabeledDoubleSpin : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a label/editor row whose child controls are owned by this widget.
     * @param[in] parent Optional QWidget owner; access the controls on the GUI thread.
     * @param[in] labelText Display label with Qt mnemonic support; no unit conversion is inferred from the label.
     * @param[in] minimum Lower bound in caller-selected units, passed to the Qt spin box.
     * @param[in] maximum Upper bound in the same units; Qt applies its range normalization.
     * @param[in] decimals Decimal precision passed to QDoubleSpinBox (default three).
     */
    LabeledDoubleSpin(const QString &labelText, double minimum, double maximum, int decimals = 3,
                      QWidget *parent = nullptr);

    /**
     * @brief Read the spin box's current accepted value in caller-selected units.
     * @return Clamped precision-rounded Qt control value.
     */
    [[nodiscard]] double value() const;
    /**
     * @brief Set the spin value under its configured Qt range and decimal precision.
     * @param[in] v Requested value in the same units as the constructor bounds; valueChanged is forwarded if the accepted value changes.
     */
    void setValue(double v);
    /**
     * @brief Access the owned spin box for additional configuration.
     * @return Borrowed child pointer; lifetime follows this widget.
     */
    [[nodiscard]] QDoubleSpinBox *spin() const { return m_spin; }

signals:
    /**
     * @brief Forward an accepted spin-box value change, including programmatic changes.
     * @param[in] v Current accepted value in caller-selected units.
     */
    void valueChanged(double v);

private:
    QLabel *m_label = nullptr;
    QDoubleSpinBox *m_spin = nullptr;
};

/*! Label + QSpinBox pair (integer). */
class LabeledIntSpin : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a label/editor row whose child controls are owned by this widget.
     * @param[in] parent Optional QWidget owner; access the controls on the GUI thread.
     * @param[in] labelText Display label with Qt mnemonic support; no unit conversion is inferred from the label.
     * @param[in] minimum Lower bound in caller-selected units, passed to the Qt spin box.
     * @param[in] maximum Upper bound in the same units; Qt applies its range normalization.
     */
    LabeledIntSpin(const QString &labelText, int minimum, int maximum, QWidget *parent = nullptr);

    /**
     * @brief Read the spin box's current accepted value in caller-selected units.
     * @return Clamped Qt control value.
     */
    [[nodiscard]] int value() const;
    /**
     * @brief Set the spin value under its configured Qt range.
     * @param[in] v Requested value in the same units as the constructor bounds; valueChanged is forwarded if the accepted value changes.
     */
    void setValue(int v);
    /**
     * @brief Access the owned spin box for additional configuration.
     * @return Borrowed child pointer; lifetime follows this widget.
     */
    [[nodiscard]] QSpinBox *spin() const { return m_spin; }

signals:
    /**
     * @brief Forward an accepted spin-box value change, including programmatic changes.
     * @param[in] v Current accepted value in caller-selected units.
     */
    void valueChanged(int v);

private:
    QLabel *m_label = nullptr;
    QSpinBox *m_spin = nullptr;
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
    /**
     * @brief Create a label/editor row whose child controls are owned by this widget.
     * @param[in] parent Optional QWidget owner; access the controls on the GUI thread.
     * @param[in] labelText Display label with Qt mnemonic support; no unit conversion is inferred from the label.
     */
    explicit LabeledCombo(const QString &labelText, QWidget *parent = nullptr);

    /**
     * @brief Append a combo row in insertion order.
     * @param[in] text Display label.
     * @param[in] data Optional UserRole payload, commonly an engine enum integer. Adding the first item can change selection and notify.
     */
    void addItem(const QString &text, const QVariant &data = {});

    /**
     * @brief Read the child combo selection.
     * @return Zero-based item index, or -1 without a selection.
     */
    [[nodiscard]] int currentIndex() const;
    /**
     * @brief Read the selected item's UserRole payload.
     * @return Stored payload, or invalid QVariant without a selected/payload item.
     */
    [[nodiscard]] QVariant currentData() const;
    /**
     * @brief Select a child-combo row with Qt's normal index semantics.
     * @param[in] idx Zero-based item index; -1 clears selection. currentIndexChanged is forwarded when Qt changes selection.
     */
    void setCurrentIndex(int idx);

    /*! Select the item whose data() equals \p data, or no-op if none match.
     * @param[in] data Payload searched by QComboBox::findData() using default UserRole matching; only the first match is selected.
     */
    void setCurrentData(const QVariant &data);

    /**
     * @brief Access the widget-owned combo for caller configuration.
     * @return Borrowed child pointer; do not delete it independently.
     */
    [[nodiscard]] QComboBox *combo() const { return m_combo; }

signals:
    /**
     * @brief Forward a changed combo index, including programmatic selection.
     * @param[in] idx Current zero-based item index, or -1.
     */
    void currentIndexChanged(int idx);

private:
    QLabel *m_label = nullptr;
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
     *                    column then).
     * @brief Create a label/editor row whose child controls are owned by this widget.
     * @param[in] parent Optional QWidget owner; access the controls on the GUI thread.
     */
    explicit LabeledPickerCombo(const QString &labelText = {}, QWidget *parent = nullptr);

    /*! Replace the combo's items with \p items, optionally selecting
     *  the entry matching \p selected. An empty `items` list yields an
     *  empty combo (with the single "(none)" placeholder still selectable).
     * @param[in] items Display/object names inserted after a permanent empty-payload (none) placeholder.
     * @param[in] selected Exact item text to select; empty or missing text leaves the placeholder selected.
     * @note Child combo signals are blocked during replacement, so currentTextChanged is not emitted.
     */
    void setItems(const QStringList &items, const QString &selected = {});
    /**
     * @brief Read the chosen object identity while mapping the first placeholder row to no identity.
     * @return Empty for row zero; otherwise the combo text.
     */
    [[nodiscard]] QString currentText() const;
    /**
     * @brief Select an object by exact displayed text, adding a fallback row if it is absent.
     * @param[in] v Identity; empty selects the first placeholder. Normal combo notifications are forwarded.
     */
    void setCurrentText(const QString &v);
    /**
     * @brief Access the widget-owned combo for caller configuration.
     * @return Borrowed child pointer; do not delete it independently.
     */
    [[nodiscard]] QComboBox *combo() const { return m_combo; }
    /**
     * @brief Access the owned create-object button.
     * @return Borrowed child pointer whose click emits pickerClicked().
     */
    [[nodiscard]] QToolButton *button() const { return m_btn; }

signals:
    /*! Fired when the user clicks the "..." button. Caller opens the
     *  matching creation dialog. */
    void pickerClicked();
    /*! Mirror of the combo's currentTextChanged for caller convenience.
     * @param[in] text Raw combo display text, including the translated (none) placeholder when it becomes selected; currentText() instead maps that placeholder to empty.
     */
    void currentTextChanged(const QString &text);

private:
    QLabel *m_label = nullptr;
    QComboBox *m_combo = nullptr;
    QToolButton *m_btn = nullptr;
};

#endif // LABELEDCONTROLS_H
