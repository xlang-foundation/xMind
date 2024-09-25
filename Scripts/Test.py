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

import requests

# Replace with your actual OpenAI API key
api_key = "Your Key"

# Define the endpoint URL
url = "https://api.openai.com/v1/chat/completions"

# Define the headers
headers = {
    "Content-Type": "application/json",
    "Authorization": f"Bearer {api_key}"
}

# Define the data (payload) for the request
data = {
    "model": "gpt-4",  # Specify the model you want to use
    "messages": [
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": "Explain how to use Python with REST API."}
    ],
    "temperature": 0.7
}

data = {
	"model":"gpt-4",
	"temperature":0.700000,
	"messages":[
	{
	"role":"user",
	"content":"1+1=2?"
}
]
}
# Send the POST request to the OpenAI API
response = requests.post(url, headers=headers, json=data)

# Check if the request was successful
if response.status_code == 200:
    # Parse and print the response
    completion = response.json()
    print(completion)
    print(completion['choices'][0]['message']['content'])
else:
    # Print the error if the request was not successful
    print(f"Error: {response.status_code}, {response.text}")
