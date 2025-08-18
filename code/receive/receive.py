#!/usr/bin/python
"""
Weather Station Data Receiver and Uploader

This script receives weather station data from an Arduino via serial communication
and uploads it to a remote server. It features robust error handling, data validation,
and asynchronous processing to ensure reliable data transmission.

Features:
- Asynchronous data reading and server uploading using threading
- Data validation for sensor value ranges
- Local CSV storage for failed uploads
- Retry mechanism for network failures
- Non-blocking serial communication
- Configuration file support for credentials

Data Format:
The Arduino sends 16 int16_t values in binary format (32 bytes - nRF24L01 limit):
[timestamp, temperature*100, pressure/10, humidity*100, illuminance, rain_tips, wind_revolutions, packet_number, sky_temp*100, box_temp*100, rain_detection, rain_analog, co2_ppm, tvoc_ppb, baseline, reserved]

Author: Weather Station Project
Version: 2.0 (Updated for I2C Multiplexer and CCS811)
Date: 2025-07-16
"""

from serial import Serial
from serial.tools import list_ports
import time
import numpy as np
from datetime import datetime, date, timezone
import struct
import requests
import threading
import queue
from astropy.time import Time
from config import get_server_credentials, get_arduino_port_search, get_data_config
import os
import platform


def clear_windows_port(port_device):
    """Clear Windows COM port to resolve access issues.
    
    Args:
        port_device (str): COM port device (e.g., 'COM12')
    """
    if platform.system() == 'Windows':
        try:
            # Try to open and immediately close the port to clear any stale handles
            temp_serial = Serial(port_device, baudrate=115200, timeout=0.1)
            temp_serial.close()
            time.sleep(1.0)  # Give Windows more time to release the port
            print(f"[INFO] Cleared port {port_device}")
        except Exception as e:
            print(f"[WARNING] Could not clear port {port_device}: {e}")


def reset_arduino_connection(port_device):
    """Try to reset Arduino connection by cycling the port.
    
    Args:
        port_device (str): COM port device (e.g., 'COM12')
    """
    if platform.system() == 'Windows':
        try:
            print(f"[INFO] Attempting to reset Arduino connection on {port_device}")
            
            # Try to open port with DTR control
            temp_serial = Serial(port_device, baudrate=115200, timeout=0.1)
            
            # Toggle DTR to reset Arduino (if supported)
            temp_serial.setDTR(False)
            time.sleep(0.1)
            temp_serial.setDTR(True)
            time.sleep(0.1)
            
            temp_serial.close()
            time.sleep(2.0)  # Give Arduino time to reset
            print(f"[INFO] Reset Arduino connection on {port_device}")
            
        except Exception as e:
            print(f"[WARNING] Could not reset Arduino connection: {e}")


def wait_for_arduino_data(serial, timeout=15):
    """Wait for Arduino to start sending data.
    
    Args:
        serial: Serial connection object
        timeout (int): Maximum time to wait in seconds
        
    Returns:
        bool: True if data is available, False if timeout
    """
    start_time = time.time()
    while (time.time() - start_time) < timeout:
        if serial.in_waiting > 0:
            print(f"[INFO] Data available ({serial.in_waiting} bytes)")
            return True
        time.sleep(0.5)
    
    print(f"[WARNING] No data available after {timeout} seconds")
    return False


def try_read_data_directly(serial, timeout=5):
    """Try to read data directly without waiting for markers.
    
    Args:
        serial: Serial connection object
        timeout (int): Maximum time to wait in seconds
        
    Returns:
        bytes: Data bytes if successful, None if failed
    """
    try:
        # Wait for enough data to be available
        start_time = time.time()
        while (time.time() - start_time) < timeout:
            if serial.in_waiting >= 32:
                break
            time.sleep(0.1)
        
        if serial.in_waiting < 32:
            print(f"[WARNING] Not enough data available: {serial.in_waiting} bytes")
            return None
        
        # Read exactly 32 bytes
        data_bytes = serial.read(32)
        print(f"[DEBUG] Read {len(data_bytes)} bytes: {data_bytes.hex()}")
        
        if len(data_bytes) == 32:
            return data_bytes
        else:
            print(f"[WARNING] Incomplete read: {len(data_bytes)} bytes")
            return None
            
    except Exception as e:
        print(f"[ERROR] Error reading data directly: {e}")
        return None


