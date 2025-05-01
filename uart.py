import serial
import json
import time
from datetime import datetime

seri = serial.Serial(
    port='/dev/ttyUSB0',
    baudrate=9600,
    timeout=5
)

json_file_path = '/var/www/html/data.json'
data_log = []

# Clear the JSON file before logging starts
with open(json_file_path, 'w') as json_file:
    json.dump([], json_file)
    print("Cleared existing data in JSON file.")

try:
    while True:
        line = seri.readline().decode('utf-8', errors='ignore').strip()
        if line:
            parts = line.split('\t')  # Split incoming data by tab
            parsed = {}

            for part in parts:
                if ':' in part or '=' in part:
                    part = part.replace('=', ':')
                    key, value = part.split(':', 1)
                    parsed[key.strip()] = value.strip()

            entry = {
                "timestamp": datetime.now().isoformat(),
                **parsed
            }
            data_log.append(entry)

            with open(json_file_path, 'w') as json_file:
                print("Dumped to JSON")
                json.dump(data_log, json_file, indent=4)

            print(f"Appended to JSON: {entry}")

except KeyboardInterrupt:
    print("Stopped by user")

finally:
    seri.close()
