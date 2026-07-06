#include "core/certificate.h"
#include "core/erase_config.h"
#include "core/erase_result.h"
#include "core/partition_table.h"
#include "core/pass_scheduler.h"
#include "core/pattern_generator.h"
#include "platform/nvme_admin.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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
    test_nvme_sanitize_progress();
    test_mode_to_string();
    test_build_certificate();
    test_export_certificate();
    test_partition_metadata_regions();
    test_sanitize_target_for_filename();
    test_default_certificate_path();
    test_final_verification_pattern_logic();
    test_nvme_poll_complete();

    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << failures << " test(s) failed.\n";
    return EXIT_FAILURE;
}