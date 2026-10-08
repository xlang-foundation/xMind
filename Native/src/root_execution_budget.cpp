#include "agentflow/root_execution_budget.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace agentflow {
namespace {
std::string attempt_id(){std::random_device source;std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)out<<std::setw(8)<<source();return out.str();}
}
RootExecutionBudget::RootExecutionBudget(PersistenceService& store,RootBudgetRecord record,
    std::chrono::steady_clock::time_point deadline,std::stop_token cancel)
    :store_(store),root_(std::move(record.root_run_id)),deadline_(deadline),cancel_(cancel),parallel_(static_cast<std::size_t>(record.spec.max_parallel)){
    if(root_.empty() || record.spec.max_parallel<1 || record.spec.max_parallel>2)throw std::invalid_argument("Invalid root execution budget");
}
void RootExecutionBudget::check(std::stop_token cancel)const{
    if(cancel_.stop_requested() || cancel.stop_requested())throw TransportCancelled("Root execution cancelled");
    if(std::chrono::steady_clock::now()>=deadline_)throw RootBudgetDeadlineExceeded("Root execution deadline elapsed");
}
RootBudgetRecord RootExecutionBudget::snapshot()const{return store_.root_budget(root_).get();}
ModelCallReservation RootExecutionBudget::reserve(const std::string& owner,ModelCallRole role,std::stop_token cancel){
    check(cancel);return store_.reserve_model_call(root_,owner,attempt_id(),role).get();
}
void RootExecutionBudget::start(const ModelCallReservation& reservation,std::stop_token cancel){
    check(cancel);
    if(reservation.root_run_id!=root_)throw Conflict("Model call belongs to another execution budget");
    store_.start_model_call(root_,reservation.owner_run_id,reservation.attempt_id).get();
}
void RootExecutionBudget::finish(const ModelCallReservation& reservation){
    if(reservation.root_run_id!=root_)throw Conflict("Model call belongs to another execution budget");
    try{store_.finish_model_call(root_,reservation.owner_run_id,reservation.attempt_id).get();}
    catch(...){throw BudgetOutcomeUnrecorded("Model call retirement could not be recorded; stop execution and recover ownership");}
}
bool RootExecutionBudget::try_acquire_leaf()noexcept{
    auto count=active_leaves_.load();while(count<parallel_)if(active_leaves_.compare_exchange_weak(count,count+1))return true;return false;
}
void RootExecutionBudget::release_leaf()noexcept{active_leaves_.fetch_sub(1);}
}
