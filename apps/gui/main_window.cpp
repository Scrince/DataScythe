#include "main_window.h"

#include "confirmation_dialog.h"
#include "drive_table_model.h"

#include "core/certificate.h"
#include "core/preflight.h"
#include "core/verification.h"

#include "platform/drive_analyzer.h"
#include "platform/drive_enumerator.h"
#include "platform/raw_device.h"
#include "platform/secure_erase.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>

#include <functional>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace datascythe::gui {

namespace {

QString mode_title(EraseMode mode) {
    switch (mode) {
        case EraseMode::FullDeviceWipe:
            return "Full-device wipe";
        case EraseMode::QuickZeroFill:
            return "Quick zero-fill";
        case EraseMode::ShredVolume:
            return "Secure erase volume";
        case EraseMode::ShredFiles:
            return "Secure erase file(s)";
        case EraseMode::ShredDirectory:
            return "Secure erase folder";
        case EraseMode::SsdSecureErase:
            return "SSD hardware secure erase";
    }
    return "Secure erase";
}

QString mode_description(EraseMode mode) {
    switch (mode) {
        case EraseMode::FullDeviceWipe:
            return "Overwrites every OS-addressable byte on the physical drive, including "
                   "partition tables and unallocated space.";
        case EraseMode::QuickZeroFill:
            return "Performs a single pass of zeros across the device for fast sanitization.";
        case EraseMode::ShredVolume:
            return "Shred-style overwrite of the first mounted volume on the selected drive.";
        case EraseMode::ShredFiles:
            return "Overwrites selected files with shred-style passes and optionally deletes them.";
        case EraseMode::ShredDirectory:
            return "Overwrites all files in the selected folder (optionally including subfolders).";
        case EraseMode::SsdSecureErase:
            return "Issues hardware secure erase: NVMe Sanitize or ATA SECURITY ERASE when "
                   "supported by the drive firmware.";
    }
    return {};
}

}  

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("DataScythe - Secure Drive Eraser");
    resize(1020, 700);

    auto* central = new QWidget(this);
    auto* root_layout = new QVBoxLayout(central);

    auto* header = new QLabel(
        "<b>DataScythe</b> securely erases drives, volumes, and files. "
        "Administrator privileges are required for raw device access.",
        this);
    header->setWordWrap(true);
    root_layout->addWidget(header);

    auto* tabs = new QTabWidget(this);

    auto* erase_tab = new QWidget(this);
    auto* erase_layout = new QVBoxLayout(erase_tab);

    model_ = new DriveTableModel(this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    erase_layout->addWidget(table_, 1);

    auto* config_group = new QGroupBox("Erase configuration", this);
    auto* config_layout = new QHBoxLayout(config_group);

    config_layout->addWidget(new QLabel("Passes:", this));
    pass_spin_ = new QSpinBox(this);
    pass_spin_->setRange(1, 100);
    pass_spin_->setValue(3);
    config_layout->addWidget(pass_spin_);

    random_check_ = new QCheckBox("Random passes", this);
    random_check_->setChecked(true);
    config_layout->addWidget(random_check_);

    zero_check_ = new QCheckBox("Final zero pass", this);
    zero_check_->setChecked(true);
    config_layout->addWidget(zero_check_);

    remove_check_ = new QCheckBox("Remove files after shred", this);
    remove_check_->setChecked(true);
    config_layout->addWidget(remove_check_);

    recursive_check_ = new QCheckBox("Recursive folders", this);
    recursive_check_->setChecked(true);
    config_layout->addWidget(recursive_check_);

    verify_check_ = new QCheckBox("Verify after erase", this);
    config_layout->addWidget(verify_check_);

    partition_check_ = new QCheckBox("Wipe partition metadata", this);
    partition_check_->setChecked(true);
    config_layout->addWidget(partition_check_);

    ads_check_ = new QCheckBox("Shred NTFS ADS", this);
    ads_check_->setChecked(true);
    config_layout->addWidget(ads_check_);

    export_cert_check_ = new QCheckBox("Auto-save certificate", this);
    export_cert_check_->setChecked(true);
    config_layout->addWidget(export_cert_check_);
    config_layout->addStretch();
    erase_layout->addWidget(config_group);

    auto* drive_buttons = new QHBoxLayout();
    refresh_button_ = new QPushButton("Refresh drives", this);
    full_wipe_button_ = new QPushButton("Full-device wipe", this);
    quick_zero_button_ = new QPushButton("Quick zero-fill", this);
    volume_wipe_button_ = new QPushButton("Secure erase volume", this);
    ssd_erase_button_ = new QPushButton("SSD secure erase", this);
    cancel_button_ = new QPushButton("Cancel", this);
    cancel_button_->setEnabled(false);
    drive_buttons->addWidget(refresh_button_);
    drive_buttons->addWidget(full_wipe_button_);
    drive_buttons->addWidget(quick_zero_button_);
    drive_buttons->addWidget(volume_wipe_button_);
    drive_buttons->addWidget(ssd_erase_button_);
    drive_buttons->addWidget(cancel_button_);
    erase_layout->addLayout(drive_buttons);

    auto* file_buttons = new QHBoxLayout();
    file_shred_button_ = new QPushButton("Secure erase file(s)...", this);
    folder_shred_button_ = new QPushButton("Secure erase folder...", this);
    auto* export_button = new QPushButton("Export log", this);
    auto* cert_button = new QPushButton("Export certificate", this);
    file_buttons->addWidget(file_shred_button_);
    file_buttons->addWidget(folder_shred_button_);
    file_buttons->addStretch();
    file_buttons->addWidget(cert_button);
    file_buttons->addWidget(export_button);
    erase_layout->addLayout(file_buttons);

    tabs->addTab(erase_tab, "Erase");

    auto* clone_tab = new QWidget(this);
    auto* clone_layout = new QVBoxLayout(clone_tab);

    auto* clone_notice = new QLabel(
        "Clone copies every OS-addressable byte from the selected source drive to the selected "
        "target drive. The target will be overwritten.",
        this);
    clone_notice->setWordWrap(true);
    clone_layout->addWidget(clone_notice);

    auto* clone_grid = new QGridLayout();
    clone_grid->addWidget(new QLabel("<b>Source drive</b>", this), 0, 0);
    clone_grid->addWidget(new QLabel("<b>Target drive</b>", this), 0, 1);

    clone_source_model_ = new DriveTableModel(this);
    clone_source_table_ = new QTableView(this);
    clone_source_table_->setModel(clone_source_model_);
    clone_source_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    clone_source_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    clone_source_table_->horizontalHeader()->setStretchLastSection(true);
    clone_source_table_->setAlternatingRowColors(true);
    clone_grid->addWidget(clone_source_table_, 1, 0);

    clone_target_model_ = new DriveTableModel(this);
    clone_target_table_ = new QTableView(this);
    clone_target_table_->setModel(clone_target_model_);
    clone_target_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    clone_target_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    clone_target_table_->horizontalHeader()->setStretchLastSection(true);
    clone_target_table_->setAlternatingRowColors(true);
    clone_grid->addWidget(clone_target_table_, 1, 1);
    clone_layout->addLayout(clone_grid, 1);

    auto* clone_controls = new QHBoxLayout();
    clone_verify_check_ = new QCheckBox("Verify byte-for-byte after clone", this);
    clone_verify_check_->setChecked(true);
    clone_wipe_tail_check_ = new QCheckBox("Zero-fill extra target space", this);
    clone_button_ = new QPushButton("Clone source to target", this);
    clone_cancel_button_ = new QPushButton("Cancel", this);
    clone_cancel_button_->setEnabled(false);
    clone_report_button_ = new QPushButton("Export clone report", this);
    clone_report_button_->setEnabled(false);
    clone_controls->addWidget(clone_verify_check_);
    clone_controls->addWidget(clone_wipe_tail_check_);
    clone_controls->addStretch();
    clone_controls->addWidget(clone_report_button_);
    clone_controls->addWidget(clone_button_);
    clone_controls->addWidget(clone_cancel_button_);
    clone_layout->addLayout(clone_controls);

    tabs->addTab(clone_tab, "Drive clone");

    auto* analytics_tab = new QWidget(this);
    auto* analytics_layout = new QVBoxLayout(analytics_tab);
    auto* analytics_notice = new QLabel(
        "Select a drive to inspect identity, media classification, geometry, cache, TRIM, "
        "alignment, volume, and platform health-related properties.",
        this);
    analytics_notice->setWordWrap(true);
    analytics_layout->addWidget(analytics_notice);

    analytics_model_ = new DriveTableModel(this);
    analytics_table_ = new QTableView(this);
    analytics_table_->setModel(analytics_model_);
    analytics_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    analytics_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    analytics_table_->horizontalHeader()->setStretchLastSection(true);
    analytics_table_->setAlternatingRowColors(true);
    analytics_layout->addWidget(analytics_table_, 1);

    auto* analytics_controls = new QHBoxLayout();
    analytics_button_ = new QPushButton("Analyze selected drive", this);
    analytics_export_button_ = new QPushButton("Export analytics report", this);
    analytics_export_button_->setEnabled(false);
    analytics_controls->addStretch();
    analytics_controls->addWidget(analytics_export_button_);
    analytics_controls->addWidget(analytics_button_);
    analytics_layout->addLayout(analytics_controls);

    analytics_view_ = new QTextEdit(this);
    analytics_view_->setReadOnly(true);
    analytics_view_->setMinimumHeight(220);
    analytics_view_->setPlaceholderText("Drive analytics will appear here.");
    analytics_layout->addWidget(analytics_view_, 1);

    tabs->addTab(analytics_tab, "Analytics");
    root_layout->addWidget(tabs, 1);

    status_label_ = new QLabel("Ready", this);
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    root_layout->addWidget(status_label_);
    root_layout->addWidget(progress_bar_);

    log_view_ = new QTextEdit(this);
    log_view_->setReadOnly(true);
    root_layout->addWidget(log_view_, 1);

    setCentralWidget(central);

    connect(refresh_button_, &QPushButton::clicked, this, &MainWindow::refresh_drives);
    connect(full_wipe_button_, &QPushButton::clicked, this,
            [this]() { start_drive_operation(EraseMode::FullDeviceWipe); });
    connect(quick_zero_button_, &QPushButton::clicked, this,
            [this]() { start_drive_operation(EraseMode::QuickZeroFill); });
    connect(volume_wipe_button_, &QPushButton::clicked, this,
            [this]() { start_drive_operation(EraseMode::ShredVolume); });
    connect(ssd_erase_button_, &QPushButton::clicked, this, &MainWindow::ssd_secure_erase);
    connect(clone_button_, &QPushButton::clicked, this, &MainWindow::start_clone_operation);
    connect(clone_cancel_button_, &QPushButton::clicked, this, &MainWindow::cancel_operation);
    connect(clone_report_button_, &QPushButton::clicked, this, &MainWindow::export_last_clone_report);
    connect(analytics_button_, &QPushButton::clicked, this, &MainWindow::analyze_selected_drive);
    connect(analytics_export_button_, &QPushButton::clicked, this, &MainWindow::export_analytics_report);
    connect(cancel_button_, &QPushButton::clicked, this, &MainWindow::cancel_operation);
    connect(file_shred_button_, &QPushButton::clicked, this, &MainWindow::shred_files);
    connect(folder_shred_button_, &QPushButton::clicked, this, &MainWindow::shred_folder);
    connect(export_button, &QPushButton::clicked, this, &MainWindow::export_log);
    connect(cert_button, &QPushButton::clicked, this, &MainWindow::export_last_certificate);

    if (!ensure_admin_privileges()) {
        append_log_line("WARNING: Not running with Administrator privileges. Raw device access may fail.");
    }

    refresh_drives();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (operation_running_.load()) {
        QMessageBox::warning(this, "Operation running",
                             "An erase or clone operation is still running. Cancel it and wait for "
                             "the operation to stop before closing DataScythe.");
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::ensure_admin_privileges() {
#if defined(_WIN32)
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated == TRUE;
#else
    return (geteuid() == 0);
#endif
}

QString MainWindow::format_eta(std::int64_t seconds) const {
    if (seconds < 0) {
        return "calculating...";
    }
    if (seconds < 60) {
        return QString("%1s").arg(seconds);
    }
    if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    }
    return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

void MainWindow::update_progress_ui(const EraseProgress& progress) {
    progress_bar_->setValue(static_cast<int>(progress.overall_percent));
    QString target;
    if (!progress.current_target.empty()) {
        target = QString::fromStdString(progress.current_target);
        if (target.size() > 48) {
            target = "..." + target.right(45);
        }
        target = " | " + target;
    }
    status_label_->setText(
        QString("Pass %1/%2 (%3) %4% | overall %5% | ETA %6%7")
            .arg(static_cast<qulonglong>(progress.current_pass))
            .arg(static_cast<qulonglong>(progress.total_passes))
            .arg(QString::fromStdString(progress.pass_label))
            .arg(progress.percent_complete, 0, 'f', 1)
            .arg(progress.overall_percent, 0, 'f', 1)
            .arg(format_eta(progress.eta_seconds))
            .arg(target));
}

void MainWindow::refresh_drives() {
    std::string error;
    auto enumerator = create_drive_enumerator();
    if (!enumerator) {
        QMessageBox::critical(this, "Platform error",
                              "Drive enumeration is not available on this platform.");
        return;
    }

    drives_ = enumerator->enumerate(error);
    model_->set_drives(drives_);
    clone_source_model_->set_drives(drives_);
    clone_target_model_->set_drives(drives_);
    analytics_model_->set_drives(drives_);

    if (!error.empty()) {
        append_log_line(QString::fromStdString(error));
    }
    append_log_line(QString("Detected %1 drive(s).").arg(static_cast<int>(drives_.size())));
    logger_.info("Drive refresh completed count=" + std::to_string(drives_.size()));
}

void MainWindow::fill_config_from_ui(EraseConfig& config) const {
    config.pass_count = static_cast<std::size_t>(pass_spin_->value());
    config.use_random_passes = random_check_->isChecked();
    config.final_zero_pass = zero_check_->isChecked();
    config.remove_after_shred = remove_check_->isChecked();
    config.recursive = recursive_check_->isChecked();
    config.verify_after_erase = verify_check_->isChecked();
    config.wipe_partition_metadata = partition_check_->isChecked();
    config.shred_alternate_data_streams = ads_check_->isChecked();
}

void MainWindow::maybe_export_certificate(const std::string& target, const EraseConfig& config,
                                          const EraseResult& result) {
    if (!export_cert_check_->isChecked()) {
        return;
    }

    const std::string path = default_certificate_path(target);
    const auto cert = build_certificate(target, config, result, logger_.entries());
    std::string error;
    if (!export_certificate(cert, path, error)) {
        QMessageBox::critical(this, "Certificate export failed", QString::fromStdString(error));
        return;
    }
    append_log_line("Certificate auto-saved to " + QString::fromStdString(path));
}

bool MainWindow::confirm_drive_operation(const DriveInfo& drive, EraseMode mode,
                                         EraseConfig& config_out) {
    if (drive.is_system_drive) {
        QMessageBox::critical(
            this, "Blocked",
            "Operations on the system drive are blocked to prevent destroying your Windows installation.");
        return false;
    }

    if (mode == EraseMode::ShredVolume && drive.volumes.empty()) {
        QMessageBox::warning(this, "No volumes", "The selected drive has no mounted volumes to erase.");
        return false;
    }

    config_out.mode = mode;
    fill_config_from_ui(config_out);

    ConfirmationDialog dialog(drive, mode_title(mode), mode_description(mode), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.confirmed()) {
        return false;
    }

    const auto answer = QMessageBox::critical(
        this, "Final confirmation",
        "Last chance: permanently erase " + QString::fromStdString(drive.device_path) + "?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool MainWindow::confirm_path_operation(const QString& path, const QString& phrase, EraseMode mode,
                                        const QString& title, const QString& description,
                                        EraseConfig& config_out) {
    config_out.mode = mode;
    fill_config_from_ui(config_out);

    ConfirmationDialog dialog(path, phrase, title, description, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.confirmed()) {
        return false;
    }

    const auto answer = QMessageBox::critical(
        this, "Final confirmation", "Last chance: permanently shred\n" + path + "?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool MainWindow::confirm_clone_operation(const DriveInfo& source, const DriveInfo& target) {
    if (source.device_path == target.device_path || source.physical_index == target.physical_index) {
        QMessageBox::critical(this, "Invalid clone target",
                              "Source and target drives must be different physical drives.");
        return false;
    }

    if (target.is_system_drive) {
        QMessageBox::critical(this, "Blocked",
                              "Cloning to the system drive is blocked to prevent destroying your "
                              "current operating system.");
        return false;
    }

    if (target.is_read_only) {
        QMessageBox::critical(this, "Blocked", "The selected target drive is read-only.");
        return false;
    }

    if (target.size_bytes < source.size_bytes) {
        QMessageBox::critical(this, "Target too small",
                              "The target drive must be at least as large as the source drive.");
        return false;
    }

    QString description =
        "Copies every OS-addressable byte from\n" +
        QString::fromStdString(source.device_path) +
        "\nto\n" + QString::fromStdString(target.device_path) +
        "\n\nAll existing data on the target drive will be overwritten.";
    if (target.size_bytes > source.size_bytes) {
        description += "\n\nThe target is larger than the source. Extra target space will be ";
        description += clone_wipe_tail_check_->isChecked() ? "zero-filled." : "left unchanged.";
    }
    if (source.is_system_drive) {
        description +=
            "\n\nThe source appears to be the running system drive. A live clone may not be "
            "forensically stable if the operating system writes to it during the copy.";
    }

    const QString target_label =
        QString::fromStdString(target.device_path) +
        QString("\nModel: %1\nSerial: %2")
            .arg(QString::fromStdString(target.model.empty() ? "N/A" : target.model))
            .arg(QString::fromStdString(target.serial.empty() ? "N/A" : target.serial));
    ConfirmationDialog dialog(target_label, QString("CLONE %1").arg(target.physical_index),
                              "Drive clone", description, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.confirmed()) {
        return false;
    }

    const auto answer = QMessageBox::critical(
        this, "Final confirmation",
        "Last chance: overwrite " + QString::fromStdString(target.device_path) +
            " with a byte-for-byte clone of " + QString::fromStdString(source.device_path) + "?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool MainWindow::run_preflight(const std::string& target, EraseMode mode) {
    auto checker = create_preflight_checker();
    if (!checker) {
        return true;
    }

    const PreflightResult result = checker->check(target, mode);
    const QString report = QString::fromStdString(format_preflight_report(result));

    for (const auto& issue : result.issues) {
        if (issue.severity == PreflightSeverity::Info) {
            append_log_line(QString::fromStdString(issue.message));
        }
    }

    if (!result.ok()) {
        QMessageBox::critical(this, "Pre-flight failed",
                              "Cannot proceed:\n\n" + report);
        return false;
    }

    bool has_warning = false;
    for (const auto& issue : result.issues) {
        if (issue.severity == PreflightSeverity::Warning) {
            has_warning = true;
            break;
        }
    }

    if (has_warning) {
        const auto answer =
            QMessageBox::warning(this, "Pre-flight warnings",
                                 "Warnings detected:\n\n" + report + "\nContinue anyway?",
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return answer == QMessageBox::Yes;
    }

    return true;
}

void MainWindow::run_erase_job(
    const std::string& target, const EraseConfig& config,
    const std::function<EraseResult(std::shared_ptr<EraseEngine>&)>& job) {
    last_target_ = target;
    last_config_ = config;

    auto engine = std::make_shared<EraseEngine>(create_raw_device());
    engine->set_logger(&logger_);

    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        active_engine_ = engine;
        operation_running_.store(true);
    }

    cancel_requested_.store(false);
    set_busy(true);
    progress_bar_->setValue(0);

    std::thread([this, engine, job]() {
        std::shared_ptr<EraseEngine> local_engine = engine;
        const EraseResult result = job(local_engine);

        QTimer::singleShot(0, this, [this, result]() {
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                active_engine_.reset();
                operation_running_.store(false);
            }

            last_result_ = result;

            if (result.success) {
                append_log_line(QString::fromStdString(result.message));
                status_label_->setText("Completed successfully");
                progress_bar_->setValue(100);
                if (result.error != EraseError::Cancelled) {
                    QMessageBox::information(this, "Complete",
                                             QString::fromStdString(result.message));
                    maybe_export_certificate(last_target_, last_config_, result);
                }
            } else {
                append_log_line("ERROR: " + QString::fromStdString(result.message));
                status_label_->setText(result.error == EraseError::Cancelled ? "Cancelled"
                                                                           : "Failed");
                if (result.error != EraseError::Cancelled) {
                    QMessageBox::critical(this, "Erase failed",
                                          QString::fromStdString(result.message));
                }
            }
            for (const auto& warning : result.warnings) {
                append_log_line("WARN: " + QString::fromStdString(warning));
            }
            set_busy(false);
            on_erase_finished();
        });
    }).detach();
}

void MainWindow::run_clone_job(const DriveInfo& source, const DriveInfo& target,
                               const DriveCloneConfig& config) {
    auto engine = std::make_shared<DriveCloneEngine>(create_raw_device(), create_raw_device());
    engine->set_logger(&logger_);

    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        active_clone_engine_ = engine;
        operation_running_.store(true);
    }

    cancel_requested_.store(false);
    set_busy(true);
    progress_bar_->setValue(0);

    const std::string source_path = source.device_path;
    const std::string target_path = target.device_path;
    logger_.info("Starting drive clone " + source_path + " -> " + target_path);
    status_label_->setText("Cloning " + QString::fromStdString(source_path) + " to " +
                           QString::fromStdString(target_path) + "...");

    auto progress_callback = [this](const EraseProgress& progress) -> bool {
        QTimer::singleShot(0, this, [this, progress]() { update_progress_ui(progress); });
        return !cancel_requested_.load();
    };

    std::thread([this, engine, source, target, source_path, target_path, config, progress_callback]() {
        const EraseResult result =
            engine->clone(source_path, target_path, config, progress_callback);

        QTimer::singleShot(0, this, [this, source, target, config, result]() {
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                active_clone_engine_.reset();
                operation_running_.store(false);
            }

            last_clone_report_.source = source;
            last_clone_report_.target = target;
            last_clone_report_.config = config;
            last_clone_report_.result = result;
            last_clone_report_.log_excerpt.clear();
            const auto& entries = logger_.entries();
            const std::size_t max_lines = 80;
            const std::size_t start =
                entries.size() > max_lines ? entries.size() - max_lines : 0;
            for (std::size_t i = start; i < entries.size(); ++i) {
                last_clone_report_.log_excerpt.push_back(entries[i]);
            }
            has_clone_report_ = true;
            clone_report_button_->setEnabled(true);

            if (result.success) {
                append_log_line(QString::fromStdString(result.message));
                status_label_->setText("Clone completed successfully");
                progress_bar_->setValue(100);
                QMessageBox::information(this, "Clone complete",
                                         QString::fromStdString(result.message));
            } else {
                append_log_line("ERROR: " + QString::fromStdString(result.message));
                status_label_->setText(result.error == EraseError::Cancelled ? "Cancelled"
                                                                             : "Clone failed");
                if (result.error != EraseError::Cancelled) {
                    QMessageBox::critical(this, "Clone failed",
                                          QString::fromStdString(result.message));
                }
            }
            for (const auto& warning : result.warnings) {
                append_log_line("WARN: " + QString::fromStdString(warning));
            }
            set_busy(false);
            refresh_drives();
        });
    }).detach();
}

void MainWindow::cancel_operation() {
    cancel_requested_.store(true);
    std::shared_ptr<EraseEngine> engine;
    std::shared_ptr<DriveCloneEngine> clone_engine;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        engine = active_engine_;
        clone_engine = active_clone_engine_;
    }
    if (engine) {
        engine->request_cancel();
    }
    if (clone_engine) {
        clone_engine->request_cancel();
    }
    append_log_line("Cancellation requested...");
    status_label_->setText("Cancelling...");
}

void MainWindow::start_drive_operation(EraseMode mode) {
    const auto selected = table_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Select a drive", "Select a drive from the list first.");
        return;
    }

    const DriveInfo* drive = model_->drive_at(selected.first().row());
    if (!drive) {
        return;
    }

    std::string target_path = drive->device_path;
    if (mode == EraseMode::ShredVolume && !drive->volumes.empty()) {
        target_path = drive->volumes.front().mount_point;
    }

    if (!run_preflight(target_path, mode)) {
        return;
    }

    EraseConfig config;
    if (!confirm_drive_operation(*drive, mode, config)) {
        return;
    }

    logger_.info("Starting erase on " + target_path);
    status_label_->setText("Erasing " + QString::fromStdString(target_path) + "...");

    auto progress_callback = [this](const EraseProgress& progress) -> bool {
        QTimer::singleShot(0, this, [this, progress]() { update_progress_ui(progress); });
        return !cancel_requested_.load();
    };

    run_erase_job(target_path, config,
                  [target_path, config, progress_callback](std::shared_ptr<EraseEngine>& engine) {
                      return engine->erase_target(target_path, config, progress_callback);
                  });
}

