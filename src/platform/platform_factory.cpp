#include "core/preflight.h"
#include "platform/drive_analyzer.h"
#include "platform/drive_enumerator.h"
#include "platform/raw_device.h"
#include "platform/secure_erase.h"
#include "platform/volume_manager.h"

#if !defined(DATASCYTHE_PLATFORM_WINDOWS) && !defined(DATASCYTHE_PLATFORM_LINUX) && \
    !defined(DATASCYTHE_PLATFORM_MACOS)

namespace datascythe {

std::unique_ptr<IDriveEnumerator> create_drive_enumerator() { return nullptr; }
std::unique_ptr<IDriveAnalyzer> create_drive_analyzer() { return nullptr; }
std::unique_ptr<IRawDevice> create_raw_device() { return nullptr; }
std::unique_ptr<IVolumeManager> create_volume_manager() { return nullptr; }
std::unique_ptr<ISecureErase> create_secure_erase() { return nullptr; }
std::unique_ptr<IPreflightChecker> create_preflight_checker() { return nullptr; }

}  

#endif
