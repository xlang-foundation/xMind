import xMind


@xMind.Function()
def Start():
	myName = owner.name
	xMind.log(myName, ",input a sentence to start the graph(input !quit to stop):")
	str0 = input()
	if str0 == '!quit':
		owner.graph.stop()
	return str0

@xMind.Function()
def PassOne(input1):
	xMind.log("PassOne:",input1)
	return ["From PassOne",input1]

@xMind.Function(Name="PassTwo")
def PassTwo(input1):
	xMind.log("PassTwo:",input1)
	return ["From PassTwo",input1]


graph = xMind.Graph()
graph.addNode(Start)
graph.addNode(PassOne,"P1")
graph.addNode(PassTwo,"P2")
graph.addNode(PassOne,"P3")
graph.connect("Start","P1")
graph.connect("P1","output","P2","input")
graph.connect("P2","P3")
graph.connect("P3","Start")
graph.startFrom("Start")

print("Done")