void MainWindow::start_clone_operation() {
    const auto source_selected = clone_source_table_->selectionModel()->selectedRows();
    const auto target_selected = clone_target_table_->selectionModel()->selectedRows();
    if (source_selected.isEmpty() || target_selected.isEmpty()) {
        QMessageBox::information(this, "Select drives",
                                 "Select one source drive and one target drive first.");
        return;
    }

    const DriveInfo* source = clone_source_model_->drive_at(source_selected.first().row());
    const DriveInfo* target = clone_target_model_->drive_at(target_selected.first().row());
    if (!source || !target) {
        return;
    }

    if (!ensure_admin_privileges()) {
        QMessageBox::critical(this, "Administrator privileges required",
                              "Drive cloning requires Administrator privileges for raw device "
                              "access.");
        return;
    }

    if (!confirm_clone_operation(*source, *target)) {
        return;
    }

    DriveCloneConfig config;
    config.verify_after_clone = clone_verify_check_->isChecked();
    config.wipe_target_tail = clone_wipe_tail_check_->isChecked();
    run_clone_job(*source, *target, config);
}

void MainWindow::analyze_selected_drive() {
    const auto selected = analytics_table_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Select a drive", "Select a drive from the analytics table first.");
        return;
    }

    const DriveInfo* drive = analytics_model_->drive_at(selected.first().row());
    if (!drive) {
        return;
    }

    auto analyzer = create_drive_analyzer();
    if (!analyzer) {
        QMessageBox::critical(this, "Unavailable",
                              "Drive analytics are not available on this platform.");
        return;
    }

    const DriveAnalysis analysis = analyzer->analyze(*drive);

    QString report;
    report += "Drive Analytics\n";
    report += "===============\n\n";
    report += "Health summary: " + QString::fromStdString(analysis.health_summary) + "\n";
    report += "SSD / non-rotational: " + QString(analysis.is_ssd ? "Yes" : "No") + "\n\n";

    if (!analysis.warnings.empty()) {
        report += "Warnings\n";
        report += "--------\n";
        for (const auto& warning : analysis.warnings) {
            report += "- " + QString::fromStdString(warning) + "\n";
        }
        report += "\n";
    }

    std::string current_category;
    for (const auto& field : analysis.fields) {
        if (field.category != current_category) {
            if (!current_category.empty()) {
                report += "\n";
            }
            current_category = field.category;
            report += QString::fromStdString(current_category) + "\n";
            report += QString(current_category.size(), '-') + "\n";
        }
        report += QString::fromStdString(field.name) + ": " +
                  QString::fromStdString(field.value) + "\n";
    }

    analytics_view_->setPlainText(report);
    analytics_export_button_->setEnabled(true);
    append_log_line("Analyzed drive " + QString::fromStdString(drive->device_path));
}

