import xMind

@xMind.Agent(inputs=[{"name":"input"}])
def AgentFirst():
	while owner.isRunning() == xMind.Running:
		inputData = owner.waitInput()
		if inputData[0] == xMind.Ok:
			data = inputData[2]
			xMind.log("AgentFirst LLM->: ",data)
	xMind.log("AgentFirst: Stop")

@xMind.Action(outputs=[{"name":"output"}])
def ActionFirst():
	xMind.log("ActionFirst: Start")
	while owner.isRunning() == xMind.Running:
		xMind.log("Input a prompt to ask AgentFirst:")
		prompt = input()
		if prompt == '!quit':
			owner.graph.stop()
			break
		else:
			owner.pushToOutput(0,prompt)
	xMind.log("ActionFirst: Stop")


graph = xMind.Graph()
graph.addNode(ActionFirst)
graph.addNode(AgentFirst)

graph.connect("ActionFirst","AgentFirst")
graph.run()

graph.waitToStop()
xMind.log("Done")