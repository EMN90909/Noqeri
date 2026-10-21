#pragma once
#include "diagnostics.hpp"
#include <filesystem>
#include <iosfwd>

namespace noe {

// DEPRECATED: legacy NoqeriDB .nqd scripts and NQDB1 .nqdb snapshots only.
// This is retained for explicit migration/compatibility testing. It is NOT
// the native NoeSQL .noedb provider; new applications use the NoeSQL ABI.
class NoqeriDatabase {
public:
    bool execute(const std::filesystem::path& script,
                 const std::filesystem::path& databaseOverride,
                 std::ostream& output,
                 Diagnostics& diagnostics) const;
};

} // namespace noe
