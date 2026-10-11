#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agentflow {

struct PtySessionMetadata {
    std::string id;
    std::string session_id;
    std::string title;
    std::uint16_t rows=24;
    std::uint16_t columns=80;
    bool running=false;
    std::uint32_t exit_code=0;
    std::uint64_t output_sequence=0;
};

struct PtyOutputChunk {
    std::string data;
    std::uint64_t next_sequence=0;
    bool truncated=false;
    bool running=false;
    std::uint32_t exit_code=0;
};

// Local, backend-owned interactive shells. A terminal is launched only from
// the fixed shell and workspace passed by the native server; callers cannot
// choose an executable, arguments, environment, or directory.
class PtySessions {
public:
    PtySessions(std::string workspace_root,std::string shell_executable);
    ~PtySessions();
    PtySessions(const PtySessions&)=delete;
    PtySessions& operator=(const PtySessions&)=delete;

    PtySessionMetadata create(const std::string& session_id,const std::string& title,
        std::uint16_t rows,std::uint16_t columns);
    std::vector<PtySessionMetadata> list(const std::string& session_id) const;
    PtySessionMetadata get(const std::string& id) const;
    PtyOutputChunk read(const std::string& id,std::uint64_t after_sequence,
        std::size_t limit,std::chrono::milliseconds wait);
    void write(const std::string& id,const std::string& utf8);
    PtySessionMetadata resize(const std::string& id,std::uint16_t rows,std::uint16_t columns);
    void close(const std::string& id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
