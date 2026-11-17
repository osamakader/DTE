#include "PropertyDialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace DTE {

PropertyDialog::PropertyDialog(const QString& title, const QString& nodePath, const QString& name,
                               const QString& value, Validator validator, QWidget* parent)
    : QDialog(parent), validator_(std::move(validator)) {
    setWindowTitle(title);
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    auto* nodeLabel = new QLabel(QString("Node: <b>%1</b>").arg(nodePath.toHtmlEscaped()));
    layout->addWidget(nodeLabel);

    auto* form = new QFormLayout();
    nameEdit_ = new QLineEdit(name);
    nameEdit_->setObjectName("propertyName");
    valueEdit_ = new QLineEdit(value);
    valueEdit_->setObjectName("propertyValue");
    valueEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    form->addRow("Name:", nameEdit_);
    form->addRow("Value:", valueEdit_);
    layout->addLayout(form);

    auto* help = new QLabel(
        "Value in DTS syntax, as after '=':<br>"
        "<code>\"okay\"</code> &nbsp; <code>\"vendor,a\", \"vendor,b\"</code> &nbsp; "
        "<code>&lt;0x1000 0x100&gt;</code> &nbsp; <code>/bits/ 64 &lt;1&gt;</code> &nbsp; "
        "<code>[00 11 22]</code><br>Leave empty for a property without a value (e.g. <code>ranges;</code>).");
    help->setWordWrap(true);
    layout->addWidget(help);

    errorLabel_ = new QLabel();
    errorLabel_->setObjectName("propertyError");
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet("color: #d32f2f;");
    layout->addWidget(errorLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(nameEdit_, &QLineEdit::textChanged, this, &PropertyDialog::revalidate);
    connect(valueEdit_, &QLineEdit::textChanged, this, &PropertyDialog::revalidate);

    (name.isEmpty() ? nameEdit_ : valueEdit_)->setFocus();
    revalidate();
}

QString PropertyDialog::name() const {
    return nameEdit_->text().trimmed();
}

QString PropertyDialog::value() const {
    return valueEdit_->text();
}

void PropertyDialog::revalidate() {
    const QString error = validator_ ? validator_(name(), value()) : QString();
    errorLabel_->setText(error);
    okButton_->setEnabled(error.isEmpty());
}

} // namespace DTE