def try_connect_with_different_settings(port_device):
    """Connect with 9600 baud (the only reliable setting).
    
    Args:
        port_device (str): COM port device (e.g., 'COM12')
        
    Returns:
        Serial object or None if connection fails
    """
    try:
        print(f"[INFO] Connecting with 9600 baud on {port_device}")
        serial = Serial(port_device, baudrate=9600, timeout=5, write_timeout=5)
        print(f"[SUCCESS] Connected with 9600 baud")
        return serial
    except Exception as e:
        print(f"[ERROR] Failed to connect with 9600 baud: {e}")
        return None


def check_arduino_ide_running():
    """Check if Arduino IDE Serial Monitor might be running.
    
    Returns:
        bool: True if Arduino IDE processes are detected
    """
    if platform.system() == 'Windows':
        try:
            import subprocess
            
            # Check for Arduino IDE processes
            result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq java.exe'], 
                                  capture_output=True, text=True, timeout=5)
            
            if result.returncode == 0 and 'java.exe' in result.stdout:
                print("[WARNING] Java process detected - Arduino IDE might be running")
                print("[INFO] Please close Arduino IDE Serial Monitor and try again")
                return True
                
        except Exception as e:
            print(f"[WARNING] Could not check for Arduino IDE: {e}")
    
    return False


def reset_arduino_driver_windows(port_device):
    """Try to reset Arduino driver under Windows using devcon.
    
    Args:
        port_device (str): COM port device (e.g., 'COM12')
    """
    if platform.system() == 'Windows':
        try:
            # Try to disable and re-enable the USB device
            import subprocess
            
            # Find the device ID for the Arduino
            result = subprocess.run(['wmic', 'path', 'win32_pnpentity', 'where', 
                                   f'Caption like "%{port_device}%"', 'get', 'DeviceID'], 
                                  capture_output=True, text=True, timeout=10)
            
            if result.returncode == 0 and result.stdout.strip():
                device_id = result.stdout.strip().split('\n')[1].strip()
                print(f"[INFO] Found device ID: {device_id}")
                
                # Try to disable and re-enable the device
                subprocess.run(['pnputil', '/disable-device', device_id], 
                             capture_output=True, timeout=10)
                time.sleep(2)
                subprocess.run(['pnputil', '/enable-device', device_id], 
                             capture_output=True, timeout=10)
                time.sleep(3)
                print(f"[INFO] Reset device {device_id}")
                
        except Exception as e:
            print(f"[WARNING] Could not reset Arduino driver: {e}")


