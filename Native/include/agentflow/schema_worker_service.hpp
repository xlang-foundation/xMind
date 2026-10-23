#pragma once
#include "xlang3/xlang3.h"
#include <functional>
#include <string>
namespace agentflow {
enum class SchemaWorkerVerdict : std::int64_t {valid=0,schema_invalid=1,arguments_invalid=2};
using SchemaWorkerEvaluator=std::function<SchemaWorkerVerdict(const std::string&,bool,const std::string&)>;
int run_schema_worker_host(SchemaWorkerEvaluator);
struct SchemasApi {
    X::Value Open(std::string capability);
    BEGIN_PACKAGE(SchemasApi)
        APISET().AddFunc<1>("Open",&SchemasApi::Open);
    END_PACKAGE
};
}
