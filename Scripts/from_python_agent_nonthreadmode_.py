#
# Copyright (C) 2024 The XLang Foundation
#
# This file is part of the xMind Project.
#
# xMind is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# xMind is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with xMind. If not, see <https://www.gnu.org/licenses/>.
# <END>

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