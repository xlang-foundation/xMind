args = get_args()
args = ["","-uk","1","2"]
def PrintUsage():
	print("xmcli --updatekey or -uk key_name value")

if args.size() <= 2:
	PrintUsage()
	return

# print("Connect to xMind thru 'lrpc:99023'")
import xMind thru 'lrpc:99023'
# print("Connected to xMind")

# Firt one is prog name
firstArg = args[1]
if (firstArg == "-uk" or firstArg == "--updatekey") and args.size()>=4:
	key = args[2]
	value = args[3]
	print("Update Key Store with (",key,",",value,")")
else:
	alert("dbg")
print("Done")
