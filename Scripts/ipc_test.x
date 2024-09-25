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

import xMind thru 'lrpc:99023'
root = xMind.GetRootPath()

def OnReady():
    print("Call from OnReady")
    
xMind.OnReady +=OnReady
for i in range(10000):
    print("Loop:",i)
    xMind.Test() #Fire OnReady event

def CallFromHost(info):
    print("CallFromHost:",info)
    return "Now, from client side with ret"

for i in range(10000):
    print("CallFromHost Loop:",i)
    xTest = xMind.Test(CallFromHost)
    print("xTest:",xTest)
print("End")