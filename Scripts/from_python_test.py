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
xMind = xlang.importModule("xMind",thru="lrpc:99023")
root = xMind.GetRootPath()
print(root)
core_object = xMind.Agent(name="112")()
id = core_object.ID()

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
print("Before fire event")
xMind.Test() #Fire OnReady event

def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"

xTest = xMind.Test(CallFromHost)
print("xTest:",xTest)
for i in range(1,101):
    xMind.Test() 
    print("i=",i)
xMind.log("Done")
print("Python Done")
