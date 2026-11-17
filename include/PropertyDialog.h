#pragma once

#include <QDialog>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;

namespace DTE {

// Dialog for adding or editing a property: name and value in DTS syntax.
// The validator returns an error message for the current input, or an
// empty string; OK is enabled only for valid input.
class PropertyDialog : public QDialog {
    Q_OBJECT

public:
    using Validator = std::function<QString(const QString& name, const QString& value)>;

    PropertyDialog(const QString& title, const QString& nodePath, const QString& name,
                   const QString& value, Validator validator, QWidget* parent = nullptr);

    QString name() const;
    QString value() const;

private:
    void revalidate();

    QLineEdit* nameEdit_;
    QLineEdit* valueEdit_;
    QLabel* errorLabel_;
    QPushButton* okButton_;
    Validator validator_;
};

} // namespace DTE
