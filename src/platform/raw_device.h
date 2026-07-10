#pragma once

#include "core/erase_result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace datascythe {

using ProgressCallback = std::function<bool(const EraseProgress&)>;

enum class RawTargetType {
    Unknown,
    RegularFile,
    BlockDevice,
    Volume,
};


class IRawDevice {
public:
    virtual ~IRawDevice() = default;

    virtual bool open(const std::string& path, std::string& error_out) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    virtual RawTargetType target_type() const = 0;
    virtual std::uint64_t size_bytes(std::string& error_out) const = 0;
    virtual std::uint64_t block_size(std::string& error_out) const = 0;
    virtual bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                          std::string& error_out) = 0;
    virtual bool read_at(std::uint64_t offset, void* data, std::size_t size,
                         std::string& error_out) = 0;
    virtual bool flush(std::string& error_out) = 0;

    
    virtual bool dismount_volumes(std::string& error_out) = 0;

    
    virtual bool remove_target(std::string& error_out) = 0;
};

std::unique_ptr<IRawDevice> create_raw_device();

}  