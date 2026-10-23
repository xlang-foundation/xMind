#include "agentflow/json_schema.hpp"
#if defined(_WIN32)
#include "agentflow/schema_worker_service.hpp"
#include <windows.h>
#endif
int run_schema_worker(int argc,char**) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=1)return 2;
    try{return agentflow::run_schema_worker_host([](const std::string& text,bool present,const std::string& instance){
        using namespace agentflow;
        try{JsonSchema schema(text);if(present)schema.validate_object(instance);return SchemaWorkerVerdict::valid;}
        catch(const SchemaInvalid&){return SchemaWorkerVerdict::schema_invalid;}
        catch(const SchemaArgumentsInvalid&){return SchemaWorkerVerdict::arguments_invalid;}
    });}catch(...){return 2;}
#else
    return 2;
#endif
}
#if !defined(XMIND_UNIFIED_EXECUTABLE)
int main(int argc,char** argv){return run_schema_worker(argc,argv);}
#endif
