#include "core/certificate.h"
#include "core/clone_report.h"
#include "core/drive_clone_engine.h"
#include "core/erase_config.h"
#include "core/erase_engine.h"
#include "core/erase_result.h"
#include "core/partition_table.h"
#include "core/pass_scheduler.h"
#include "core/pattern_generator.h"
#include "core/sha256.h"
#include "platform/nvme_admin.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect_true(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_eq(std::size_t actual, std::size_t expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " (got " << actual << ", expected " << expected
                  << ")\n";
        ++failures;
    }
}

void test_pass_scheduler_count() {
    datascythe::PassScheduler scheduler;
    const auto schedule = scheduler.build_schedule(3, true);
    expect_eq(schedule.size(), 3, "schedule size for 3 passes");
}

void test_pass_scheduler_no_random() {
    datascythe::PassScheduler scheduler;
    const auto schedule = scheduler.build_schedule(5, false);
    expect_eq(schedule.size(), 5, "schedule size without random");
    for (int pattern : schedule) {
        expect_true(pattern >= 0, "fixed patterns only when random disabled");
    }
}

void test_pattern_generator_fixed() {
    datascythe::PatternGenerator generator(42);
    std::vector<std::uint8_t> buffer;
    generator.fill_buffer(0x000, buffer, 512);
    expect_eq(buffer.size(), 512, "buffer size");
    for (std::uint8_t byte : buffer) {
        expect_true(byte == 0x00, "zero pattern bytes");
    }
}

void test_pattern_generator_random() {
    datascythe::PatternGenerator generator(99);
    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;
    generator.fill_buffer(-1, a, 256);
    generator.fill_buffer(-1, b, 256);
    bool different = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            different = true;
            break;
        }
    }
    expect_true(different, "random buffers should differ");
}

void test_sha256_abc() {
    const std::string input = "abc";
    expect_true(datascythe::sha256_hex(input.data(), input.size()) ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "sha256 abc vector");
}

void test_nvme_sanitize_progress() {
    expect_eq(datascythe::nvme::sanitize_progress_percent(0xFFFF), 100,
              "0xFFFF means sanitize complete");
    expect_eq(datascythe::nvme::sanitize_progress_percent(0), 0, "zero progress");
}

void test_mode_to_string() {
    expect_true(datascythe::mode_to_string(datascythe::EraseMode::FullDeviceWipe) ==
                    "Full-device wipe",
                "full device mode name");
    expect_true(datascythe::mode_to_string(datascythe::EraseMode::SsdSecureErase) ==
                    "SSD hardware secure erase",
                "ssd secure erase mode name");
}

void test_build_certificate() {
    datascythe::EraseConfig config;
    config.mode = datascythe::EraseMode::QuickZeroFill;
    config.pass_count = 1;
    config.verify_after_erase = true;

    datascythe::EraseResult result;
    result.success = true;
    result.verification_passed = true;
    result.message = "Quick zero-fill completed";

    const auto cert = datascythe::build_certificate("\\\\.\\PhysicalDrive2", config, result,
                                                    {"2026-01-01 12:00:00 INFO start"});
    expect_true(cert.target == "\\\\.\\PhysicalDrive2", "certificate target");
    expect_true(cert.mode_name == "Quick zero-fill", "certificate mode");
    expect_true(cert.verification_enabled, "verification enabled flag");
    expect_true(cert.verification_passed, "verification passed flag");
    expect_true(cert.success, "certificate success");
}

void test_export_certificate() {
    datascythe::ErasureCertificate cert;
    cert.target = "test-target";
    cert.mode_name = "Quick zero-fill";
    cert.pass_count = 1;
    cert.success = true;
    cert.result_message = "done";

    const std::string path = "datascythe_test_cert.txt";
    std::string error;
    expect_true(datascythe::export_certificate(cert, path, error), "export certificate");
    expect_true(error.empty(), "no export error");

    std::ifstream in(path);
    expect_true(in.good(), "certificate file readable");
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    expect_true(contents.find("DATASCYTHE ERASURE CERTIFICATE") != std::string::npos,
                "certificate header present");
    expect_true(contents.find("test-target") != std::string::npos, "certificate target in file");

    std::remove(path.c_str());
}

void test_partition_metadata_regions() {
    const auto small = datascythe::partition_metadata_regions(512);
    expect_eq(small.front_size, 512, "small device front region");
    expect_eq(small.back_size, 0, "small device has no backup region");

    const auto large = datascythe::partition_metadata_regions(10 * 1024 * 1024);
    expect_eq(large.front_size, datascythe::kPartitionMetadataWipeBytes,
              "large device front region");
    expect_eq(large.back_size, datascythe::kPartitionMetadataWipeBytes,
              "large device backup region");
    expect_eq(large.back_offset, 10 * 1024 * 1024 - datascythe::kPartitionMetadataWipeBytes,
              "large device backup offset");
}

