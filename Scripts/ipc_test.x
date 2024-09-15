import xMind thru 'lrpc:99023'
root = xMind.GetRootPath()

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
for i in range(10000):
    print("Loop:",i)
    xMind.Test() #Fire OnReady event

def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"

for i in range(10000):
    print("CallFromHost Loop:",i)
    xTest = xMind.Test(CallFromHost)
    print("xTest:",xTest)
print("End")