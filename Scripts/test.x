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

x =10
yes = isinstance(x,int)

import xMind
import pandas as pd
import test_lib
py_ret = test_lib.hello_xMind(xMind)
xlang_ret = to_xlang(py_ret)
yes = isinstance(xlang_ret,dict)
yes = isinstance(xlang_ret,(dict,list))
xlang_ret.set("test_key","this is a key")
root = xMind.GetRootPath()
xMind.log("Root path: ",root)
@xMind.Function(Name="Start",
	Prompt="any prompt",
	EnvType ="Local"
	)
def Start():
	myName = owner.name
	xMind.log(myName, ",input a sentence to start the graph:")
	str0 = input()
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
graph.startOnce()

print("Done")