def validate_sensor_data(data):
    """
    Validate sensor data for reasonable ranges.
    
    Args:
        data (tuple): Tuple of 16 int16_t values from sensors
        
    Returns:
        bool: True if data is valid, False otherwise
    """
    if len(data) != 16:
        return False
    
    # Extract the main sensor data (first 15 values including CCS811)
    timestamp, temp_scaled, pressure_scaled, humidity_scaled, illuminance, rain_tips, wind_revolutions, packet_number, sky_temp_scaled, box_temp_scaled, rain_detection, rain_analog, co2_ppm, tvoc_ppb, baseline = data[:15]
    
    # Convert scaled values back to physical units for validation
    temperature = temp_scaled / 100.0  # Convert back from scaled integer
    pressure = pressure_scaled / 10.0  # Convert back from scaled integer
    humidity = humidity_scaled / 100.0  # Convert back from scaled integer
    sky_temp = sky_temp_scaled / 100.0  # Convert back from scaled integer
    box_temp = box_temp_scaled / 100.0  # Convert back from scaled integer
    
    # Check for reasonable ranges
    if not (-327 <= temp_scaled <= 8000):  # Temperature range (-327°C to +80°C scaled)
        print(f"[WARNING] Temperature out of range: {temperature}°C (scaled: {temp_scaled})")
        return False
    
    if not (8000 <= pressure_scaled <= 12000):  # Pressure range (800-1200 hPa scaled)
        print(f"[WARNING] Pressure out of range: {pressure} hPa (scaled: {pressure_scaled})")
        return False
    
    if not (0 <= humidity_scaled <= 10000):  # Humidity range (0-100% scaled)
        print(f"[WARNING] Humidity out of range: {humidity}% (scaled: {humidity_scaled})")
        return False
    
    if not (0 <= illuminance <= 65535):  # Light range (scaled lux, max 655.35 actual lux)
        print(f"[WARNING] Illuminance out of range: {illuminance} (scaled), {illuminance * 100.0} lux (actual)")
        return False
    
    if not (0 <= rain_tips <= 65535):  # Rain tips range
        print(f"[WARNING] Rain tips out of range: {rain_tips}")
        return False
    
    if not (0 <= wind_revolutions <= 65535):  # Wind revolutions range
        print(f"[WARNING] Wind revolutions out of range: {wind_revolutions}")
        return False
    
    # MLX90614 validation
    if not (-327 <= sky_temp_scaled <= 8000):  # Sky temperature range (-327°C to +80°C scaled)
        print(f"[WARNING] Sky temperature out of range: {sky_temp}°C (scaled: {sky_temp_scaled})")
        return False
    
    if not (-327 <= box_temp_scaled <= 8000):  # Box temperature range (-327°C to +80°C scaled)
        print(f"[WARNING] Box temperature out of range: {box_temp}°C (scaled: {box_temp_scaled})")
        return False
    
    # Rain drop sensor validation
    if not (0 <= rain_detection <= 1):  # Binary rain detection (0 or 1)
        print(f"[WARNING] Rain detection out of range: {rain_detection}")
        return False
    
    if not (0 <= rain_analog <= 1023):  # Analog rain sensor value (0-1023)
        print(f"[WARNING] Rain analog value out of range: {rain_analog}")
        return False
    
    # CCS811 air quality sensor validation
    if not (0 <= co2_ppm <= 5000):  # CO2 range (0-5000 ppm)
        print(f"[WARNING] CO2 out of range: {co2_ppm} ppm")
        return False
    
    if not (0 <= tvoc_ppb <= 1000):  # TVOC range (0-1000 ppb)
        print(f"[WARNING] TVOC out of range: {tvoc_ppb} ppb")
        return False
    
    if not (0 <= baseline <= 65535):  # Baseline range (0-65535)
        print(f"[WARNING] Baseline out of range: {baseline}")
        return False
    
    return True


def convert_scaled_data_to_physical(data):
    """
    Convert scaled integer data back to physical units.
    
    Args:
        data (tuple): Tuple of 16 int16_t values from Arduino
        
    Returns:
        dict: Dictionary with physical units and calculated values
    """
    # Extract main sensor data (first 15 values including CCS811)
    timestamp, temp_scaled, pressure_scaled, humidity_scaled, illuminance, rain_tips, wind_revolutions, packet_number, sky_temp_scaled, box_temp_scaled, rain_detection, rain_analog, co2_ppm, tvoc_ppb, baseline = data[:15]
    
    # Convert scaled values back to physical units
    temperature = temp_scaled / 100.0  # Convert back from scaled integer
    pressure = pressure_scaled / 10.0  # Convert back from scaled integer (hPa)
    humidity = humidity_scaled / 100.0  # Convert back from scaled integer
    sky_temp = sky_temp_scaled / 100.0  # Convert back from scaled integer
    box_temp = box_temp_scaled / 100.0  # Convert back from scaled integer
    
    # Convert illuminance from scaled value (divided by 100 in Arduino) back to actual lux
    illuminance_actual = illuminance * 100.0  # Convert back to actual lux
    
    # Calculate derived values
    # Rain:
    #   - 1.25 ml per dipper change
    #   - the diameter of raingauge is about 130mm, r = 65mm, surface = pi*r^2 = 132.73 cm^2
    #   - therefore per m^2 (=10000cm^2) we have a factor of about 75.34
    #   - => 1.25*75.34 = 0.094175mm/m^2
    rain_mm = rain_tips * 1.25  # Convert tips to mm (1.25 mm per tip)
    wind_speed = wind_revolutions * 1.0  # Convert revolutions to m/s (adjust factor as needed)
    # pressure is already in hPa from scaled integer; no further conversion here
    
    # Convert rain detection to boolean
    is_raining = bool(rain_detection)
    
    return {
        'timestamp': timestamp,
        'temperature': temperature,
        'pressure': pressure,
        'humidity': humidity,
        'illuminance': illuminance_actual,  # Use corrected lux value
        'rain_tips': rain_tips,
        'rain_mm': rain_mm,
        'wind_revolutions': wind_revolutions,
        'wind_speed': wind_speed,
        'packet_number': packet_number,
        'sky_temp': sky_temp,
        'box_temp': box_temp,
        'is_raining': is_raining,
        'rain_analog': rain_analog,
        'co2_ppm': co2_ppm,
        'tvoc_ppb': tvoc_ppb,
        'baseline': baseline
    }


