#include "sql.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#ifdef NOQERI_WITH_SQLITE
#include <sqlite3.h>
#endif
#ifdef NOQERI_WITH_POSTGRES
#include <libpq-fe.h>
#endif

namespace noe {
namespace {
constexpr std::uintmax_t kMaxScriptBytes = 1024u * 1024u;
std::string tabSafe(const char* data, std::size_t count) {
    std::string s;
    s.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const char c = data[i];
        if (c == '\t') s += "\\t";
        else if (c == '\n') s += "\\n";
        else if (c == '\r') s += "\\r";
        else if (c == '\0') s += "\\0";
        else s += c;
    }
    return s;
}
#ifdef NOQERI_WITH_SQLITE
struct SQLiteClose { void operator()(sqlite3* db) const { if (db) sqlite3_close(db); } };
struct StmtClose { void operator()(sqlite3_stmt* s) const { if (s) sqlite3_finalize(s); } };
bool sqliteScript(const std::string& target, const std::string& source,
                  std::ostream& output, std::ostream& errors) {
    sqlite3* raw = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int openResult = sqlite3_open_v2(target.c_str(), &raw, flags, nullptr);
    std::unique_ptr<sqlite3, SQLiteClose> db(raw);
    if (openResult != SQLITE_OK) {
        errors << "NoqeriSQL SQLite open failed: " << (raw ? sqlite3_errmsg(raw) : "out of memory") << '\n';
        return false;
    }
    sqlite3_busy_timeout(db.get(), 5000);
    sqlite3_limit(db.get(), SQLITE_LIMIT_SQL_LENGTH, static_cast<int>(kMaxScriptBytes));
    sqlite3_limit(db.get(), SQLITE_LIMIT_LENGTH, 1024 * 1024);
    sqlite3_limit(db.get(), SQLITE_LIMIT_COLUMN, 256);
    sqlite3_db_config(db.get(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    if (sqlite3_exec(db.get(), "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        errors << "NoqeriSQL SQLite safety PRAGMA failed: " << sqlite3_errmsg(db.get()) << '\n';
        return false;
    }
    // Entire script is one transaction; explicit BEGIN/COMMIT is unsupported.
    if (sqlite3_exec(db.get(), "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        errors << "NoqeriSQL SQLite transaction begin failed: " << sqlite3_errmsg(db.get()) << '\n';
        return false;
    }
    bool ok = true;
    const char* cursor = source.c_str();
    const char* end = cursor + source.size();
    std::ostringstream stagedOutput;
    while (cursor < end && ok) {
        sqlite3_stmt* rawStmt = nullptr;
        const char* tail = nullptr;
        const int len = static_cast<int>(end - cursor);
        const int rc = sqlite3_prepare_v2(db.get(), cursor, len, &rawStmt, &tail);
        std::unique_ptr<sqlite3_stmt, StmtClose> stmt(rawStmt);
        if (rc != SQLITE_OK || tail == cursor) {
            errors << "NoqeriSQL SQLite invalid SQL: " << sqlite3_errmsg(db.get()) << '\n';
            ok = false;
            break;
        }
        cursor = tail;
        if (!stmt) continue;
        if (sqlite3_bind_parameter_count(stmt.get()) != 0) {
            errors << "NoqeriSQL: unbound SQL parameters in script; use a prepared statement API\n";
            ok = false;
            break;
        }
        const int columns = sqlite3_column_count(stmt.get());
        if (columns) {
            for (int c = 0; c < columns; ++c) {
                if (c) stagedOutput << '\t';
                const char* name = sqlite3_column_name(stmt.get(), c);
                stagedOutput << tabSafe(name, std::char_traits<char>::length(name));
            }
            stagedOutput << '\n';
        }
        for (;;) {
            const int step = sqlite3_step(stmt.get());
            if (step == SQLITE_DONE) break;
            if (step != SQLITE_ROW) {
                errors << "NoqeriSQL SQLite statement failed: " << sqlite3_errmsg(db.get()) << '\n';
                ok = false;
                break;
            }
            for (int c = 0; c < columns; ++c) {
                if (c) stagedOutput << '\t';
                const int kind = sqlite3_column_type(stmt.get(), c);
                if (kind == SQLITE_NULL) { stagedOutput << "NULL"; continue; }
                if (kind == SQLITE_BLOB) {
                    stagedOutput << "[BLOB " << sqlite3_column_bytes(stmt.get(), c) << " bytes]";
                    continue;
                }
                const unsigned char* value = sqlite3_column_text(stmt.get(), c);
                const int count = sqlite3_column_bytes(stmt.get(), c);
                if (value && count >= 0) stagedOutput << tabSafe(reinterpret_cast<const char*>(value), static_cast<std::size_t>(count));
            }
            stagedOutput << '\n';
        }
    }
    if (ok && sqlite3_exec(db.get(), "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) {
        output << stagedOutput.str();
        return true;
    }
    if (ok) errors << "NoqeriSQL SQLite commit failed: " << sqlite3_errmsg(db.get()) << '\n';
    sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}
#endif
#ifdef NOQERI_WITH_POSTGRES
struct PGClose { void operator()(PGconn* c) const { if (c) PQfinish(c); } };
struct PGResultClose { void operator()(PGresult* r) const { if (r) PQclear(r); } };
bool postgresScript(const std::string& target, const std::string& source,
                    std::ostream& output, std::ostream& errors) {
    // Later parameters override user-supplied TLS and timeout settings.
    const char* keywords[] = {"dbname", "sslmode", "connect_timeout", nullptr};
    const char* values[] = {target.c_str(), "verify-full", "10", nullptr};
    std::unique_ptr<PGconn, PGClose> conn(PQconnectdbParams(keywords, values, 1));
    if (!conn || PQstatus(conn.get()) != CONNECTION_OK) {
        errors << "NoqeriSQL PostgreSQL connection failed: " << (conn ? PQerrorMessage(conn.get()) : "out of memory") << '\n';
        return false;
    }
    std::unique_ptr<PGresult, PGResultClose> start(PQexec(conn.get(), "BEGIN"));
    if (!start || PQresultStatus(start.get()) != PGRES_COMMAND_OK) {
        errors << "NoqeriSQL PostgreSQL could not begin transaction: " << PQerrorMessage(conn.get());
        return false;
    }
    bool ok = true;
    std::ostringstream stagedOutput;
    if (PQsendQuery(conn.get(), source.c_str()) != 1) {
        errors << "NoqeriSQL PostgreSQL query send failed: " << PQerrorMessage(conn.get());
        ok = false;
    }
    if (ok) {
        for (;;) {
            std::unique_ptr<PGresult, PGResultClose> result(PQgetResult(conn.get()));
            if (!result) break;
            const ExecStatusType status = PQresultStatus(result.get());
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK && status != PGRES_EMPTY_QUERY) {
                errors << "NoqeriSQL PostgreSQL query failed: " << PQresultErrorMessage(result.get());
                ok = false;
                continue;
            }
            if (status != PGRES_TUPLES_OK || !ok) continue;
            const int columns = PQnfields(result.get());
            for (int c = 0; c < columns; ++c) {
                if (c) stagedOutput << '\t';
                const char* name = PQfname(result.get(), c);
                stagedOutput << tabSafe(name, std::char_traits<char>::length(name));
            }
            stagedOutput << '\n';
            for (int row = 0; row < PQntuples(result.get()); ++row) {
                for (int c = 0; c < columns; ++c) {
                    if (c) stagedOutput << '\t';
                    if (PQgetisnull(result.get(), row, c)) stagedOutput << "NULL";
                    else stagedOutput << tabSafe(PQgetvalue(result.get(), row, c), static_cast<std::size_t>(PQgetlength(result.get(), row, c)));
                }
                stagedOutput << '\n';
            }
        }
    }
    std::unique_ptr<PGresult, PGResultClose> final(PQexec(conn.get(), ok ? "COMMIT" : "ROLLBACK"));
    if (!final || PQresultStatus(final.get()) != PGRES_COMMAND_OK) {
        errors << "NoqeriSQL PostgreSQL transaction finalization failed: " << PQerrorMessage(conn.get());
        return false;
    }
    if (ok && std::string(PQcmdStatus(final.get())) != "COMMIT") {
        errors << "NoqeriSQL PostgreSQL transaction rolled back\n";
        return false;
    }
    if (ok) output << stagedOutput.str();
    return ok;
}
#endif
} // namespace

bool executeNoqeriSql(const std::string& backend, const std::string& target,
                      const std::filesystem::path& script,
                      std::ostream& output, std::ostream& errors) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec) || ec) {
        errors << "NoqeriSQL: script is missing or is not a regular file\n";
        return false;
    }
    const auto size = std::filesystem::file_size(script, ec);
    if (ec || size == 0 || size > kMaxScriptBytes) {
        errors << "NoqeriSQL: SQL script must be between 1 byte and 1 MiB\n";
        return false;
    }
    std::ifstream input(script, std::ios::binary);
    std::string source(size, '\0');
    if (!input.read(&source[0], static_cast<std::streamsize>(size))) {
        errors << "NoqeriSQL: failed to read complete SQL script\n";
        return false;
    }
    if (source.find('\0') != std::string::npos) {
        errors << "NoqeriSQL: NUL byte is not permitted in SQL scripts\n";
        return false;
    }
    if (target.empty()) {
        errors << "NoqeriSQL: database path or connection string is required\n";
        return false;
    }
    if (backend == "sqlite") {
#ifdef NOQERI_WITH_SQLITE
        return sqliteScript(target, source, output, errors);
#else
        errors << "NoqeriSQL: SQLite support is not built (install SQLite3 development libraries)\n";
        return false;
#endif
    }
    if (backend == "postgres") {
#ifdef NOQERI_WITH_POSTGRES
        return postgresScript(target, source, output, errors);
#else
        errors << "NoqeriSQL: PostgreSQL support is not built (install libpq development libraries)\n";
        return false;
#endif
    }
    errors << "NoqeriSQL: unknown backend; choose sqlite or postgres\n";
    return false;
}
} // namespace noe
