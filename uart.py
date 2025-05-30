import serial
import json
import time
from datetime import datetime

seri = serial.Serial(
    port='/dev/ttyUSB0',
    baudrate=38400,
    timeout=5
)

json_file_path = '/var/www/html/data.json'
data_log = []

with open(json_file_path, 'w') as json_file:
    json.dump([], json_file)
    print("Cleared existing data in JSON file.")

try:
    while True:
        line = seri.readline().decode('utf-8', errors='ignore').strip()
        print(f"Raw line: {line}")  # DEBUG

        if line:
            parts = line.split('\t')
            parsed = {}

            for part in parts:
                if ':' in part or '=' in part:
                    part = part.replace('=', ':')
                    key, value = part.split(':', 1)
                    key = key.strip()
                    value = value.strip()
                    if key == "Duty Cycle":  # Handle alternate format
                        key = "DutyCycle"
                    parsed[key] = value

            print(f"Parsed data: {parsed}")  # DEBUG

            filtered_entry = {
                "timestamp": datetime.now().isoformat()
            }

            required_fields = ["LowSideVoltage", "LowSideCurrent", "HighSideVoltage", "HighSideCurrent", "DutyCycle"]
            for field in required_fields:
                if field in parsed:
                    filtered_entry[field] = parsed[field]

            if all(k in filtered_entry for k in required_fields):
                data_log.append(filtered_entry)

                with open(json_file_path, 'w') as json_file:
                    print("Dumped to JSON")
                    json.dump(data_log, json_file, indent=4)

                print(f"Appended to JSON: {filtered_entry}")

except KeyboardInterrupt:
    print("Stopped by user")

finally:
    seri.close()
