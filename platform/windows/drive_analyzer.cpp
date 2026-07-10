#include "platform/drive_analyzer.h"

#if defined(DATASCYTHE_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace datascythe {

namespace {

std::string bool_text(bool value) {
    return value ? "Yes" : "No";
}

std::string bytes_text(std::uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit]
        << " (" << bytes << " bytes)";
    return out.str();
}

std::string trim_copy(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string drive_type_text(DriveType type) {
    switch (type) {
        case DriveType::HDD:
            return "HDD";
        case DriveType::SSD:
            return "SSD";
        case DriveType::Removable:
            return "Removable";
        case DriveType::Virtual:
            return "Virtual";
        case DriveType::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::string bus_type_text(STORAGE_BUS_TYPE type) {
    switch (type) {
        case BusTypeScsi:
            return "SCSI";
        case BusTypeAtapi:
            return "ATAPI";
        case BusTypeAta:
            return "ATA";
        case BusType1394:
            return "IEEE 1394";
        case BusTypeSsa:
            return "SSA";
        case BusTypeFibre:
            return "Fibre Channel";
        case BusTypeUsb:
            return "USB";
        case BusTypeRAID:
            return "RAID";
        case BusTypeiScsi:
            return "iSCSI";
        case BusTypeSas:
            return "SAS";
        case BusTypeSata:
            return "SATA";
        case BusTypeSd:
            return "SD";
        case BusTypeMmc:
            return "MMC";
        case BusTypeVirtual:
            return "Virtual";
        case BusTypeFileBackedVirtual:
            return "File-backed virtual";
        case BusTypeSpaces:
            return "Storage Spaces";
        case BusTypeNvme:
            return "NVMe";
        default:
            return "Unknown";
    }
}

std::string media_type_text(MEDIA_TYPE type) {
    switch (type) {
        case FixedMedia:
            return "Fixed media";
        case RemovableMedia:
            return "Removable media";
        default:
            return "Other/unknown";
    }
}

std::string win_error(DWORD code) {
    return "Win32 error " + std::to_string(code);
}

void add(DriveAnalysis& analysis, const std::string& category, const std::string& name,
         const std::string& value) {
    analysis.fields.push_back({category, name, value.empty() ? "N/A" : value});
}

template <typename T>
bool query_property(HANDLE handle, STORAGE_PROPERTY_ID property, T& out, std::string& error_out) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = property;
    query.QueryType = PropertyStandardQuery;

    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &out,
                         sizeof(out), &returned, nullptr)) {
        error_out = win_error(GetLastError());
        return false;
    }
    return true;
}

bool query_descriptor(HANDLE handle, STORAGE_PROPERTY_ID property, std::vector<std::uint8_t>& out,
                      std::string& error_out) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = property;
    query.QueryType = PropertyStandardQuery;

    STORAGE_DESCRIPTOR_HEADER header{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header,
                         sizeof(header), &returned, nullptr)) {
        error_out = win_error(GetLastError());
        return false;
    }

    const DWORD size = std::max<DWORD>(header.Size, sizeof(STORAGE_DESCRIPTOR_HEADER));
    out.assign(size, 0);
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), out.data(),
                         static_cast<DWORD>(out.size()), &returned, nullptr)) {
        error_out = win_error(GetLastError());
        return false;
    }
    return true;
}

std::string descriptor_string(const std::vector<std::uint8_t>& buffer, DWORD offset) {
    if (offset == 0 || offset >= buffer.size()) {
        return {};
    }
    const auto begin = buffer.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, buffer.end(), 0);
    return trim_copy(std::string(begin, end));
}

std::string cache_type_text(WRITE_CACHE_TYPE type) {
    switch (type) {
        case WriteCacheTypeNone:
            return "None";
        case WriteCacheTypeWriteBack:
            return "Write-back";
        case WriteCacheTypeWriteThrough:
            return "Write-through";
        default:
            return "Unknown";
    }
}

