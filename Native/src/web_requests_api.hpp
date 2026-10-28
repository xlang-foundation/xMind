// Included after ApiContext. These are ordinary SDK objects and values; the
// browser's HTTP contract is dispatched in C++, without an internal HTTP peer.
struct WebResponseApi {
    std::shared_ptr<ApiContext> context;
    std::shared_ptr<httplib::Response> response;
    std::mutex mutex;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<NativeResponseStream> stream;
    WebResponseApi()=default;
    WebResponseApi(std::shared_ptr<ApiContext> owner,std::shared_ptr<httplib::Response> value,std::shared_ptr<std::atomic_bool> cancellation)
        :context(std::move(owner)),response(std::move(value)),cancelled(std::move(cancellation)){}
    auto enter(){if(!context)throw RunUnavailable("Response unavailable");return context->enter();}
    std::int64_t Status(){auto life=enter();std::lock_guard lock(mutex);if(!response)throw RunUnavailable("Response closed");return response->status;}
    X::Value Headers(){auto life=enter();std::lock_guard lock(mutex);if(!response)throw RunUnavailable("Response closed");auto entries=X::Value::Dict(Host());for(const auto& [name,value]:response->headers)set(entries,name.c_str(),X::Value::String(Host(),value));return entries;}
    std::string Body(){auto life=enter();std::lock_guard lock(mutex);if(!response)throw RunUnavailable("Response closed");return response->body;}
    bool Streaming(){auto life=enter();std::lock_guard lock(mutex);if(!response)throw RunUnavailable("Response closed");return static_cast<bool>(response->content_provider_);}
    X::Value Read(){
        auto life=enter();std::shared_ptr<NativeResponseStream> current;
        {std::lock_guard lock(mutex);if(cancelled->load())throw RunUnavailable("Response closed");if(!stream)stream=std::make_shared<NativeResponseStream>(response,cancelled,context->web_running,[owner=context](const std::function<void()>& pump){auto life=owner->enter();pump();});current=stream;}
        const auto chunk=current->read();auto result=X::Value::Dict(Host());set(result,"data",X::Value::Bytes(Host(),chunk.data.data(),chunk.data.size()));set(result,"done",X::Value(chunk.done));return result;
    }
    void Close(){cancelled->store(true);std::shared_ptr<NativeResponseStream> current;{std::lock_guard lock(mutex);current=stream;}if(current)current->close();{std::lock_guard lock(mutex);stream.reset();response.reset();}}
    BEGIN_PACKAGE(WebResponseApi)
        APISET().AddFunc<0>("Status",&WebResponseApi::Status);
        APISET().AddFunc<0>("Headers",&WebResponseApi::Headers);
        APISET().AddFunc<0>("Body",&WebResponseApi::Body);
        APISET().AddFunc<0>("Streaming",&WebResponseApi::Streaming);
        APISET().AddFunc<0>("Read",&WebResponseApi::Read);
        APISET().AddFunc<0>("Close",&WebResponseApi::Close);
    END_PACKAGE
};
struct WebRequestState {
    std::shared_ptr<ApiContext> context;
    std::shared_ptr<httplib::Response> response=std::make_shared<httplib::Response>();
    std::shared_ptr<std::atomic_bool> cancellation=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<WebResponseApi> api;
    std::mutex mutex;
    bool done=false,closed=false;
    std::exception_ptr failure;
    std::atomic_bool released=false;
    explicit WebRequestState(std::shared_ptr<ApiContext> owner):context(std::move(owner)){
        api=std::make_shared<WebResponseApi>(context,response,cancellation);context->retain_web_reply();
    }
    void release(){if(!released.exchange(true))context->release_web_reply();}
    void close(){cancellation->store(true);{std::lock_guard lock(mutex);closed=true;}api->Close();{std::lock_guard lock(mutex);response.reset();}release();}
    ~WebRequestState(){release();}
};
struct WebRequestJob {
    std::shared_ptr<WebRequestState> state;
    WebRequestJob()=default;
    explicit WebRequestJob(std::shared_ptr<WebRequestState> value):state(std::move(value)){}
    bool Ready(){if(!state)throw RunUnavailable("Request unavailable");std::lock_guard lock(state->mutex);return state->done||state->closed;}
    X::Value Snapshot();
    void Close(){if(state)state->close();}
    BEGIN_PACKAGE(WebRequestJob)
      APISET().AddFunc<0>("Ready",&WebRequestJob::Ready);
      APISET().AddFunc<0>("Snapshot",&WebRequestJob::Snapshot);
      APISET().AddFunc<0>("Close",&WebRequestJob::Close);
    END_PACKAGE
};
struct WebObjects { BEGIN_PACKAGE(WebObjects) APISET().AddClass<0,WebResponseApi>("Response"); APISET().AddClass<0,WebRequestJob>("Request"); END_PACKAGE };
WebObjects& web_objects(){static auto value=[] {auto owner=std::make_shared<WebObjects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_web_objects",*owner);return owner;}();return *value;}
X::Value WebRequestJob::Snapshot(){
    if(!state)throw RunUnavailable("Request unavailable");std::lock_guard lock(state->mutex);
    if(state->closed||!state->done)throw RunUnavailable("Request observation unavailable");if(state->failure)std::rethrow_exception(state->failure);
    const auto& response=*state->response;auto result=X::Value::Dict(Host());set(result,"status",X::Value(response.status));
    auto headers=X::Value::Dict(Host());for(const auto& [name,field]:response.headers)set(headers,name.c_str(),X::Value::String(Host(),field));set(result,"headers",headers);
    set(result,"body",X::Value::String(Host(),response.body));const bool streaming=static_cast<bool>(response.content_provider_);set(result,"streaming",X::Value(streaming));
    if(streaming)set(result,"response",web_objects().__xlang3_package_->CreateInstance("Response",state->api));return result;
}
struct WebRequestsApi {
    std::shared_ptr<ApiContext> context;
    WebRequestsApi(RootApi*,std::string);
    std::shared_ptr<httplib::Response> dispatch(const nlohmann::json& value,std::shared_ptr<std::atomic_bool> cancellation,std::shared_ptr<httplib::Response> output){
        auto life=context->enter();
        if(!context->web_requests)throw RunUnavailable("Web services unavailable");
        if(!value.is_object()||value.size()!=4||!value.contains("method")||!value.contains("target")||!value.contains("headers")||!value.contains("body")||!value["headers"].is_object())throw std::invalid_argument("Invalid web request");
        httplib::Request request;request.is_connection_closed=[cancellation]{return cancellation->load();};request.method=value.at("method").get<std::string>();request.target=value.at("target").get<std::string>();request.body=value.at("body").get<std::string>();
        if((request.method!="GET"&&request.method!="POST"&&request.method!="DELETE")||!request.target.starts_with("/v1/")||request.target.size()>65536||request.target.find_first_of("\r\n\0",0,3)!=std::string::npos||request.body.size()>4*1024*1024)throw std::invalid_argument("Invalid web request");
        const auto query=request.target.find('?');request.path=httplib::decode_uri_component(request.target.substr(0,query));
        if(query!=std::string::npos)httplib::detail::parse_query_text(request.target.substr(query+1),request.params);
        request.headers.emplace("Host","127.0.0.1:"+std::to_string(context->web_port));
        for(const auto& [name,field]:value.at("headers").items()){
            if(name!="Authorization"&&name!="X-XMind-View-Origin"&&name!="Content-Type"&&name!="Last-Event-ID")throw std::invalid_argument("Invalid web request header");
            const auto data=field.get<std::string>();if(data.size()>8192||data.find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::invalid_argument("Invalid web request header");request.headers.emplace(name,data);
        }
        context->web_requests(request,*output);return output;
    }
    X::Value deliver(X::Value input,bool snapshot){
        auto cancellation=std::make_shared<std::atomic_bool>(false);
        auto output=dispatch(sdk_values::from_value(input),cancellation,std::make_shared<httplib::Response>());
        if(snapshot){
            // Owner retirement can close the service immediately after this
            // dispatch. Return immutable metadata in the same SDK invocation.
            auto result=X::Value::Dict(Host());set(result,"status",X::Value(output->status));
            auto headers=X::Value::Dict(Host());for(const auto& [name,field]:output->headers)set(headers,name.c_str(),X::Value::String(Host(),field));set(result,"headers",headers);
            set(result,"body",X::Value::String(Host(),output->body));const bool streaming=static_cast<bool>(output->content_provider_);set(result,"streaming",X::Value(streaming));
            if(streaming)set(result,"response",web_objects().__xlang3_package_->CreateInstance("Response",std::make_shared<WebResponseApi>(context,std::move(output),std::move(cancellation))));
            return result;
        }
        return web_objects().__xlang3_package_->CreateInstance("Response",std::make_shared<WebResponseApi>(context,std::move(output),std::move(cancellation)));
    }
    std::int64_t ReplyProtocol()const{return 3;}
    X::Value BeginRequest(X::Value input){
        const auto value=sdk_values::from_value(input);auto state=std::make_shared<WebRequestState>(context);
        const auto object=web_objects().__xlang3_package_->CreateInstance("Request",std::make_shared<WebRequestJob>(state));
        // No domain work blocks the SDK listener's per-call deadline. The
        // caller observes this one admitted job; it never replays the request.
        const auto captured=context;auto output=state->response;
        std::thread([state,captured,value,output]{
            try{WebRequestsApi service;service.context=captured;service.dispatch(value,state->cancellation,output);}
            catch(...){std::lock_guard lock(state->mutex);state->failure=std::current_exception();}
            {std::lock_guard lock(state->mutex);state->done=true;}
        }).detach();return object;
    }
    WebRequestsApi()=default;
    X::Value Request(X::Value input){return deliver(input,false);}
    X::Value RequestSnapshot(X::Value input){return deliver(input,true);}
    BEGIN_PACKAGE(WebRequestsApi)
      APISET().AddFunc<0>("ReplyProtocol",&WebRequestsApi::ReplyProtocol);
      APISET().AddFunc<1>("BeginRequest",&WebRequestsApi::BeginRequest);
      APISET().AddFunc<1>("RequestSnapshot",&WebRequestsApi::RequestSnapshot);
      APISET().AddFunc<1>("Request",&WebRequestsApi::Request);
    END_PACKAGE
};