def upload_data_to_server(data, username, password, server_url):
    """Upload weather data to remote server.
    
    Args:
        data (dict): Dictionary containing weather data
        username (str): Username for server authentication
        password (str): Password for server authentication
        server_url (str): Server URL for upload
        
    Returns:
        bool: True if upload successful, False otherwise
    """
    try:
        # Prepare data for upload
        jd = Time(datetime.now(timezone.utc)).jd
        
        upload_data = {
            'jd'          : jd,
            'temperature' : data['temperature'],
            'pressure'    : data['pressure'], 
            'humidity'    : data['humidity'],
            'illuminance' : data['illuminance'],
            'wind_speed'  : data['wind_speed'],
            'rain'        : data['rain_mm'],
            'sky_temp'    : data['sky_temp'],
            'box_temp'    : data['box_temp'],
            'is_raining'  : 1 if data['is_raining'] else 0,  # Convert boolean to integer for server
            'rain_analog' : data['rain_analog'],
            'co2_ppm'     : data['co2_ppm'],
            'tvoc_ppb'    : data['tvoc_ppb'],
            'baseline'    : data['baseline']
        }
        
        # Send POST request to server with authentication
        response = requests.post(server_url, auth=(username, password), data=upload_data, timeout=10)
        
        if response.status_code == 200:
            print(f"[SUCCESS] Data uploaded successfully (Packet {data['packet_number']})")
            return True
        else:
            print(f"[ERROR] Server returned status code {response.status_code}")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"[ERROR] Network error during upload: {e}")
        return False
    except Exception as e:
        print(f"[ERROR] Unexpected error during upload: {e}")
        return False


def save_failed_data(data, filename):
    """Save failed upload data to local CSV file.
    
    Args:
        data (dict): Weather data to save
        filename (str): CSV filename
    """
    try:
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        # Create CSV line
        csv_line = f"{timestamp},{data['temperature']},{data['pressure']},{data['humidity']},{data['illuminance']},{data['wind_speed']},{data['rain_mm']},{data['sky_temp']},{data['box_temp']},{1 if data['is_raining'] else 0},{data['rain_analog']},{data['co2_ppm']},{data['tvoc_ppb']},{data['baseline']}\n"
        
        # Append to file
        with open(filename, 'a') as f:
            f.write(csv_line)
            
        print(f"[INFO] Failed data saved to {filename}")
        
    except Exception as e:
        print(f"[ERROR] Failed to save data to CSV: {e}")


def upload_worker(data_queue, failed_data_file, username, password, server_url):
    """Worker thread for uploading data to server.
    
    Args:
        data_queue (queue.Queue): Queue containing data to upload
        failed_data_file (str): Filename for failed uploads
        username (str): Username for server authentication
        password (str): Password for server authentication
        server_url (str): Server URL for upload
    """
    while True:
        try:
            # Get data from queue (blocking)
            data = data_queue.get(timeout=1)
            
            # Attempt upload with authentication
            if not upload_data_to_server(data, username, password, server_url):
                # Save to local file if upload fails
                save_failed_data(data, failed_data_file)
                
        except queue.Empty:
            # No data in queue, continue
            continue
        except Exception as e:
            print(f"[ERROR] Upload worker error: {e}")
            time.sleep(1)


