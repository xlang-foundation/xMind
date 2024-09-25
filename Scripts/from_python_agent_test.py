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

import xlang
import time
xMind = xlang.importModule("xMind",thru="lrpc:99023")
root = xMind.GetRootPath()
print(root)

xMind_Running =3
xMind_Ok = 0
owner = None

@xMind.Agent(inputs=[{"name":"input"}],name="AgentA")
def AgentFirst():
	xMind.log("AgentFirst: Start")
	while owner.isRunning() == xMind_Running:
		inputData = owner.waitInput()
		if inputData[0] == xMind_Ok:
			data = inputData[2]
			xMind.log("AgentFirst LLM->: ",data)
	xMind.log("AgentFirst: Stop")

@xMind.Action(outputs=[{"name":"output"}],name="ActionB")
def ActionFirst():
	xMind.log("ActionFirst: Start")
	while owner.isRunning() == xMind_Running:
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

graph.connect("ActionB","AgentA")
graph.run()


def main_event_loop():
	print("Starting the non-blocking loop...")
	while True:
		time.sleep(0.1) 

# graph.waitToStop()
# input()
main_event_loop()
xMind.log("Done")