std::string cache_enabled_text(WRITE_CACHE_ENABLE value) {
    switch (value) {
        case WriteCacheEnableUnknown:
            return "Unknown";
        case WriteCacheDisabled:
            return "Disabled";
        case WriteCacheEnabled:
            return "Enabled";
        default:
            return "Unknown";
    }
}

std::string cache_change_text(WRITE_CACHE_CHANGE value) {
    switch (value) {
        case WriteCacheChangeUnknown:
            return "Unknown";
        case WriteCacheNotChangeable:
            return "Not changeable";
        case WriteCacheChangeable:
            return "Changeable";
        default:
            return "Unknown";
    }
}

class WindowsDriveAnalyzer final : public IDriveAnalyzer {
public:
    DriveAnalysis analyze(const DriveInfo& drive) override {
        DriveAnalysis analysis;
        analysis.drive = drive;
        analysis.is_ssd = drive.type == DriveType::SSD;
        analysis.health_summary =
            "Basic device properties collected. Vendor SMART/NVMe health logs are not exposed "
            "through this generic analytics path.";

        add_basic_fields(analysis, drive);

        HANDLE handle = CreateFileA(drive.device_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            analysis.warnings.push_back("Unable to open device for analytics: " +
                                        win_error(GetLastError()) +
                                        ". Run as Administrator for full data.");
            add_classification_fields(analysis, drive);
            return analysis;
        }

        add_storage_descriptor(analysis, handle);
        add_geometry(analysis, handle);
        add_adapter(analysis, handle);
        add_alignment(analysis, handle);
        add_seek_penalty(analysis, handle);
        add_trim(analysis, handle);
        add_write_cache(analysis, handle);

        CloseHandle(handle);
        add_classification_fields(analysis, drive);

        if (analysis.is_ssd) {
            analysis.health_summary =
                "Drive is reported as SSD/non-rotational. No critical health failure was reported "
                "by generic Windows storage properties, but SMART/NVMe lifetime counters are not "
                "available in this view.";
        } else if (drive.type == DriveType::HDD) {
            analysis.health_summary =
                "Drive is reported as rotational media. Generic Windows storage properties do not "
                "include full SMART health in this view.";
        }

        return analysis;
    }

private:
    void add_basic_fields(DriveAnalysis& analysis, const DriveInfo& drive) {
        add(analysis, "Identity", "Device path", drive.device_path);
        add(analysis, "Identity", "Physical index", std::to_string(drive.physical_index));
        add(analysis, "Identity", "Model", drive.model);
        add(analysis, "Identity", "Vendor", drive.vendor);
        add(analysis, "Identity", "Serial", drive.serial);
        add(analysis, "Capacity", "Size", bytes_text(drive.size_bytes));
        add(analysis, "Safety", "System drive", bool_text(drive.is_system_drive));
        add(analysis, "Safety", "Removable", bool_text(drive.is_removable));
        add(analysis, "Safety", "Read-only", bool_text(drive.is_read_only));

        if (drive.volumes.empty()) {
            add(analysis, "Volumes", "Mounted volumes", "None detected");
        } else {
            for (const auto& volume : drive.volumes) {
                std::ostringstream value;
                value << volume.mount_point << " filesystem=" << volume.filesystem
                      << " size=" << bytes_text(volume.size_bytes)
                      << " system=" << bool_text(volume.is_system_volume);
                add(analysis, "Volumes", "Volume", value.str());
            }
        }
    }

    void add_classification_fields(DriveAnalysis& analysis, const DriveInfo& drive) {
        add(analysis, "Classification", "Enumerated type", drive_type_text(drive.type));
        add(analysis, "Classification", "SSD/non-rotational", bool_text(analysis.is_ssd));
    }

