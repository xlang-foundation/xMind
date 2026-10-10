#pragma once
#include "agentflow/mcp_oauth_credentials.hpp"
namespace agentflow {
// Consumes the one private snapshot returned by durable claim admission.
// Duplicate observations do not dispatch. The returned terminal record is read
// from SQLite, including uncertain or committed outcomes after a failed reply.
McpOAuthRefreshRecord renew_mcp_oauth_grant(PersistenceService&,const McpServerSetting&,
    McpOAuthStoredRefresh,McpDeadline,std::stop_token cancel={});
}