def readline(port_device, timestamp=True, max_retries=5):
    """Read a line of data from serial port with retry mechanism.
    
    Args:
        port_device (str): Serial device path (e.g., '/dev/ttyUSB0')
        timestamp (bool): Whether to include timestamp
        max_retries (int): Maximum number of retry attempts
        
    Returns:
        tuple: (timestamp, data) or data depending on timestamp parameter
        
    Raises:
        RuntimeError: If data cannot be read after max_retries
    """
    for attempt in range(max_retries):
        try:
            # Simple retry with delay
            if attempt > 0:
                print(f"[INFO] Retry attempt {attempt + 1}/{max_retries}")
                time.sleep(3)  # Wait between retries
            
            # Connect with 9600 baud
            serial = try_connect_with_different_settings(port_device)
            if serial is None:
                raise RuntimeError("Could not establish connection")
                
            try:
                # Clear any existing data in the buffer
                serial.reset_input_buffer()
                serial.reset_output_buffer()
                
                # Wait for Arduino to start sending data
                if not wait_for_arduino_data(serial, timeout=15):
                    raise RuntimeError("Arduino not sending data")
                
                # Try to find data marker 'D' or read data directly
                data_bytes = None
                start_time = time.time()
                while (time.time() - start_time) < 10:  # Increased timeout to 10 seconds
                    marker = serial.read(1)
                    if marker == b'D':
                        # Found marker, read data normally
                        data_bytes = serial.read(32)
                        if len(data_bytes) != 32:
                            print(f"[WARNING] Incomplete data received: {len(data_bytes)} bytes instead of 32")
                            print(f"[DEBUG] Data received: {data_bytes.hex()}")
                            raise RuntimeError(f"Incomplete data received: {len(data_bytes)} bytes instead of 32")
                        
                        # Check for end marker (optional)
                        end_marker = serial.read(1)
                        if end_marker != b'E':
                            print(f"[WARNING] Invalid or missing end marker: {end_marker} (hex: {end_marker.hex()})")
                        break
                    elif marker == b'':  # No data available
                        time.sleep(0.1)  # Short wait before trying again
                        continue
                    else:
                        # Skip text messages from Arduino (printable ASCII characters)
                        if marker in b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?()[]{}:;\r\n\t':
                            continue  # Skip text, don't print debug
                        elif marker != b'\x00':
                            print(f"[DEBUG] Received non-text marker: {marker} (hex: {marker.hex()})")
                
                # If we didn't find 'D' marker, try to read data directly
                if data_bytes is None:
                    print("[WARNING] Data marker 'D' not found, attempting to read data directly")
                    data_bytes = try_read_data_directly(serial, timeout=5)
                    if data_bytes is None:
                        raise RuntimeError("Could not read data directly")
                
                # Unpack data as 16 int16_t values
                data = struct.unpack('16h', data_bytes)  # 'h' for int16_t
                
                # Validate data (check for reasonable ranges)
                if not validate_sensor_data(data):
                    raise RuntimeError("Sensor data validation failed")
                
                if timestamp:
                    return (str(date.today()) + " " + datetime.now().strftime("%H:%M:%S"), data)
                else:
                    return data
            finally:
                # Always close the serial connection
                serial.close()
                    
        except Exception as e:
            print(f"[ERROR] Attempt {attempt + 1}/{max_retries} failed: {e}")
            if attempt == max_retries - 1:
                raise RuntimeError(f"Failed to read data after {max_retries} attempts")
            time.sleep(1)  # Longer wait before retry


