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
import time

xMind.importBlueprint("simple.yml")

# if need use session memory, call llm with use_session_memory = True
@xMind.Agent()
def simple_py_agent(owner):
	inputDataList = owner.waitInputs()
	if inputDataList != None:
		for inputData in inputDataList:
			sessionId = inputData[0]
			inputIndex = inputData[1]
			data = inputData[2]
			resp = owner.llm(data,sessionId)
			resp = "{Python Agent:simple_py_agent}"+resp
			owner.pushToOutput(sessionId,0,resp)

		
xMind.MainLoop()

xMind.log("simple_py_agent Done")