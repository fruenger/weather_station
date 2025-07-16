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
from datetime import datetime, date
import struct
import requests
import threading
import queue
from astropy.time import Time
from config import get_server_credentials, get_arduino_port_search, get_data_config


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
    
    if not (0 <= illuminance <= 65535):  # Light range (lux)
        print(f"[WARNING] Illuminance out of range: {illuminance} lux")
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
    pressure = pressure_scaled / 10.0  # Convert back from scaled integer 
    humidity = humidity_scaled / 100.0  # Convert back from scaled integer
    sky_temp = sky_temp_scaled / 100.0  # Convert back from scaled integer
    box_temp = box_temp_scaled / 100.0  # Convert back from scaled integer
    
    # Calculate derived values
    # Rain:
    #   - 1.25 ml per dipper change
    #   - the diameter of raingauge is about 130mm, r = 65mm, surface = pi*r^2 = 132.73 cm^2
    #   - therefore per m^2 (=10000cm^2) we have a factor of about 75.34
    #   - => 1.25*75.34 = 0.094175mm/m^2
    rain_mm = rain_tips * 1.25  # Convert tips to mm (1.25 mm per tip)
    wind_speed = wind_revolutions * 1.0  # Convert revolutions to m/s (adjust factor as needed)
    # TODO: check if this following line is necessary
    pressure = pressure / 100.0  # Convert back to hPa
    
    # Convert rain detection to boolean
    is_raining = bool(rain_detection)
    
    return {
        'timestamp': timestamp,
        'temperature': temperature,
        'pressure': pressure,
        'humidity': humidity,
        'illuminance': illuminance,
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
        jd = Time(datetime.datetime.now(datetime.timezone.utc)).jd
        
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


def readline(timestamp=True, max_retries=3):
    """Read a line of data from serial port with retry mechanism.
    
    Args:
        timestamp (bool): Whether to include timestamp
        max_retries (int): Maximum number of retry attempts
        
    Returns:
        tuple: (timestamp, data) or data depending on timestamp parameter
        
    Raises:
        RuntimeError: If data cannot be read after max_retries
    """
    for attempt in range(max_retries):
        try:
            with Serial(comport.usb_description(), timeout=1) as serial:  # Short timeout for non-blocking
                
                # Wait for data marker 'D' with timeout
                start_time = time.time()
                while (time.time() - start_time) < 5:  # 5 second timeout
                    marker = serial.read(1)
                    if marker == b'D':
                        break
                else:
                    raise RuntimeError("Timeout waiting for data marker 'D'")
                
                # Read data with validation (32 bytes for 16 int16_t values)
                data_bytes = serial.read(32)
                if len(data_bytes) != 32:
                    raise RuntimeError(f"Incomplete data received: {len(data_bytes)} bytes instead of 32")
                
                # Check for end marker
                end_marker = serial.read(1)
                if end_marker != b'E':
                    raise RuntimeError(f"Invalid end marker: {end_marker}")
                
                # Unpack data as 16 int16_t values
                data = struct.unpack('16h', data_bytes)  # 'h' for int16_t
                
                # Validate data (check for reasonable ranges)
                if not validate_sensor_data(data):
                    raise RuntimeError("Sensor data validation failed")
                
                if timestamp:
                    return (str(date.today()) + " " + datetime.now().strftime("%H:%M:%S"), data)
                else:
                    return data
                    
        except Exception as e:
            print(f"[ERROR] Attempt {attempt + 1}/{max_retries} failed: {e}")
            if attempt == max_retries - 1:
                raise RuntimeError(f"Failed to read data after {max_retries} attempts")
            time.sleep(0.1)  # Short wait before retry


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
    
    for port in list_ports.comports():
        for term in port_search_terms:
            if term in port.description or term in port.device:
                comport = port
                break
        if comport:
            break
    
    if not comport:
        print("[ERROR] Arduino not found!")
        print(f"[INFO] Searched for: {port_search_terms}")
        return
    
    print(f"[INFO] Found Arduino on {comport.device}")
    
    # Main data processing loop
    while True:
        try:
            # Read data from serial (this is the only blocking operation)
            data_table = readline(timestamp=False)
            print("[INFO] Measurement completed!")
            
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
            jd = Time(datetime.datetime.now(datetime.timezone.utc)).jd
            data = {
                'jd'          : jd,
                'temperature' : temperature,
                'pressure'    : pressure,  
                'humidity'    : humidity,
                'illuminance' : illuminance,
                'wind_speed'  : wind_speed,
                'rain'        : rain,
                'sky_temp'    : sky_temp,
                'box_temp'    : box_temp,
                'is_raining'  : 1 if is_raining else 0,  # Convert boolean to integer for server
                'rain_analog' : rain_analog,
                'co2_ppm'     : co2_ppm,
                'tvoc_ppb'    : tvoc_ppb,
                'baseline'    : baseline
            }

            # Add data to upload queue (non-blocking)
            try:
                data_queue.put(data, timeout=1)  # 1 second timeout
                print(f"[INFO] Data queued for upload (queue size: {data_queue.qsize()})")
            except queue.Full:
                print("[WARNING] Upload queue full, saving data locally")
                save_failed_data(data, failed_data_file)
                
        except Exception as e:
            print(f"[ERROR] Critical error in main loop: {e}")
            time.sleep(1)  # Short wait before retrying


if __name__ == "__main__":
    main()
