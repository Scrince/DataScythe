#pragma once

#include "platform/drive_info.h"

#include <QAbstractTableModel>
#include <vector>

namespace datascythe::gui {

class DriveTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        Index = 0,
        Model,
        Serial,
        Size,
        Type,
        Volumes,
        System,
        ColumnCount
    };

    explicit DriveTableModel(QObject* parent = nullptr);

    void set_drives(const std::vector<DriveInfo>& drives);
    const DriveInfo* drive_at(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<DriveInfo> drives_;
};

}  