def main():
    """Main function for weather station data receiver."""
    print("Weather Station Data Receiver")
    print("Version 2.01")
    print("=" * 50)
    
    # Load configuration
    data_config = get_data_config()
    username, password, server_url = get_server_credentials()
    
    # Configuration from file
    failed_data_file = data_config.get("failed_data_file", "failed_uploads.csv")
    queue_size = data_config.get("queue_size", 100)
    data_queue = queue.Queue(maxsize=queue_size)  # Limit queue size
    
    # Start upload worker thread with authentication
    upload_thread = threading.Thread(target=upload_worker, args=(data_queue, failed_data_file, username, password, server_url), daemon=True)
    upload_thread.start()
    print("[INFO] Upload worker thread started")
    
    # Find Arduino port using configuration
    comport = None
    port_search_terms = get_arduino_port_search()
    
    # List all available ports for debugging
    print("[INFO] Available COM ports:")
    for port in list_ports.comports():
        print(f"  - {port.device}: {port.description}")
    
    # Try to find Arduino using search terms
    for port in list_ports.comports():
        for term in port_search_terms:
            if term in port.description or term in port.device:
                comport = port
                break
        if comport:
            break
    
    # If not found, try common Arduino port descriptions
    if not comport:
        print("[INFO] Trying common Arduino port descriptions...")
        common_terms = ["USB-SERIAL CH340", "Arduino", "CH340", "USB Serial Device"]
        for port in list_ports.comports():
            for term in common_terms:
                if term in port.description:
                    comport = port
                    print(f"[INFO] Found Arduino using common term '{term}' on {port.device}")
                    break
            if comport:
                break
    
    if not comport:
        print("[ERROR] Arduino not found!")
        print(f"[INFO] Searched for: {port_search_terms}")
        print("[INFO] Also tried common terms: USB-SERIAL CH340, Arduino, CH340, USB Serial Device")
        return
    
    print(f"[INFO] Found Arduino on {comport.device}: {comport.description}")
    
    # Check if Arduino IDE might be running
    if check_arduino_ide_running():
        print("[INFO] Waiting 5 seconds for Arduino IDE to close...")
        time.sleep(5)
    
    # No port clearing or reset needed - just connect directly
    
    # Main data processing loop
    consecutive_failures = 0
    max_consecutive_failures = 3
    
    while True:
        try:
            # Read data from serial (this is the only blocking operation)
            data_table = readline(comport.device, timestamp=False)
            print("[INFO] Measurement completed!")
            
            # Reset failure counter on success
            consecutive_failures = 0
            
            # Convert scaled integer data to physical units
            physical_data = convert_scaled_data_to_physical(data_table)
            
            # Extract values for upload
            timestamp = physical_data['timestamp']
            temperature = physical_data['temperature']
            pressure = physical_data['pressure']
            humidity = physical_data['humidity']
            illuminance = physical_data['illuminance']
            wind_speed = physical_data['wind_speed']
            rain = physical_data['rain_mm']
            packet_number = physical_data['packet_number']
            sky_temp = physical_data['sky_temp']
            box_temp = physical_data['box_temp']
            is_raining = physical_data['is_raining']
            rain_analog = physical_data['rain_analog']
            co2_ppm = physical_data['co2_ppm']
            tvoc_ppb = physical_data['tvoc_ppb']
            baseline = physical_data['baseline']
            
            print(f"[DATA] Timestamp: {timestamp}, Temp: {temperature}°C, Pressure: {pressure} hPa, Humidity: {humidity}%, Light: {illuminance} lux, Wind: {wind_speed} m/s, Rain: {rain} mm, Sky: {sky_temp}°C, Box: {box_temp}°C, Raining: {'YES' if is_raining else 'NO'}, Analog: {rain_analog}, CO2: {co2_ppm} ppm, TVOC: {tvoc_ppb} ppb, Baseline: {baseline}, Packet: {packet_number}")
            
            # Prepare data for upload
            jd = Time(datetime.now(timezone.utc)).jd
            data = {
                'jd'          : jd,
                'temperature' : temperature,
                'pressure'    : pressure,  
                'humidity'    : humidity,
                'illuminance' : illuminance,
                'wind_speed'  : wind_speed,
                'rain'        : rain,      # kept for compatibility (not used downstream)
                'rain_mm'     : rain,      # required by upload/save helpers
                'sky_temp'    : sky_temp,
                'box_temp'    : box_temp,
                'is_raining'  : 1 if is_raining else 0,  # Convert boolean to integer for server
                'rain_analog' : rain_analog,
                'co2_ppm'     : co2_ppm,
                'tvoc_ppb'    : tvoc_ppb,
                'baseline'    : baseline,
                'packet_number': packet_number
            }

            # Add data to upload queue (non-blocking)
            try:
                data_queue.put(data, timeout=1)  # 1 second timeout
                print(f"[INFO] Data queued for upload (queue size: {data_queue.qsize()})")
            except queue.Full:
                print("[WARNING] Upload queue full, saving data locally")
                save_failed_data(data, failed_data_file)
                
        except Exception as e:
            consecutive_failures += 1
            print(f"[ERROR] Critical error in main loop (failure {consecutive_failures}/{max_consecutive_failures}): {e}")
            
            if consecutive_failures >= max_consecutive_failures:
                print(f"[ERROR] Too many consecutive failures ({consecutive_failures}), waiting longer before retry")
                time.sleep(30)  # Wait 30 seconds before retrying
                consecutive_failures = 0  # Reset counter
            else:
                time.sleep(5)  # Short wait before retrying


if __name__ == "__main__":
    main()
