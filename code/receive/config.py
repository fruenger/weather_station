#!/usr/bin/python3
"""
Weather Station Configuration File

This file contains configuration settings for the weather station data receiver.
The configuration is designed to work on both Linux and Windows systems.

Author: Weather Station Project
Version: 2.0
Date: 2025-07-16
"""

import os
import json
from pathlib import Path

# Default configuration
DEFAULT_CONFIG = {
    "server": {
        "url": "https://polaris.astro.physik.uni-potsdam.de/weather_station/weather_api/datasets/",
        "username": "data_upload_user",
        "password": ""
    },
    "arduino": {
        "port_search": ["Arduino", "ACM", "CH340", "USB-SERIAL", "USB2.0-Ser!"]
    },
    "data": {
        "queue_size": 100,
        "failed_data_file": "failed_uploads.csv",
        "timeout": 10
    }
}

def get_config_path():
    """Get the path to the configuration file.
    
    Returns:
        str: Path to the configuration file
    """
    # Get the directory where this script is located
    script_dir = Path(__file__).parent.absolute()
    config_file = script_dir / "weather_station_config.json"
    return str(config_file)

def create_default_config():
    """Create a default configuration file if it doesn't exist.
    
    Returns:
        dict: The default configuration
    """
    config_path = get_config_path()
    
    if not os.path.exists(config_path):
        print(f"[INFO] Creating default configuration file: {config_path}")
        print("[INFO] Please edit the configuration file and add your password!")
        
        with open(config_path, 'w', encoding='utf-8') as f:
            json.dump(DEFAULT_CONFIG, f, indent=4, ensure_ascii=False)
    
    return DEFAULT_CONFIG

def load_config():
    """Load configuration from file.
    
    Returns:
        dict: Configuration dictionary
    """
    config_path = get_config_path()
    
    try:
        if os.path.exists(config_path):
            with open(config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)
                print(f"[INFO] Configuration loaded from: {config_path}")
                return config
        else:
            print(f"[WARNING] Configuration file not found: {config_path}")
            return create_default_config()
            
    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON in configuration file: {e}")
        print("[INFO] Creating new configuration file...")
        return create_default_config()
    except Exception as e:
        print(f"[ERROR] Error loading configuration: {e}")
        return create_default_config()

def save_config(config):
    """Save configuration to file.
    
    Args:
        config (dict): Configuration dictionary to save
    """
    config_path = get_config_path()
    
    try:
        with open(config_path, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=4, ensure_ascii=False)
        print(f"[INFO] Configuration saved to: {config_path}")
    except Exception as e:
        print(f"[ERROR] Error saving configuration: {e}")

def get_server_credentials():
    """Get server credentials from configuration.
    
    Returns:
        tuple: (username, password, url)
    """
    config = load_config()
    
    server_config = config.get("server", {})
    username = server_config.get("username", "data_upload_user")
    password = server_config.get("password", "")
    url = server_config.get("url", "https://polaris.astro.physik.uni-potsdam.de/weather_station/weather_api/datasets/")
    
    if not password:
        print("[WARNING] No password configured! Please edit the configuration file.")
        print(f"[INFO] Configuration file location: {get_config_path()}")
    
    return username, password, url

def get_arduino_port_search():
    """Get Arduino port search terms.
    
    Returns:
        list: List of search terms for Arduino ports
    """
    config = load_config()
    arduino_config = config.get("arduino", {})
    return arduino_config.get("port_search", ["Arduino", "ACM", "CH340", "USB-SERIAL"])

def get_data_config():
    """Get data processing configuration.
    
    Returns:
        dict: Data configuration dictionary
    """
    config = load_config()
    return config.get("data", {})

# Test function
if __name__ == "__main__":
    print("Weather Station Configuration Test")
    print("=" * 40)
    
    # Test configuration loading
    config = load_config()
    print(f"Configuration loaded: {bool(config)}")
    
    # Test server credentials
    username, password, url = get_server_credentials()
    print(f"Username: {username}")
    print(f"Password: {'*' * len(password) if password else 'NOT SET'}")
    print(f"URL: {url}")
    
    # Test Arduino port search
    port_search = get_arduino_port_search()
    print(f"Arduino port search terms: {port_search}")
    
    # Test data config
    data_config = get_data_config()
    print(f"Data configuration: {data_config}")
    
    print("\nConfiguration file location:")
    print(get_config_path()) 