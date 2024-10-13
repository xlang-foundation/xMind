#
# Copyright (C) 2024 The XLang Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# <END>

import sys,os
script_dir = os.path.dirname(os.path.abspath(__file__))
normalized_path = os.path.normpath(os.path.join(script_dir, '../../Scripts'))
sys.path.append(normalized_path)

from xMind import xMind
import time,requests

xMind.importBlueprint("PyGraph.yml")

@xMind.Agent()
def py_graph_agent(owner):
	inputDataList = owner.waitInputs()
	if inputDataList != None:
		for inputData in inputDataList:
			sessionId = inputData[0]
			inputIndex = inputData[1]
			data = inputData[2]
			resp = owner.llm(data,sessionId,use_session_memory = True)
			resp = "{Python Agent:simple_py_agent}"+resp
			owner.pushToOutput(sessionId,0,resp)

def MakePrompts():
	response = requests.get('http://worldtimeapi.org/api/ip')

	# Parse the response to extract the current date and time
	if response.status_code == 200:
		data = response.json()
		current_date = data['datetime'].split('T')[0]  # Get the date part
		prompts = [{'role': 'assistant', 'content': f'Today is {current_date}.'}]
	return prompts

@xMind.Action()
def py_graph_action(owner):
	inputDataList = owner.waitInputs()
	if inputDataList != None:
		for inputData in inputDataList:
			sessionId = inputData[0]
			inputIndex = inputData[1]
			data = inputData[2]
			action_data = MakePrompts()
			action_data.append(data)
			owner.pushToOutput(sessionId,0,action_data)
		
xMind.MainLoop()

xMind.log("py_graph Done")