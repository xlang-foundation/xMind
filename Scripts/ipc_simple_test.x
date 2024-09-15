import xMind thru 'lrpc:99023'

root = xMind.GetRootPath()

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
xMind.Test()
def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"
xTest = xMind.Test(CallFromHost)
print("Done")