void MainWindow::shred_files() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Select files to shred", QString(), "All files (*.*)");
    if (files.isEmpty()) {
        return;
    }

    const QString summary = files.size() == 1 ? files.first() : QString("%1 files").arg(files.size());
    if (!run_preflight(files.first().toStdString(), EraseMode::ShredFiles)) {
        return;
    }

    EraseConfig config;
    if (!confirm_path_operation(summary, "SHRED FILES", EraseMode::ShredFiles,
                                mode_title(EraseMode::ShredFiles),
                                mode_description(EraseMode::ShredFiles), config)) {
        return;
    }

    std::vector<std::string> paths;
    paths.reserve(static_cast<std::size_t>(files.size()));
    for (const auto& file : files) {
        paths.push_back(file.toStdString());
    }

    logger_.info("Starting file shred count=" + std::to_string(paths.size()));

    auto progress_callback = [this](const EraseProgress& progress) -> bool {
        QTimer::singleShot(0, this, [this, progress]() { update_progress_ui(progress); });
        return !cancel_requested_.load();
    };

    const std::string target = paths.size() == 1 ? paths.front()
                                                 : std::to_string(paths.size()) + " files";
    run_erase_job(target, config,
                  [paths, config, progress_callback](std::shared_ptr<EraseEngine>& engine) {
                      return engine->erase_paths(paths, config, progress_callback);
                  });
}

