#include "drive_table_model.h"

#include <QBrush>
#include <QColor>

namespace datascythe::gui {

namespace {

QString format_bytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 2) + " " + units[unit];
}

QString drive_type_label(DriveType type) {
    switch (type) {
        case DriveType::HDD:
            return "HDD";
        case DriveType::SSD:
            return "SSD";
        case DriveType::Removable:
            return "Removable";
        case DriveType::Virtual:
            return "Virtual";
        default:
            return "Unknown";
    }
}

}  // namespace

DriveTableModel::DriveTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void DriveTableModel::set_drives(const std::vector<DriveInfo>& drives) {
    beginResetModel();
    drives_ = drives;
    endResetModel();
}

const DriveInfo* DriveTableModel::drive_at(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= drives_.size()) {
        return nullptr;
    }
    return &drives_[static_cast<std::size_t>(row)];
}

int DriveTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(drives_.size());
}

int DriveTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant DriveTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || static_cast<std::size_t>(index.row()) >= drives_.size()) {
        return {};
    }

    const DriveInfo& drive = drives_[static_cast<std::size_t>(index.row())];

    if (role == Qt::ForegroundRole && drive.is_system_drive) {
        return QBrush(QColor(180, 40, 40));
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
        case Index:
            return drive.physical_index;
        case Model:
            return QString::fromStdString(drive.model.empty() ? drive.device_path : drive.model);
        case Serial:
            return QString::fromStdString(drive.serial.empty() ? "N/A" : drive.serial);
        case Size:
            return format_bytes(drive.size_bytes);
        case Type:
            return drive_type_label(drive.type);
        case Volumes: {
            QStringList parts;
            for (const auto& vol : drive.volumes) {
                parts << QString::fromStdString(vol.mount_point);
            }
            return parts.join(", ");
        }
        case System:
            return drive.is_system_drive ? "YES - DO NOT WIPE" : "No";
        default:
            return {};
    }
}

QVariant DriveTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
        case Index:
            return "Drive #";
        case Model:
            return "Model / Path";
        case Serial:
            return "Serial";
        case Size:
            return "Size";
        case Type:
            return "Type";
        case Volumes:
            return "Volumes";
        case System:
            return "System Drive";
        default:
            return {};
    }
}

}  // namespace datascythe::gui