    void add_storage_descriptor(DriveAnalysis& analysis, HANDLE handle) {
        std::string error;
        std::vector<std::uint8_t> buffer;
        if (!query_descriptor(handle, StorageDeviceProperty, buffer, error)) {
            analysis.warnings.push_back("Storage device descriptor unavailable: " + error);
            return;
        }
        if (buffer.size() < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            analysis.warnings.push_back("Storage device descriptor was shorter than expected.");
            return;
        }

        const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        add(analysis, "Device descriptor", "Vendor ID",
            descriptor_string(buffer, desc->VendorIdOffset));
        add(analysis, "Device descriptor", "Product ID",
            descriptor_string(buffer, desc->ProductIdOffset));
        add(analysis, "Device descriptor", "Product revision",
            descriptor_string(buffer, desc->ProductRevisionOffset));
        add(analysis, "Device descriptor", "Serial number",
            descriptor_string(buffer, desc->SerialNumberOffset));
        add(analysis, "Device descriptor", "Bus type", bus_type_text(desc->BusType));
        add(analysis, "Device descriptor", "Removable media",
            bool_text(desc->RemovableMedia != 0));
        add(analysis, "Device descriptor", "Command queueing",
            bool_text(desc->CommandQueueing != 0));
        if (desc->BusType == BusTypeNvme || desc->BusType == BusTypeSd ||
            desc->BusType == BusTypeMmc) {
            analysis.is_ssd = true;
        }
    }