void MainWindow::shred_folder() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Select folder to shred");
    if (folder.isEmpty()) {
        return;
    }

    if (!run_preflight(folder.toStdString(), EraseMode::ShredDirectory)) {
        return;
    }

    EraseConfig config;
    if (!confirm_path_operation(folder, "SHRED FOLDER", EraseMode::ShredDirectory,
                                mode_title(EraseMode::ShredDirectory),
                                mode_description(EraseMode::ShredDirectory), config)) {
        return;
    }

    logger_.info("Starting folder shred " + folder.toStdString());

    auto progress_callback = [this](const EraseProgress& progress) -> bool {
        QTimer::singleShot(0, this, [this, progress]() { update_progress_ui(progress); });
        return !cancel_requested_.load();
    };

    const std::string target = folder.toStdString();
    run_erase_job(target, config,
                  [target, config, progress_callback](std::shared_ptr<EraseEngine>& engine) {
                      return engine->erase_paths({target}, config, progress_callback);
                  });
}

void MainWindow::ssd_secure_erase() {
    const auto selected = table_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Select a drive", "Select a drive from the list first.");
        return;
    }

    const DriveInfo* drive = model_->drive_at(selected.first().row());
    if (!drive) {
        return;
    }

    if (drive->is_system_drive) {
        QMessageBox::critical(this, "Blocked", "Cannot secure-erase the system drive.");
        return;
    }

    auto secure = create_secure_erase();
    if (!secure) {
        QMessageBox::critical(this, "Unavailable", "SSD secure erase is not available on this platform.");
        return;
    }

    std::string support_reason;
    if (!secure->is_supported(drive->device_path, support_reason)) {
        QMessageBox::warning(this, "Not supported", QString::fromStdString(support_reason));
        return;
    }

    if (!run_preflight(drive->device_path, EraseMode::SsdSecureErase)) {
        return;
    }

    EraseConfig config;
    config.mode = EraseMode::SsdSecureErase;
    if (!confirm_drive_operation(*drive, EraseMode::SsdSecureErase, config)) {
        return;
    }

    logger_.info("Starting SSD secure erase on " + drive->device_path);
    status_label_->setText("Issuing hardware secure erase...");

    run_erase_job(drive->device_path, config,
                  [drive_path = drive->device_path, config, this](std::shared_ptr<EraseEngine>&) {
                      auto secure_erase = create_secure_erase();
                      auto progress = [this](int percent, const std::string& status) -> bool {
                          QTimer::singleShot(0, this, [this, percent, status]() {
                              progress_bar_->setValue(percent);
                              status_label_->setText(QString::fromStdString(status));
                          });
                          return !cancel_requested_.load();
                      };
                      EraseResult result = secure_erase->execute(drive_path, progress);
                      if (result.success) {
                          logger_.info(result.message);
                          if (config.verify_after_erase) {
                              if (!verify_target_zeroed(drive_path, result)) {
                                  result.success = false;
                                  result.error = EraseError::IoError;
                                  result.message = "SSD secure erase verification failed";
                                  logger_.error(result.message);
                              } else {
                                  logger_.info("SSD secure erase verification passed (" +
                                               std::to_string(result.verification_samples) +
                                               " samples)");
                              }
                          }
                      } else {
                          logger_.error(result.message);
                      }
                      return result;
                  });
}

