#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <algorithm>
#include <thread>
#include <mutex>
#include <optional>
namespace agentflow {
namespace {
constexpr std::uint64_t chunk_limit=65536,document_limit=8*1024*1024;
}
struct NativeWorkerModelClient::Impl {
    X::Runtime runtime;
    X::Value root,worker,model;
    std::mutex mutex;
    Impl(const NativeWorkerModelAccess& access,const std::string& root_id,const std::string& owner,const std::string& workspace,const std::string& generation,std::int64_t revision){
        if(!access.endpoint.starts_with("lrpc:")||access.endpoint.size()>32||access.ticket.size()!=64||access.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos||!access.pid)throw Conflict("Invalid native worker model access");
        X3LrpcEndpointInfo info{};info.size=sizeof(info);if(x3_runtime_probe_remote(runtime.get(),access.endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=access.pid)throw Conflict("Native model endpoint identity differs");
        root=runtime.ImportRemote("xmind",access.endpoint.c_str());auto services=call(root,"Workers",{});worker=call(services,"Open",{text(access.ticket)});const auto identity=call(worker,"Identity",{});
        if(identity["root"].ToString()!=root_id||identity["owner"].ToString()!=owner||identity["workspace"].ToString()!=workspace||identity["generation"].ToString()!=generation||!identity["revision"].IsInt64()||identity["revision"].ToInt64()!=revision)throw Conflict("Native model object differs from captured worker");model=call(worker,"Model",{});
    }
    X::Value text(const std::string& bytes){return X::Value(runtime,bytes);}
    X::Value number(std::uint64_t n){return X::Value(runtime,n);}
    X::Value call(const X::Value& object,const char* method,std::vector<X::Value> args){X::Value result;if(!object[method].Call(args,result))throw AgentWorkerModelOutcomeUnrecorded("Native SDK model acknowledgement unavailable; no replay authorized");return result;}
    std::string read(const X::Value& operation,const char* method,std::uint64_t size,std::optional<std::uint64_t> event={}){
        if(size>document_limit)throw AgentWorkerModelOutcomeUnrecorded("Native model document exceeds its captured bound");std::string bytes;bytes.reserve(size);
        for(std::uint64_t offset=0;offset<size;){const auto count=std::min(chunk_limit,size-offset);std::vector<X::Value> args;if(event)args.push_back(number(*event));args.push_back(number(offset));args.push_back(number(count));auto page=call(operation,method,std::move(args));std::uint64_t actual=0;const auto* data=page.BytesData(&actual);if(!page.IsBin()||actual!=count||(!data&&actual))throw AgentWorkerModelOutcomeUnrecorded("Native model byte page differs");bytes.append(static_cast<const char*>(data),actual);offset+=actual;}return bytes;
    }
    std::uint64_t integer(const X::Value& value){if((!value.IsUInt64()&&!value.IsInt64())||(value.IsInt64()&&value.ToInt64()<0))throw AgentWorkerModelOutcomeUnrecorded("Native model count has invalid type");return value.ToUInt64();}
    std::string await(const X::Value& operation,ChatCompletionStream::Sink sink,std::stop_token cancel){
        try{std::uint64_t ordinal=0;X::Value snapshot;for(;;){if(cancel.stop_requested())throw TransportCancelled("Native worker model observation cancelled");snapshot=call(operation,"Status",{});if(!snapshot.IsDict()||snapshot.Size()!=6)throw AgentWorkerModelOutcomeUnrecorded("Native model status differs");const auto sizes=snapshot["events"],done=snapshot["done"];if(!sizes.IsList()||done.raw().tag!=X3_TAG_BOOL||sizes.Size()<ordinal||sizes.Size()>8192)throw AgentWorkerModelOutcomeUnrecorded("Native model event order differs");const auto count=sizes.Size();for(;ordinal<count;++ordinal){const auto size=integer(sizes[ordinal]);auto event=decode_agent_model_event(read(operation,"ReadEvent",size,ordinal));if(sink)sink(event);}if(done.ToInt64()!=0)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            const auto error=snapshot["error"];if(!error.IsString())throw AgentWorkerModelOutcomeUnrecorded("Native model failure has invalid type");const auto code=error.ToString();if(code=="rejected")throw Conflict("Native model rejected captured invocation");if(code=="cancelled")throw TransportCancelled("Native model operation cancelled");if(code=="timeout")throw TransportTimeout("Native model operation deadline elapsed");if(code=="provider_http"){const auto status=snapshot["provider_status"],detail=snapshot["provider_code"];if(!status.IsInt64()||status.ToInt64()<100||status.ToInt64()>599||!detail.IsString()||(!detail.ToString().empty()&&(status.ToInt64()!=400||detail.ToString()!="context_length_exceeded")))throw AgentWorkerModelOutcomeUnrecorded("Native provider failure facts differ");ProviderHttpError actual(static_cast<int>(status.ToInt64()));actual.code=detail.ToString();throw actual;}if(code=="model_protocol")throw ModelProtocolError("Native model protocol failed");if(!code.empty())throw AgentWorkerModelOutcomeUnrecorded("Native model outcome is unrecorded; no replay authorized");return read(operation,"ReadResult",integer(snapshot["result_size"]));
        }catch(...){try{call(operation,"Cancel",{});}catch(...){}throw;}
    }
    X::Value upload(const ModelRequest& request){const auto bytes=encode_agent_model_request(request);if(bytes.size()>document_limit)throw Conflict("Native model request exceeds bound");auto input=call(model,"NewRequest",{});try{for(std::size_t offset=0;offset<bytes.size();offset+=chunk_limit){const auto count=std::min<std::size_t>(chunk_limit,bytes.size()-offset);auto page=X::Value::Bytes(runtime.host(),bytes.data()+offset,count);call(input,"Append",{page});}call(input,"Seal",{});return input;}catch(...){try{call(input,"Release",{});}catch(...){}throw;}}
};
NativeWorkerModelClient::NativeWorkerModelClient(NativeWorkerModelAccess access,std::string root,std::string owner,std::string workspace,std::string generation,std::int64_t revision):impl_(std::make_unique<Impl>(access,root,owner,workspace,generation,revision)){}
NativeWorkerModelClient::~NativeWorkerModelClient()=default;
AgentWorkerModelAdmission NativeWorkerModelClient::admission(std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model admission cancelled");return decode_agent_worker_model_admission(impl_->await(impl_->call(impl_->model,"Admission",{}),{},cancel));}
ModelRequest NativeWorkerModelClient::prepare_request(std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model prepare cancelled");return decode_agent_model_request(impl_->await(impl_->call(impl_->model,"Prepare",{}),{},cancel));}
ModelRequest NativeWorkerModelClient::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model rebuild cancelled");auto input=impl_->upload(request);return decode_agent_model_request(impl_->await(impl_->call(impl_->model,"Rebuild",{impl_->text(attempt),impl_->call(input,"Id",{})}),{},cancel));}
ModelCompletion NativeWorkerModelClient::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model complete cancelled");if(!sink)throw std::invalid_argument("Native model event sink required");auto input=impl_->upload(request);return decode_agent_model_completion(impl_->await(impl_->call(impl_->model,"Complete",{impl_->text(attempt),impl_->call(input,"Id",{})}),std::move(sink),cancel));}
}