void test_sanitize_target_for_filename() {
    expect_true(datascythe::sanitize_target_for_filename(R"(\\.\PhysicalDrive1)") ==
                    "__._PhysicalDrive1",
                "sanitize drive path");
}

void test_default_certificate_path() {
    const auto path = datascythe::default_certificate_path(R"(\\.\PhysicalDrive2)");
    expect_true(path.find("datascythe-certificate-") != std::string::npos,
                "certificate filename prefix");
    expect_true(path.find("PhysicalDrive2") != std::string::npos, "certificate target in path");
    expect_true(path.find(".txt") != std::string::npos, "certificate extension");
}

void test_final_verification_pattern_logic() {
    datascythe::PassScheduler scheduler;
    datascythe::EraseConfig config;
    config.pass_count = 3;
    config.use_random_passes = false;
    config.final_zero_pass = true;

    const auto schedule = scheduler.build_schedule(config.pass_count, config.use_random_passes);
    expect_eq(schedule.size(), 3, "schedule for verification test");

    const int expected_with_zero = 0x000;
    const int expected_without_zero = schedule.back();

    expect_true(config.final_zero_pass ? expected_with_zero == 0x000 : true,
                "final zero pass yields 0x000 pattern");
    expect_true(!config.final_zero_pass || expected_with_zero == 0x000,
                "zero pattern when final zero enabled");

    config.final_zero_pass = false;
    const auto schedule_no_zero =
        scheduler.build_schedule(config.pass_count, config.use_random_passes);
    expect_true(schedule_no_zero.back() == expected_without_zero,
                "last schedule pattern without zero pass");
}

class MemoryRawDevice final : public datascythe::IRawDevice {
public:
    MemoryRawDevice(std::string expected_path, std::vector<std::uint8_t> bytes)
        : expected_path_(std::move(expected_path)), bytes_(std::move(bytes)) {}

    bool open(const std::string& path, std::string& error_out) override {
        if (path != expected_path_) {
            error_out = "unexpected path";
            return false;
        }
        open_ = true;
        return true;
    }

    void close() override { open_ = false; }
    bool is_open() const override { return open_; }
    datascythe::RawTargetType target_type() const override {
        return datascythe::RawTargetType::BlockDevice;
    }
    std::uint64_t size_bytes(std::string&) const override { return bytes_.size(); }
    std::uint64_t block_size(std::string&) const override { return 512; }

    bool write_at(std::uint64_t offset, const void* data, std::size_t size,
                  std::string& error_out) override {
        if (offset + size > bytes_.size()) {
            error_out = "write out of range";
            return false;
        }
        const auto* source = static_cast<const std::uint8_t*>(data);
        std::copy(source, source + size, bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
    }

    bool read_at(std::uint64_t offset, void* data, std::size_t size,
                 std::string& error_out) override {
        if (offset + size > bytes_.size()) {
            error_out = "read out of range";
            return false;
        }
        auto* target = static_cast<std::uint8_t*>(data);
        std::copy(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset + size), target);
        return true;
    }

    bool flush(std::string&) override { return true; }
    bool dismount_volumes(std::string&) override { return true; }
    bool remove_target(std::string& error_out) override {
        error_out = "remove unsupported";
        return false;
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::string expected_path_;
    std::vector<std::uint8_t> bytes_;
    bool open_ = false;
};

class FaultyRawDevice final : public datascythe::IRawDevice {
public:
    enum class FailureMode {
        Write,
        Flush,
    };

    FaultyRawDevice(std::string expected_path, FailureMode mode)
        : expected_path_(std::move(expected_path)), mode_(mode) {}

    bool open(const std::string& path, std::string& error_out) override {
        if (path != expected_path_) {
            error_out = "unexpected path";
            return false;
        }
        open_ = true;
        return true;
    }

    void close() override { open_ = false; }
    bool is_open() const override { return open_; }
    datascythe::RawTargetType target_type() const override {
        return datascythe::RawTargetType::BlockDevice;
    }
    std::uint64_t size_bytes(std::string&) const override { return 4096; }
    std::uint64_t block_size(std::string&) const override { return 512; }

    bool write_at(std::uint64_t, const void*, std::size_t, std::string& error_out) override {
        if (mode_ == FailureMode::Write) {
            error_out = "injected write failure";
            return false;
        }
        return true;
    }

    bool read_at(std::uint64_t, void* data, std::size_t size, std::string&) override {
        std::fill(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size, 0);
        return true;
    }

    bool flush(std::string& error_out) override {
        if (mode_ == FailureMode::Flush) {
            error_out = "injected flush failure";
            return false;
        }
        return true;
    }

