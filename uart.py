import serial
import json
import time
from datetime import datetime

seri=serial.Serial(
    port='/dev/tty',
    baudrate=9600,
    timeout=1
)

json_file_path = '/var/www/html/data.json'
data_log = []

#http://172.20.10.7/data.json address

try:
    while True:
        line = seri.readline().decode('utf-8', errors='ignore').strip()
        if line:
            entry = {
                "timestamp": datetime.now().isoformat(),
                "data": line
            }
            data_log.append(entry)

            with open(json_file_path, 'w') as json_file:
                json.dump(data_log, json_file, indent=4)

            print(f"Appended to JSON: {entry}")

except KeyboardInterrupt:
    print("Stopped by user")

finally:
    seri.close()

