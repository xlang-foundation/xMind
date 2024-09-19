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

@srv.route("/api/nodes")
def queryGraph():
	nodes = metrics.queryNodes()
	return [str(nodes,format=True), "text/json"]

xMind.logV("xMind WebServer Started,port:",port)
srv.listen("::", port)
