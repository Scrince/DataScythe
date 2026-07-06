#include "confirmation_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace datascythe::gui {

ConfirmationDialog::ConfirmationDialog(const DriveInfo& drive, const QString& operation_title,
                                       const QString& operation_description, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Confirm Destructive Operation");
    setModal(true);
    resize(560, 320);

    required_phrase_ = QString("ERASE %1").arg(drive.physical_index);

    auto* layout = new QVBoxLayout(this);
    warning_label_ = new QLabel(this);
    warning_label_->setWordWrap(true);
    warning_label_->setText(
        QString("<b style='color:#b00020;'>WARNING</b><br/>"
                "This will <b>irreversibly destroy all data</b> on:<br/>"
                "<pre>%1</pre>"
                "Operation: <b>%2</b><br/>%3<br/><br/>"
                "Type <b>%4</b> below to confirm.")
            .arg(QString::fromStdString(drive.device_path))
            .arg(operation_title)
            .arg(operation_description)
            .arg(required_phrase_));
    layout->addWidget(warning_label_);

    phrase_edit_ = new QLineEdit(this);
    phrase_edit_->setPlaceholderText(required_phrase_);
    layout->addWidget(phrase_edit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* confirm_button =
        buttons->addButton("I understand, erase now", QDialogButtonBox::AcceptRole);
    confirm_button->setEnabled(false);
    layout->addWidget(buttons);

    connect(phrase_edit_, &QLineEdit::textChanged, this,
            [confirm_button, this](const QString& text) {
                confirm_button->setEnabled(text.trimmed() == required_phrase_);
            });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(confirm_button, &QPushButton::clicked, this, &ConfirmationDialog::on_accept);
}

ConfirmationDialog::ConfirmationDialog(const QString& target_path,
                                       const QString& confirmation_phrase,
                                       const QString& operation_title,
                                       const QString& operation_description, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Confirm Destructive Operation");
    setModal(true);
    resize(560, 320);

    required_phrase_ = confirmation_phrase;

    auto* layout = new QVBoxLayout(this);
    warning_label_ = new QLabel(this);
    warning_label_->setWordWrap(true);
    warning_label_->setText(
        QString("<b style='color:#b00020;'>WARNING</b><br/>"
                "This will <b>irreversibly destroy data</b> at:<br/>"
                "<pre>%1</pre>"
                "Operation: <b>%2</b><br/>%3<br/><br/>"
                "Type <b>%4</b> below to confirm.")
            .arg(target_path)
            .arg(operation_title)
            .arg(operation_description)
            .arg(required_phrase_));
    layout->addWidget(warning_label_);

    phrase_edit_ = new QLineEdit(this);
    phrase_edit_->setPlaceholderText(required_phrase_);
    layout->addWidget(phrase_edit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* confirm_button =
        buttons->addButton("I understand, erase now", QDialogButtonBox::AcceptRole);
    confirm_button->setEnabled(false);
    layout->addWidget(buttons);

    connect(phrase_edit_, &QLineEdit::textChanged, this,
            [confirm_button, this](const QString& text) {
                confirm_button->setEnabled(text.trimmed() == required_phrase_);
            });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(confirm_button, &QPushButton::clicked, this, &ConfirmationDialog::on_accept);
}

void ConfirmationDialog::on_accept() {
    if (phrase_edit_->text().trimmed() != required_phrase_) {
        return;
    }
    confirmed_ = true;
    accept();
}

}  // namespace datascythe::gui