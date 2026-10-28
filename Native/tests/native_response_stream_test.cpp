#include "native_response_stream.hpp"
#include <iostream>
#include <future>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
agentflow::NativeResponseStream::Chunk next(agentflow::NativeResponseStream& stream){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    for(;;){auto chunk=stream.read();if(!chunk.data.empty()||chunk.done)return chunk;check(std::chrono::steady_clock::now()<deadline,"Stream did not produce an observation");}
}
}
int main(){
    try{
        {
            auto response=std::make_shared<httplib::Response>();std::atomic_bool release=false,finished=false;
            response->set_chunked_content_provider("text/event-stream",[&](std::size_t,httplib::DataSink& sink){
                if(!sink.write("first\n",6))return false;
                while(!release&&sink.is_writable())std::this_thread::sleep_for(std::chrono::milliseconds(5));
                finished=true;sink.done();return true;
            });
            agentflow::NativeResponseStream stream(response);const auto first=next(stream);
            check(first.data=="first\n"&&!first.done&&!finished,"First event was buffered until provider completion");
            release=true;check(next(stream).done,"Completed provider did not close its stream");
        }
        {
            auto response=std::make_shared<httplib::Response>();
            response->set_chunked_content_provider("text/event-stream",[](std::size_t offset,httplib::DataSink& sink){
                if(offset==0)return sink.write("a",1);
                if(offset==1)return sink.write("b",1);
                sink.done();return true;
            });
            agentflow::NativeResponseStream stream(response);std::string text;
            for(;;){const auto chunk=next(stream);text+=chunk.data;if(chunk.done)break;}
            check(text=="ab","Provider offsets or repeated invocations were lost");
        }
        {
            auto response=std::make_shared<httplib::Response>();std::atomic<std::size_t> written=0;
            response->set_chunked_content_provider("text/event-stream",[&](std::size_t,httplib::DataSink& sink){
                const std::string block(16384,'x');while(sink.is_writable()){if(!sink.write(block.data(),block.size()))return false;written+=block.size();}return false;
            });
            agentflow::NativeResponseStream stream(response);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(written<65536&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(written==65536,"A slow observer was not bounded by queue backpressure");stream.close();
        }
        {
            auto response=std::make_shared<httplib::Response>();
            response->set_chunked_content_provider("text/event-stream",[](std::size_t,httplib::DataSink&){return false;});
            agentflow::NativeResponseStream stream(response);bool failed=false;
            try{(void)next(stream);}catch(const std::exception&){failed=true;}
            check(failed,"An interrupted provider was reported as complete");
        }
        {
            auto response=std::make_shared<httplib::Response>();
            // Deliberately split a multibyte code point at the queue boundary.
            const auto expected=std::string(16383,'x')+"\xe9\x9b\xaa"+std::string(19000,'y');
            response->set_chunked_content_provider("text/event-stream",[&](std::size_t,httplib::DataSink& sink){sink.write(expected.data(),expected.size());sink.done();return true;});
            agentflow::NativeResponseStream stream(response);std::string actual;
            for(;;){const auto chunk=next(stream);actual+=chunk.data;if(chunk.done)break;}
            check(actual==expected,"Multibyte event data changed at a queue boundary");
        }
        std::cout<<"Native stream first-event, incremental offset, backpressure, cancellation, interruption and byte preservation contracts passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
