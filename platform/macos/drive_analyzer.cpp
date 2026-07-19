#include "platform/drive_analyzer.h"

#if defined(DATASCYTHE_PLATFORM_MACOS)

#include <array>
#include <memory>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace datascythe {

namespace {

void add(DriveAnalysis& analysis, const std::string& category, const std::string& name,
         const std::string& value) {
    analysis.fields.push_back({category, name, value.empty() ? "N/A" : value});
}

std::string bool_text(bool value) {
    return value ? "Yes" : "No";
}

bool is_safe_diskutil_path(const std::string& device_path) {
    // Only allow absolute /dev/disk* or /dev/rdisk* paths without shell metacharacters.
    if (device_path.rfind("/dev/disk", 0) != 0 && device_path.rfind("/dev/rdisk", 0) != 0) {
        return false;
    }
    for (const char ch : device_path) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '/' || ch == '_' || ch == '-' ||
                        ch == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string diskutil_info(const std::string& device_path) {
    if (!is_safe_diskutil_path(device_path)) {
        return {};
    }

    // argv array — never build a shell command string.
    std::vector<std::string> args = {"/usr/sbin/diskutil", "info", device_path};
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {};
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {};
    }
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    pid_t pid = 0;
    const int spawn_rc =
        posix_spawn(&pid, args[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (spawn_rc != 0) {
        close(pipefd[0]);
        return {};
    }

    std::ostringstream out;
    std::array<char, 512> buffer{};
    ssize_t n = 0;
    while ((n = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
        out.write(buffer.data(), n);
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return {};
    }
    return out.str();
}

class MacDriveAnalyzer final : public IDriveAnalyzer {
public:
    DriveAnalysis analyze(const DriveInfo& drive) override {
        DriveAnalysis analysis;
        analysis.drive = drive;
        analysis.is_ssd = drive.type == DriveType::SSD;
        analysis.health_summary =
            "macOS diskutil information collected when available. Full SMART/NVMe health logs "
            "require platform-specific tooling outside this generic view.";

        add(analysis, "Identity", "Device path", drive.device_path);
        add(analysis, "Identity", "Physical index", std::to_string(drive.physical_index));
        add(analysis, "Identity", "Model", drive.model);
        add(analysis, "Identity", "Serial", drive.serial);
        add(analysis, "Capacity", "Size bytes", std::to_string(drive.size_bytes));
        add(analysis, "Classification", "SSD/non-rotational", bool_text(analysis.is_ssd));
        add(analysis, "Safety", "System drive", bool_text(drive.is_system_drive));
        add(analysis, "Safety", "Removable", bool_text(drive.is_removable));
        add(analysis, "Safety", "Read-only", bool_text(drive.is_read_only));

        const std::string info = diskutil_info(drive.device_path);
        if (info.empty()) {
            analysis.warnings.push_back("diskutil info was unavailable for " + drive.device_path);
        } else {
            add(analysis, "diskutil", "Raw info", info);
        }
        return analysis;
    }
};

}  

std::unique_ptr<IDriveAnalyzer> create_drive_analyzer() {
    return std::make_unique<MacDriveAnalyzer>();
}

}  

#endif
