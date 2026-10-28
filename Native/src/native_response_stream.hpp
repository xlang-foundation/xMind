#pragma once
#include "httplib.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace agentflow {
// Adapt cpp-httplib's push provider to bounded SDK/ASGI reads. A provider may
// write many events in one invocation; never buffer that whole invocation.
class NativeResponseStream {
public:
    struct Chunk { std::string data; bool done=false; };
    using Guard=std::function<void(const std::function<void()>&)>;
private:
    std::shared_ptr<httplib::Response> response_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::function<bool()> running_;
    Guard guard_;
    std::mutex mutex_;
    std::mutex closing_;
    std::condition_variable changed_;
    std::deque<std::string> queue_;
    std::size_t queued_=0,total_=0,offset_=0;
    bool done_=false,failed_=false;
    std::thread producer_;
    bool writable()const{return !cancelled_->load()&&(!running_||running_());}
    void pump(){
        try{
            httplib::DataSink sink;
            sink.is_writable=[&]{return writable();};
            sink.done=[&]{std::lock_guard lock(mutex_);done_=true;changed_.notify_all();};
            sink.write=[&](const char* data,std::size_t size){
                for(std::size_t position=0;position<size;){
                    const auto count=std::min<std::size_t>(16384,size-position);
                    std::unique_lock lock(mutex_);
                    while(queued_>=65536&&writable())changed_.wait_for(lock,std::chrono::milliseconds(50));
                    if(!writable())return false;
                    if(total_+count>64*1024*1024)throw std::runtime_error("Event observation exceeds limits");
                    queue_.emplace_back(data+position,count);queued_+=count;total_+=count;offset_+=count;position+=count;changed_.notify_all();
                }
                return true;
            };
            while(writable()){
                {std::lock_guard lock(mutex_);if(done_)break;}
                if(!response_->content_provider_(offset_,16384,sink)){
                    if(writable())throw std::runtime_error("Event observation interrupted");break;
                }
            }
        }catch(...){std::lock_guard lock(mutex_);failed_=writable();}
        {std::lock_guard lock(mutex_);done_=true;changed_.notify_all();}
    }
public:
    NativeResponseStream(std::shared_ptr<httplib::Response> response,
        std::shared_ptr<std::atomic_bool> cancelled={},std::function<bool()> running={},Guard guard={})
        :response_(std::move(response)),cancelled_(cancelled?std::move(cancelled):std::make_shared<std::atomic_bool>(false)),running_(std::move(running)),guard_(std::move(guard)){
        producer_=std::thread([this]{try{if(guard_)guard_([this]{pump();});else pump();}catch(...){std::lock_guard lock(mutex_);failed_=true;done_=true;changed_.notify_all();}});
    }
    ~NativeResponseStream(){close();}
    Chunk read(){
        std::unique_lock lock(mutex_);
        if(queue_.empty()&&!done_)changed_.wait_for(lock,std::chrono::milliseconds(50),[&]{return !queue_.empty()||done_||!writable();});
        if(failed_&&queue_.empty())throw std::runtime_error("Native event observation failed");
        Chunk value;
        if(!queue_.empty()){value.data=std::move(queue_.front());queue_.pop_front();queued_-=value.data.size();changed_.notify_all();}
        value.done=(done_||!writable())&&queue_.empty();return value;
    }
    void close(){
        cancelled_->store(true);changed_.notify_all();
        std::lock_guard lock(closing_);
        if(producer_.joinable())producer_.join();
    }
};
}
