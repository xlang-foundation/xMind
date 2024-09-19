from xMind import xMind
import time


@xMind.Agent(inputs=[{"name":"input"}],name="AgentA")
def AgentFirst(owner):
	inputData = owner.waitInput()
	if inputData[0] == xMind.Ok:
		data = inputData[2]
		xMind.log("AgentFirst LLM->: ",data)

@xMind.Action(outputs=[{"name":"output"}],name="ActionB")
def ActionFirst(owner):
	xMind.log("Wait a prompt to input to ask AgentFirst:")
	print("Input a prompt to ask AgentFirst:")
	prompt = input()
	if prompt == '!quit':
		xMind.Stop()
	else:
		xMind.log("Your Prompt:",prompt)
		owner.pushToOutput(0,prompt)


graph = xMind.Graph()
graph.addNode(ActionFirst)
graph.addNode(AgentFirst)

graph.connect("ActionB","AgentA")
graph.run()
print("Entern Mainloop")
xMind.MainLoop()

xMind.log("Done")