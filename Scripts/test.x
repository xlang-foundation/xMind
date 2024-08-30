import xMind

root = xMind.GetRootPath()
xMind.log("Root path: ",root)

@xMind.Function
def HelloFunction():
	xMind.log("Hello xMind Function")
print("Done")