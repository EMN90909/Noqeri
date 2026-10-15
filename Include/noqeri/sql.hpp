#pragma once
#include <filesystem>
#include <iosfwd>
#include <string>

namespace noe {
// Execute a bounded, trusted SQL file using a real SQLite or PostgreSQL engine.
// Never interpolate untrusted values into SQL; use prepared binding APIs instead.
bool executeNoqeriSql(const std::string& backend, const std::string& target,
                      const std::filesystem::path& script,
                      std::ostream& output, std::ostream& errors);
} // namespace noe
