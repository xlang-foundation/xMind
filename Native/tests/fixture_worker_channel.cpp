#include "fixture_worker_channel.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "xlang3/cpp/runtime.h"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace agentflow {
namespace {
using Clock=std::chrono::steady_clock;
constexpr const char* package_name="_xmind_fixture_worker_channel";
[[noreturn]] void reject(){throw AgentWorkerProtocolError("Fixture SDK channel rejected its message");}
void check(AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    if(stop.stop_requested())throw AgentWorkerChannelCancelled("Fixture SDK channel cancelled");
    if(Clock::now()>=deadline)throw AgentWorkerChannelTimeout("Fixture SDK channel deadline elapsed");
}
void validate(const AgentWorkerMessage& message){
    if(message.operation.empty()||message.operation.size()>64||message.operation.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._")!=std::string::npos||message.payload.size()>FixtureWorkerChannel::max_message_bytes-message.operation.size())reject();
}
struct Mailbox {
    std::mutex mutex;std::condition_variable changed;bool waiting=false,closed=false;
    std::optional<AgentWorkerMessage> message;
};
struct MailboxApi {
    std::shared_ptr<Mailbox> box;
    MailboxApi()=default;explicit MailboxApi(std::shared_ptr<Mailbox> value):box(std::move(value)){}
    std::int64_t Push(std::string operation,std::string payload){
        AgentWorkerMessage message{std::move(operation),std::move(payload)};validate(message);if(!box)reject();
        std::lock_guard lock(box->mutex);if(box->closed)return -1;if(!box->waiting||box->message)return 0;
        box->message=std::move(message);box->changed.notify_one();return 1;
    }
    BEGIN_PACKAGE(MailboxApi)
        APISET().AddFunc<2>("Push",&MailboxApi::Push);
    END_PACKAGE
};
struct FixtureRoot {
    std::mutex mutex;std::map<std::string,std::shared_ptr<Mailbox>> boxes;
    std::shared_ptr<Mailbox> find(const std::string& key){
        if(key.empty()||key.size()>2048)reject();std::lock_guard lock(mutex);
        auto& value=boxes[key];if(!value)value=std::make_shared<Mailbox>();return value;
    }
    X::Value Open(std::string key){return __xlang3_package_->CreateInstance("Mailbox",std::make_shared<MailboxApi>(find(key)));}
    BEGIN_PACKAGE(FixtureRoot)
        APISET().AddClass<0,MailboxApi>("Mailbox");
        APISET().AddFunc<1>("Open",&FixtureRoot::Open);
    END_PACKAGE
};
FixtureRoot& root(){
    static auto owner=[] {auto value=std::make_shared<FixtureRoot>();retain_xmind_ipc_service(value);auto& runtime=xmind_ipc_runtime();auto package=runtime.RegisterPackage(package_name,*value);runtime.RegisterRemoteObject(package_name,package);return value;}();return *owner;
}
std::string current_identity(){FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))reject();const auto birth=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;return std::to_string(GetCurrentProcessId())+":"+std::to_string(birth);}
std::string connection_key(const AgentWorkerChannel& channel){
    const auto peer=channel.peer();auto first=current_identity(),second=std::to_string(peer.pid)+":"+std::to_string(peer.creation_time);if(second<first)std::swap(first,second);
    const auto& scope=channel.scope();return first+"|"+second+"|"+scope.root_id+"|"+scope.run_id+"|"+scope.workspace_id+"|"+scope.generation+"|"+std::to_string(scope.workspace_revision);
}
}
struct FixtureWorkerChannel::Impl {
    AgentWorkerChannel& channel;std::shared_ptr<Mailbox> inbox;
    std::mutex reader,writer;std::atomic<bool> poisoned=false;
    std::atomic<std::uint64_t> reads=0,writes=0,cancelled_reads=0,cancelled_writes=0;
    X::Runtime client;X::Value remote;
    Impl(AgentWorkerChannel& value,const std::string& key):channel(value),inbox(root().find(key)){
        auto service=client.ImportRemote(package_name,channel.peer().endpoint.c_str());
        if(!service["Open"].Call(std::vector<X::Value>{X::Value(client,key)},remote))reject();
    }
    void live(){if(poisoned||channel.peer_closed())reject();}
    void poison(){poisoned=true;std::lock_guard lock(inbox->mutex);inbox->closed=true;inbox->message.reset();inbox->changed.notify_all();}
};
void initialize_fixture_worker_channel(){ensure_xmind_ipc_api();(void)root();}
FixtureWorkerChannel::FixtureWorkerChannel(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
FixtureWorkerChannel& fixture_worker_channel(AgentWorkerChannel& channel){
    const auto key=connection_key(channel);static std::mutex mutex;static std::map<std::string,std::unique_ptr<FixtureWorkerChannel>> channels;
    std::lock_guard lock(mutex);auto& value=channels[key];if(!value)value=std::make_unique<FixtureWorkerChannel>(std::make_shared<FixtureWorkerChannel::Impl>(channel,key));return *value;
}
FixtureWorkerChannelStatistics FixtureWorkerChannel::statistics()const{return {impl_->reads.load(),impl_->writes.load(),impl_->cancelled_reads.load(),impl_->cancelled_writes.load()};}
void FixtureWorkerChannel::send(const AgentWorkerMessage& message,AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    std::lock_guard writer(impl_->writer);++impl_->writes;
    try{check(deadline,stop);impl_->live();validate(message);const std::vector<X::Value> args{X::Value(impl_->client,message.operation),X::Value(impl_->client,message.payload)};
        for(;;){check(deadline,stop);impl_->live();X::Value reply;if(!impl_->remote["Push"].Call(args,reply)||!reply.IsInt64())reject();const auto status=reply.ToInt64();if(status==1){check(deadline,stop);return;}if(status!=0)reject();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    }catch(const AgentWorkerChannelCancelled&){++impl_->cancelled_writes;impl_->poison();throw;}
    catch(const AgentWorkerChannelTimeout&){++impl_->cancelled_writes;impl_->poison();throw;}
    catch(...){impl_->poison();throw AgentWorkerProtocolError("Fixture SDK channel send failed");}
}
AgentWorkerMessage FixtureWorkerChannel::receive(AgentWorkerChannel::Deadline deadline,std::stop_token stop){
    std::lock_guard reader(impl_->reader);++impl_->reads;auto& box=*impl_->inbox;std::unique_lock lock(box.mutex);box.waiting=true;
    struct Waiting {Mailbox& box;~Waiting(){box.waiting=false;}} waiting{box};
    try{for(;;){check(deadline,stop);if(box.message){auto message=std::move(*box.message);box.message.reset();return message;}if(box.closed)reject();impl_->live();box.changed.wait_for(lock,std::chrono::milliseconds(5));}}
    catch(const AgentWorkerChannelCancelled&){++impl_->cancelled_reads;impl_->poisoned=true;throw;}
    catch(const AgentWorkerChannelTimeout&){++impl_->cancelled_reads;impl_->poisoned=true;throw;}
    catch(...){impl_->poisoned=true;throw;}
}
}
