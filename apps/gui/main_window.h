#pragma once

#include "core/erase_config.h"
#include "core/clone_report.h"
#include "core/drive_clone_engine.h"
#include "core/erase_engine.h"
#include "core/logger.h"
#include "platform/drive_info.h"

#include <QMainWindow>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableView;
class QTextEdit;

namespace datascythe::gui {

class DriveTableModel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refresh_drives();
    void start_drive_operation(EraseMode mode);
    void shred_files();
    void shred_folder();
    void ssd_secure_erase();
    void start_clone_operation();
    void analyze_selected_drive();
    void cancel_operation();
    void export_log();
    void export_last_certificate();
    void export_last_clone_report();
    void export_analytics_report();
    void on_erase_finished();

private:
    bool ensure_admin_privileges();
    bool run_preflight(const std::string& target, EraseMode mode);
    bool confirm_drive_operation(const DriveInfo& drive, EraseMode mode, EraseConfig& config_out);
    bool confirm_path_operation(const QString& path, const QString& phrase, EraseMode mode,
                                const QString& title, const QString& description,
                                EraseConfig& config_out);
    bool confirm_clone_operation(const DriveInfo& source, const DriveInfo& target);
    void run_erase_job(const std::string& target, const EraseConfig& config,
                       const std::function<EraseResult(std::shared_ptr<EraseEngine>&)>& job);
    void run_clone_job(const DriveInfo& source, const DriveInfo& target,
                       const DriveCloneConfig& config);
    void append_log_line(const QString& line);
    void set_busy(bool busy);
    QString format_eta(std::int64_t seconds) const;
    void update_progress_ui(const EraseProgress& progress);
    void fill_config_from_ui(EraseConfig& config) const;
    void maybe_export_certificate(const std::string& target, const EraseConfig& config,
                                  const EraseResult& result);

    DriveTableModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    DriveTableModel* clone_source_model_ = nullptr;
    DriveTableModel* clone_target_model_ = nullptr;
    DriveTableModel* analytics_model_ = nullptr;
    QTableView* clone_source_table_ = nullptr;
    QTableView* clone_target_table_ = nullptr;
    QTableView* analytics_table_ = nullptr;
    QSpinBox* pass_spin_ = nullptr;
    QCheckBox* random_check_ = nullptr;
    QCheckBox* zero_check_ = nullptr;
    QCheckBox* remove_check_ = nullptr;
    QCheckBox* recursive_check_ = nullptr;
    QCheckBox* verify_check_ = nullptr;
    QCheckBox* partition_check_ = nullptr;
    QCheckBox* ads_check_ = nullptr;
    QCheckBox* export_cert_check_ = nullptr;
    QCheckBox* clone_verify_check_ = nullptr;
    QCheckBox* clone_wipe_tail_check_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTextEdit* log_view_ = nullptr;
    QTextEdit* analytics_view_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QPushButton* full_wipe_button_ = nullptr;
    QPushButton* quick_zero_button_ = nullptr;
    QPushButton* volume_wipe_button_ = nullptr;
    QPushButton* file_shred_button_ = nullptr;
    QPushButton* folder_shred_button_ = nullptr;
    QPushButton* ssd_erase_button_ = nullptr;
    QPushButton* clone_button_ = nullptr;
    QPushButton* clone_cancel_button_ = nullptr;
    QPushButton* clone_report_button_ = nullptr;
    QPushButton* analytics_button_ = nullptr;
    QPushButton* analytics_export_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;

    Logger logger_;
    std::vector<DriveInfo> drives_;

    std::mutex job_mutex_;
    std::shared_ptr<EraseEngine> active_engine_;
    std::shared_ptr<DriveCloneEngine> active_clone_engine_;
    std::atomic<bool> operation_running_{false};
    std::atomic<bool> cancel_requested_{false};

    std::string last_target_;
    EraseConfig last_config_{};
    EraseResult last_result_{};
    CloneReport last_clone_report_{};
    bool has_clone_report_ = false;
};

}  
