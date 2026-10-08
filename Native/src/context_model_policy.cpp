#include "agentflow/context_model_policy.hpp"
#include <array>
#include <string_view>

namespace agentflow {
ContextRuntimePolicy documented_openai_context_policy(){
    struct Fact {std::string_view model,page;std::int64_t context,output;};
    constexpr std::array facts{
        Fact{"gpt-6-astra","gpt-6-astra",1050000,128000},
        Fact{"gpt-6.1-sol","gpt-6.1-sol",1050000,128000},
        Fact{"gpt-6-sol","gpt-6-sol",1050000,128000},
        Fact{"gpt-6-luna","gpt-6-luna",1050000,128000},
        Fact{"gpt-5.6-sol","gpt-5.6-sol",1050000,128000},
        Fact{"gpt-5.5","gpt-5.5",1050000,128000},
        Fact{"gpt-5.5-2026-04-23","gpt-5.5",1050000,128000},
        Fact{"gpt-5.4","gpt-5.4",1050000,128000},
        Fact{"gpt-5.4-2026-03-05","gpt-5.4",1050000,128000},
        Fact{"gpt-4.1","gpt-4.1",1047576,32768},
        Fact{"gpt-4.1-2025-04-14","gpt-4.1",1047576,32768}
    };
    ContextRuntimePolicy result;
    for(const auto& fact:facts){
        VerifiedContextCapacity record;record.wire="responses";record.model_id=fact.model;
        record.revision=1;record.verified=true;record.context_tokens=fact.context;record.output_tokens=fact.output;
        const auto source="https://developers.openai.com/api/docs/models/"+std::string(fact.page);
        record.source_binding=context_digest("native.capacity.official.v1:2026-10-08:"+source+":"+
            std::string(fact.model)+":"+std::to_string(fact.context)+":"+std::to_string(fact.output));
        // No guessed input-only ceiling or undocumented provider default. A
        // configured native profile supplies an explicit request output limit.
        result.model_capacities.emplace(record.model_id,std::move(record));
    }
    return result;
}
}
