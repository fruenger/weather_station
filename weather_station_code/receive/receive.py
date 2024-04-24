#!/usr/bin/python


# A program to read the data of the BMP Pressure/Temperature/Humidity Arduino Sensor via a serial port. It provides 

from serial import Serial
from serial.tools import list_ports
import time
import numpy as np
from datetime import datetime, date
import struct
import requests

import time
import datetime
from astropy.time import Time


PORT_NAME = "USB-SERIAL CH340" # the correct port descriptor. The Arduino uses a CH340 chipset for the communication. Even though it doesn't tell about the existence of an arduino the presence of this chipset is somewhat indicative.


def find_port(port_name):
    """This function finds the correct com port that matches the description provided in the function argument.

    Args:
        port_name (str): The desired COM port's name.
    
    Returns:
        Com port object. Its USB description can be accessed with its "usb_description()" method.
    """
    
    # get names of all attached ports
    all_comports = list_ports.comports()
    all_port_descriptions = [portinfo.description for portinfo in all_comports]
    
    for i, description in enumerate(all_port_descriptions):
        
        if description.find(port_name) > -1:
            
            comport = all_comports[i]
            break
    
    return comport


def save_to_file(data_array, fname):
    """A helper function that eases the process of saving data tables that have been taken with the BMP sensor.

    Args:
        data_array (ndarray): The data array
        fname (str): The filename in which the data is to be stored
    """
    
    if data_array.shape[1] == 9:
        
        header = "Measurement begin: " + str(date.today()) + " " + datetime.now().strftime("%H:%M:%S") \
            + "\ntimestamp transID temperature pressure humidity amblight precipitation windintrate unassigned"
        
    elif data_array.shape[1] == 8:
        
        header = "Measurement begin: " + str(date.today()) + " " + datetime.now().strftime("%H:%M:%S") + "\ntemperature pressure humidity"
    
    np.savetxt(
        fname,
        data_array,
        fmt="%.2f",
        header=header
    )


def readline(port_name, timestamp=True):
    """A function that reads out the com port for ONE row of data. It will connect and disconnect from the serial port before and aftwards, respectively. If you rather want to stay connected and read the data continuously, use the function "" which stays connected while waiting/read multiple data rows.

    Args:
        port_name (str): The port name to connect to.
        timestamp (bool, optional): Choose wether a timestamp should be added to the data row. If enabled the result of this function will be atuple with the date-time string along with the instrument readings in its second entry. Defaults to True.
    """
    comport = find_port(port_name)
    
    with Serial(comport.usb_description()) as serial:
        
        data = struct.unpack('8f', serial.read(size=32)) # each serial transmission comes with in packets of 32 bits, encoding 8 floating point values
    
    if timestamp:
        
        return (str(date.today()) + " " + datetime.now().strftime("%H:%M:%S"), data)
    
    else:
            
        return data


def readlines(port_name, Nlines, timestamp=True, output=""):
    """A function that reads Nlines before disconnecting from the serial port again.

    Args:
        port_name (str): The COM port's name to connect to
        Nlines (int): Numer of lines to be transmitted before the disconnection is issued
        timestamp (bool, optional): Choose wether a timestamp should be added to each data row. Defaults to True.
    """
    
    start_time = time.time()
    comport = find_port(port_name)
    
    with Serial(comport.usb_description()) as serial:
        
        data_table = []
        for i in range(Nlines):
            
            data_row = struct.unpack('8f', serial.read(size=32)) # each serial transmission comes with in packets of 32 bits, encoding 8 floating point values
        
            if timestamp:
                
                data_row = np.insert(data_row, 0, time.time() - start_time)
            
            data_table.append(data_row)
            print(data_row)

    data_table = np.array(data_table)

    if output != "":
        
        save_to_file(data_table, output)

    return data_table


def read_until(port_name, duration, timestamp=True, output=""):
    """A function that takes measurements as long as specified with the duration keyword in the function call.

    Args:
        port_name (str): The COM port's name to connect to
        duration (float): The duratio of the measurement (given in seconds)
        timestamp (bool, optional): Choose wether a timestamp should be added to each data row. Defaults to True.
    """

    start_time = time.time()
    end_time = start_time + duration
    comport = find_port(port_name)
    
    with Serial(comport.usb_description()) as serial:
        
        data_table = []
        while time.time() < end_time:
            
            data_row = struct.unpack('8f', serial.read(size=32)) # each serial transmission comes with in packets of 32 bits, encoding 8 floating point values
        
            if timestamp:
                
                data_row = np.insert(data_row, 0, time.time() - start_time)
            
            data_table.append(data_row)
            print(data_row)
    
    data_table = np.array(data_table)
    
    if output != "":
        
        save_to_file(data_table, output)
    
    return data_table


if __name__ == "__main__":
    
    # Initialise environment variables

    URL = "https://polaris.astro.physik.uni-potsdam.de/weather_station/weather_api/datasets/"

    username="data_upload_user"
    password=r""
    
    print("[INFO] Starting measurement ...")
    comport = find_port(PORT_NAME)
    print(comport)
    
    # Starting to log the data and uploading them ...
    while True:
        data_table = readline(comport.usb_description(), timestamp=False)
        print("[INFO] Measurement completed!")
        _, temperature, pressure, humidity, illuminance, rain, wind_speed, _ = data_table
        print(data_table)
        
        # Prepare data
        jd = Time(datetime.datetime.now(datetime.timezone.utc)).jd
        data = {
            'jd'          :jd,
            'temperature' : temperature,
            'pressure'    : pressure/100.,
            'humidity'    : humidity,
            'illuminance' : illuminance,
            'wind_speed'  : wind_speed,
            'rain'        : rain
        }

        #   Add data
        try:
            response = requests.post(URL, auth=(username, password), data=data)
            print(response.status_code)
        except:
            print("Could not upload data to the database server ...")
