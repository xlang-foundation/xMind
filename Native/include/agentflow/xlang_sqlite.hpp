#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace agentflow {
using SqlBytes=std::vector<std::uint8_t>;
using SqlValue=std::variant<std::nullptr_t,std::int64_t,double,std::string,SqlBytes>;
struct SqlResult {
    std::vector<std::vector<SqlValue>> rows;
    std::int64_t affected_rows=-1;
    std::optional<std::int64_t> last_insert_id;
};
// Own on the persistence thread. All values crossing this boundary are copies.
// Uses the embedded xlang3 SQLite interface, never the SQLite C API.
class XlangSqlite {
public:
    XlangSqlite(const std::string& database, const std::vector<std::string>& import_roots);
    ~XlangSqlite();
    XlangSqlite(const XlangSqlite&)=delete;
    XlangSqlite& operator=(const XlangSqlite&)=delete;
    SqlResult execute(const std::string& sql, const std::vector<SqlValue>& parameters={});
    void begin();
    void commit();
    void rollback();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
