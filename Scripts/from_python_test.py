import xlang
xMind = xlang.importModule("xMind",thru="lrpc:99023")
root = xMind.GetRootPath()
print(root)

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
xMind.Test() #Fire OnReady event

def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"

xTest = xMind.Test(CallFromHost)
print("xTest:",xTest)
for i in range(100000):
    xMind.Test() 
    print("i=",i)
xMind.log("Done")
print("Python Done")
