# CMake honors SOURCE_DATE_EPOCH here for reproducible release builds.
string(TIMESTAMP SWMMVIS_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)
get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
# file(CONFIGURE) preserves the mtime when the timestamp is unchanged.
file(CONFIGURE OUTPUT "${OUTPUT_FILE}" CONTENT [=[
#include "core/buildinfo.h"

namespace openswmmvis {
const char *buildTimestamp() noexcept
{
    return "@SWMMVIS_BUILD_TIMESTAMP@";
}
} // namespace openswmmvis
]=] @ONLY)