void MainWindow::on_erase_finished() {
    refresh_drives();
}

void MainWindow::export_last_certificate() {
    if (!last_result_.success) {
        QMessageBox::information(this, "No certificate",
                                 "No completed erase operation to certify. Run an erase first.");
        return;
    }

    const QString default_name =
        "datascythe-certificate-" +
        QString::fromStdString(last_target_).replace(":", "_").replace("\\", "_") + ".txt";
    const QString path = QFileDialog::getSaveFileName(this, "Save erasure certificate", default_name,
                                                      "Text files (*.txt)");
    if (path.isEmpty()) {
        return;
    }

    const auto cert = build_certificate(last_target_, last_config_, last_result_, logger_.entries());
    std::string error;
    if (!export_certificate(cert, path.toStdString(), error)) {
        QMessageBox::critical(this, "Certificate export failed", QString::fromStdString(error));
        return;
    }
    append_log_line("Certificate exported to " + path);
}

void MainWindow::export_last_clone_report() {
    if (!has_clone_report_) {
        QMessageBox::information(this, "No clone report",
                                 "No clone report is available yet. Run a clone first.");
        return;
    }

    const QString default_name = QString::fromStdString(default_clone_report_path(
        last_clone_report_.source.device_path, last_clone_report_.target.device_path));
    const QString path = QFileDialog::getSaveFileName(this, "Save clone report", default_name,
                                                      "Text files (*.txt)");
    if (path.isEmpty()) {
        return;
    }

    std::string error;
    if (!export_clone_report(last_clone_report_, path.toStdString(), error)) {
        QMessageBox::critical(this, "Clone report export failed", QString::fromStdString(error));
        return;
    }
    append_log_line("Clone report exported to " + path);
}

