import xlang
xMind = xlang.importModule("xMind",thru="lrpc:99023")
root = xMind.GetRootPath()
print(root)
core_object = xMind.Agent(name="112")()
id = core_object.ID()

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
print("Before fire event")
xMind.Test() #Fire OnReady event

def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"

xTest = xMind.Test(CallFromHost)
print("xTest:",xTest)
for i in range(1,101):
    xMind.Test() 
    print("i=",i)
xMind.log("Done")
print("Python Done")
