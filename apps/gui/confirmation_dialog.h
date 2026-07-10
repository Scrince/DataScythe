#pragma once

#include "platform/drive_info.h"

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

namespace datascythe::gui {

class ConfirmationDialog : public QDialog {
    Q_OBJECT

public:
    ConfirmationDialog(const DriveInfo& drive, const QString& operation_title,
                       const QString& operation_description, QWidget* parent = nullptr);

    
    ConfirmationDialog(const QString& target_path, const QString& confirmation_phrase,
                       const QString& operation_title, const QString& operation_description,
                       QWidget* parent = nullptr);

    bool confirmed() const { return confirmed_; }

private slots:
    void on_accept();

private:
    bool confirmed_ = false;
    QString required_phrase_;
    QLineEdit* phrase_edit_ = nullptr;
    QLabel* warning_label_ = nullptr;
};

}  