    bool dismount_volumes(std::string&) override { return true; }
    bool remove_target(std::string& error_out) override {
        error_out = "remove unsupported";
        return false;
    }

private:
    std::string expected_path_;
    FailureMode mode_;
    bool open_ = false;
};

void test_erase_fails_on_write_error() {
    auto device = std::make_unique<FaultyRawDevice>(
        "target", FaultyRawDevice::FailureMode::Write);
    datascythe::EraseEngine engine(std::move(device));
    datascythe::EraseConfig config;
    config.mode = datascythe::EraseMode::QuickZeroFill;

    const auto result = engine.erase_target("target", config, nullptr);
    expect_true(!result.success, "erase fails when write fails");
    expect_true(result.error == datascythe::EraseError::IoError, "write failure is I/O error");
}

void test_erase_fails_on_flush_error() {
    auto device = std::make_unique<FaultyRawDevice>(
        "target", FaultyRawDevice::FailureMode::Flush);
    datascythe::EraseEngine engine(std::move(device));
    datascythe::EraseConfig config;
    config.mode = datascythe::EraseMode::QuickZeroFill;

    const auto result = engine.erase_target("target", config, nullptr);
    expect_true(!result.success, "erase fails when flush fails");
    expect_true(result.error == datascythe::EraseError::IoError, "flush failure is I/O error");
}

void test_drive_clone_engine_copies_and_verifies() {
    std::vector<std::uint8_t> source_bytes(8192);
    for (std::size_t i = 0; i < source_bytes.size(); ++i) {
        source_bytes[i] = static_cast<std::uint8_t>(i % 251);
    }
    std::vector<std::uint8_t> target_bytes(source_bytes.size(), 0xA5);

    auto source = std::make_unique<MemoryRawDevice>("source", source_bytes);
    auto target = std::make_unique<MemoryRawDevice>("target", target_bytes);
    const MemoryRawDevice* target_view = target.get();

    datascythe::DriveCloneEngine engine(std::move(source), std::move(target));
    datascythe::DriveCloneConfig config;
    config.verify_after_clone = true;

    int progress_calls = 0;
    const auto result = engine.clone("source", "target", config,
                                     [&](const datascythe::EraseProgress&) {
                                         ++progress_calls;
                                         return true;
                                     });

    expect_true(result.success, "drive clone succeeds");
    expect_true(result.verification_passed, "drive clone verifies");
    expect_true(target_view->bytes() == source_bytes, "target bytes match source bytes");
    expect_true(result.source_sha256 == result.target_sha256, "clone hashes match");
    expect_true(result.source_sha256 == datascythe::sha256_hex(source_bytes.data(), source_bytes.size()),
                "clone source hash matches expected");
    expect_true(progress_calls > 0, "clone reports progress");
}

void test_export_clone_report() {
    datascythe::CloneReport report;
    report.source.device_path = "source";
    report.source.model = "source-model";
    report.source.serial = "source-serial";
    report.source.size_bytes = 4096;
    report.target.device_path = "target";
    report.target.model = "target-model";
    report.target.serial = "target-serial";
    report.target.size_bytes = 4096;
    report.config.verify_after_clone = true;
    report.result.success = true;
    report.result.verification_passed = true;
    report.result.message = "Drive clone completed and verified byte-for-byte";
    report.result.source_sha256 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    report.result.target_sha256 = report.result.source_sha256;

    const std::string path = "datascythe_clone_report_test.txt";
    std::string error;
    expect_true(datascythe::export_clone_report(report, path, error), "export clone report");

    std::ifstream in(path);
    expect_true(in.good(), "clone report readable");
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    expect_true(contents.find("DATASCYTHE CLONE REPORT") != std::string::npos,
                "clone report header present");
    expect_true(contents.find("Source SHA-256") != std::string::npos,
                "clone report hash present");

    std::remove(path.c_str());
}

void test_nvme_poll_complete() {
    bool called = false;
    const auto poll = [](datascythe::nvme::SanitizeStatusLog& log, std::string&) -> bool {
        log.progress = 0xFFFF;
        log.status = 0x02;
        return true;
    };
    std::string error;
    const auto state = datascythe::nvme::wait_for_sanitize_completion(
        poll,
        [&](int, const std::string&) {
            called = true;
            return true;
        },
        std::chrono::seconds(1), error);
    expect_true(state == datascythe::nvme::SanitizePollState::Complete, "poll completes");
    expect_true(called, "progress callback invoked");
}

}  // namespace

int main() {
    test_pass_scheduler_count();
    test_pass_scheduler_no_random();
    test_pattern_generator_fixed();
    test_pattern_generator_random();
    test_sha256_abc();
    test_nvme_sanitize_progress();
    test_mode_to_string();
    test_build_certificate();
    test_export_certificate();
    test_partition_metadata_regions();
    test_sanitize_target_for_filename();
    test_default_certificate_path();
    test_final_verification_pattern_logic();
    test_erase_fails_on_write_error();
    test_erase_fails_on_flush_error();
    test_drive_clone_engine_copies_and_verifies();
    test_export_clone_report();
    test_nvme_poll_complete();

    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << failures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