    void add_geometry(DriveAnalysis& analysis, HANDLE handle) {
        DISK_GEOMETRY_EX geometry{};
        DWORD returned = 0;
        if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geometry,
                            sizeof(geometry), &returned, nullptr)) {
            add(analysis, "Geometry", "Disk size", bytes_text(geometry.DiskSize.QuadPart));
            add(analysis, "Geometry", "Bytes per sector",
                std::to_string(geometry.Geometry.BytesPerSector));
            add(analysis, "Geometry", "Sectors per track",
                std::to_string(geometry.Geometry.SectorsPerTrack));
            add(analysis, "Geometry", "Tracks per cylinder",
                std::to_string(geometry.Geometry.TracksPerCylinder));
            add(analysis, "Geometry", "Cylinders",
                std::to_string(geometry.Geometry.Cylinders.QuadPart));
            add(analysis, "Geometry", "Media type", media_type_text(geometry.Geometry.MediaType));
            return;
        }

        DISK_GEOMETRY legacy{};
        if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &legacy,
                            sizeof(legacy), &returned, nullptr)) {
            add(analysis, "Geometry", "Bytes per sector",
                std::to_string(legacy.BytesPerSector));
            add(analysis, "Geometry", "Sectors per track",
                std::to_string(legacy.SectorsPerTrack));
            add(analysis, "Geometry", "Tracks per cylinder",
                std::to_string(legacy.TracksPerCylinder));
            add(analysis, "Geometry", "Cylinders", std::to_string(legacy.Cylinders.QuadPart));
            add(analysis, "Geometry", "Media type", media_type_text(legacy.MediaType));
        } else {
            analysis.warnings.push_back("Disk geometry unavailable: " + win_error(GetLastError()));
        }
    }

    void add_adapter(DriveAnalysis& analysis, HANDLE handle) {
        STORAGE_ADAPTER_DESCRIPTOR adapter{};
        adapter.Version = sizeof(adapter);
        std::string error;
        if (!query_property(handle, StorageAdapterProperty, adapter, error)) {
            analysis.warnings.push_back("Storage adapter descriptor unavailable: " + error);
            return;
        }

        add(analysis, "Adapter", "Maximum transfer length",
            std::to_string(adapter.MaximumTransferLength));
        add(analysis, "Adapter", "Maximum physical pages",
            std::to_string(adapter.MaximumPhysicalPages));
        add(analysis, "Adapter", "Alignment mask", std::to_string(adapter.AlignmentMask));
        add(analysis, "Adapter", "Adapter uses PIO", bool_text(adapter.AdapterUsesPio != 0));
        add(analysis, "Adapter", "Command queueing", bool_text(adapter.CommandQueueing != 0));
        add(analysis, "Adapter", "Accelerated transfer",
            bool_text(adapter.AcceleratedTransfer != 0));
        add(analysis, "Adapter", "Bus type",
            bus_type_text(static_cast<STORAGE_BUS_TYPE>(adapter.BusType)));
        add(analysis, "Adapter", "Bus major version", std::to_string(adapter.BusMajorVersion));
        add(analysis, "Adapter", "Bus minor version", std::to_string(adapter.BusMinorVersion));
    }

    void add_alignment(DriveAnalysis& analysis, HANDLE handle) {
        STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignment{};
        alignment.Version = sizeof(alignment);
        std::string error;
        if (!query_property(handle, StorageAccessAlignmentProperty, alignment, error)) {
            analysis.warnings.push_back("Access alignment descriptor unavailable: " + error);
            return;
        }
        add(analysis, "Alignment", "Bytes per cache line",
            std::to_string(alignment.BytesPerCacheLine));
        add(analysis, "Alignment", "Bytes offset for cache alignment",
            std::to_string(alignment.BytesOffsetForCacheAlignment));
        add(analysis, "Alignment", "Bytes per logical sector",
            std::to_string(alignment.BytesPerLogicalSector));
        add(analysis, "Alignment", "Bytes per physical sector",
            std::to_string(alignment.BytesPerPhysicalSector));
        add(analysis, "Alignment", "Bytes offset for sector alignment",
            std::to_string(alignment.BytesOffsetForSectorAlignment));
    }

    void add_seek_penalty(DriveAnalysis& analysis, HANDLE handle) {
        DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
        seek.Version = sizeof(seek);
        std::string error;
        if (!query_property(handle, StorageDeviceSeekPenaltyProperty, seek, error)) {
            analysis.warnings.push_back("Seek penalty descriptor unavailable: " + error);
            return;
        }
        const bool has_seek_penalty = seek.IncursSeekPenalty != FALSE;
        add(analysis, "Media behavior", "Incurs seek penalty", bool_text(has_seek_penalty));
        add(analysis, "Media behavior", "Likely SSD", bool_text(!has_seek_penalty));
        if (!has_seek_penalty) {
            analysis.is_ssd = true;
        }
    }

    void add_trim(DriveAnalysis& analysis, HANDLE handle) {
        DEVICE_TRIM_DESCRIPTOR trim{};
        trim.Version = sizeof(trim);
        std::string error;
        if (!query_property(handle, StorageDeviceTrimProperty, trim, error)) {
            analysis.warnings.push_back("TRIM descriptor unavailable: " + error);
            return;
        }
        add(analysis, "Media behavior", "TRIM/UNMAP enabled", bool_text(trim.TrimEnabled != FALSE));
        if (trim.TrimEnabled != FALSE) {
            analysis.is_ssd = true;
        }
    }

    void add_write_cache(DriveAnalysis& analysis, HANDLE handle) {
        STORAGE_WRITE_CACHE_PROPERTY cache{};
        cache.Version = sizeof(cache);
        std::string error;
        if (!query_property(handle, StorageDeviceWriteCacheProperty, cache, error)) {
            analysis.warnings.push_back("Write cache descriptor unavailable: " + error);
            return;
        }
        add(analysis, "Write cache", "Type", cache_type_text(cache.WriteCacheType));
        add(analysis, "Write cache", "Enabled", cache_enabled_text(cache.WriteCacheEnabled));
        add(analysis, "Write cache", "Changeable", cache_change_text(cache.WriteCacheChangeable));
        add(analysis, "Write cache", "Write-through supported",
            bool_text(cache.WriteThroughSupported != FALSE));
        add(analysis, "Write cache", "Flush cache supported",
            bool_text(cache.FlushCacheSupported != FALSE));
        add(analysis, "Write cache", "User defined power protection",
            bool_text(cache.UserDefinedPowerProtection != FALSE));
    }
};

}  

std::unique_ptr<IDriveAnalyzer> create_drive_analyzer() {
    return std::make_unique<WindowsDriveAnalyzer>();
}

}  

#endif
