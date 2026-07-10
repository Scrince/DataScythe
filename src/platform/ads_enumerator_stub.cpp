#include "platform/ads_enumerator.h"

#if !defined(DATASCYTHE_PLATFORM_WINDOWS)

#include <vector>

namespace datascythe {

std::vector<std::string> enumerate_alternate_data_streams(const std::string& file_path) {
    (void)file_path;
    return {};
}

}  

#endif