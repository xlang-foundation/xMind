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
sys.path.append(os.path.abspath('../Scripts'))

from xMind import xMind
import time

xMind.importBlueprint("simple.yml")

@xMind.Agent()
def SingleAgent(owner):
	inputData = owner.waitInput()
	if inputData[0] == xMind.Ok:
		sessionId = inputData[1]
		inputIndex = inputData[2]
		data = inputData[3]
		resp = owner.llm(data)
		owner.pushToOutput(sessionId,0,resp)
		
xMind.MainLoop()

xMind.log("SingleAgent Done")