void MainWindow::export_analytics_report() {
    const QString text = analytics_view_->toPlainText();
    if (text.trimmed().isEmpty()) {
        QMessageBox::information(this, "No analytics report",
                                 "No analytics report is available yet. Analyze a drive first.");
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, "Save analytics report", "datascythe-analytics-report.txt", "Text files (*.txt)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Analytics export failed",
                              "Unable to write analytics report to " + path);
        return;
    }
    QTextStream out(&file);
    out << text;
    append_log_line("Analytics report exported to " + path);
}

void MainWindow::export_log() {
    const QString path =
        QFileDialog::getSaveFileName(this, "Export log", "datascythe-log.txt", "Text files (*.txt)");
    if (path.isEmpty()) {
        return;
    }
    std::string error;
    if (!logger_.export_to_file(path.toStdString(), error)) {
        QMessageBox::critical(this, "Export failed", QString::fromStdString(error));
        return;
    }
    append_log_line("Log exported to " + path);
}

void MainWindow::append_log_line(const QString& line) {
    log_view_->append(line);
    logger_.info(line.toStdString());
}

void MainWindow::set_busy(bool busy) {
    refresh_button_->setEnabled(!busy);
    full_wipe_button_->setEnabled(!busy);
    quick_zero_button_->setEnabled(!busy);
    volume_wipe_button_->setEnabled(!busy);
    file_shred_button_->setEnabled(!busy);
    folder_shred_button_->setEnabled(!busy);
    ssd_erase_button_->setEnabled(!busy);
    clone_button_->setEnabled(!busy);
    clone_report_button_->setEnabled(!busy && has_clone_report_);
    analytics_button_->setEnabled(!busy);
    analytics_export_button_->setEnabled(!busy && !analytics_view_->toPlainText().trimmed().isEmpty());
    cancel_button_->setEnabled(busy);
    clone_cancel_button_->setEnabled(busy);
}

}  
