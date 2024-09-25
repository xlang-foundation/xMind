#
# Copyright (C) 2024 The XLang Foundation
#
# This file is part of the xMind Project.
#
# xMind is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# xMind is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with xMind. If not, see <https://www.gnu.org/licenses/>.
# <END>

from xlang_http import http
import xMind thru 'lrpc:99023'
import json
from xlang_os import utils
# import agent_service

xMind.logV("Service Starting")
args = get_args()
if args.size()>0:
	port = args[0]
else:
	port = 9901
srv = http.Server()
srv.SupportStaticFiles = True
www_root = "webpages"
srv.StaticRoots = [www_root]
srv.StaticIndexFile = "index.html"

xMind.logV("root folder:",www_root,",pid=",pid())


session_histories ={}

# Link: http://localhost:9901/api/loadGraph?fileName=D:\Github\xMind\Schema\compositeAgent.yml
@srv.route("/api/loadGraph")
def loadGraph():
	params = req.params
	fileName = params["fileName"]
	isOK = xMind.LoadBlueprintFromFile(fileName)
	return [str(isOK,format=True), "text/json"]
# Link: http://localhost:9901/api/queryGraph
@srv.route("/api/queryGraph")
def queryGraph():
	graphJson = xMind.QueryAgentFlow()
	return [graphJson, "text/json"]


# this call will keep session history
@srv.route("/api/chat/completions")
def chat_completions():
    params = json.loads(req.body)
    resp = xMind.ChatRequest(params)
    return [str(resp,format=True), "text/json"]

@srv.route("/api/completions")
def completions():
    params = json.loads(req.body)
    resp = xMind.CompletionRequest(params)
    return [str(resp,format=True), "text/json"]


# Link: http://localhost:9901/api/rootagents
@srv.route("/api/rootagents")
def query_rootagents():
    rootAgents = xMind.GetRootAgents()
    return [str(rootAgents,format=True), "text/json"]

xMind.logV("xMind WebServer Started,port:",port)
srv.listen("::", port)
