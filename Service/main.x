from xlang_http import http
import xMind thru 'lrpc:99023'


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

# Link:http://localhost:9901/api/loadGraph?filename=D:\Github\xMind\Schema\compositeAgent.yml
@srv.route("/api/loadGraph")
def loadGraph():
	params = req.params
	fileName = params["fileName"]
	isOK = xMind.LoadAgentFlowFromFile(fileName)
	return [str(isOK,format=True), "text/json"]

@srv.route("/api/queryGraph")
def queryGraph():
	graphJson = xMind.QueryAgentFlow()
	return [graphJson, "text/json"]

xMind.logV("xMind WebServer Started,port:",port)
srv.listen("::", port)
