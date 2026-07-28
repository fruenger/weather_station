#!/usr/bin/python3
"""
Weather Station Configuration File

Works on Linux and Windows. On Windows, Unix chmod 600 does not apply —
use ACL helpers after writing secrets.
"""

import os
import json
import subprocess
import sys
import tempfile
from pathlib import Path

# Default configuration (HMAC-v1)
DEFAULT_CONFIG = {
    "server": {
        "url": "https://polaris.astro.physik.uni-potsdam.de/weather_station/weather_api/datasets/",
        "device_id": "",
        "key_id": "",
        "hmac_secret_hex": "",
        "canonical_path": "/weather_station/weather_api/datasets/"
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
    script_dir = Path(__file__).parent.absolute()
    return str(script_dir / "weather_station_config.json")


def _strip_cr(value):
    if isinstance(value, str):
        return value.replace('\r', '').strip()
    return value


def harden_config_permissions(path):
    """Restrict config file to the current user (POSIX mode or Windows ACL)."""
    try:
        if os.name == 'nt':
            user = os.environ.get('USERNAME') or os.environ.get('USER') or ''
            if user:
                # Remove inheritance and grant only the current user full control.
                subprocess.run(
                    ['icacls', path, '/inheritance:r'],
                    check=False,
                    capture_output=True,
                )
                subprocess.run(
                    ['icacls', path, '/grant:r', f'{user}:F'],
                    check=False,
                    capture_output=True,
                )
        else:
            os.chmod(path, 0o600)
    except Exception as exc:
        print(f"[WARNING] Could not harden config permissions: {exc}")


def create_default_config():
    config_path = get_config_path()
    if not os.path.exists(config_path):
        print(f"[INFO] Creating default configuration file: {config_path}")
        print("[INFO] Please edit the configuration file and add HMAC credentials!")
        atomic_write_json(config_path, DEFAULT_CONFIG)
    return DEFAULT_CONFIG


def atomic_write_json(path, config):
    directory = os.path.dirname(path) or '.'
    fd, tmp_path = tempfile.mkstemp(prefix='.weather_cfg_', dir=directory, text=True)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(config, f, indent=4, ensure_ascii=False)
            f.write('\n')
        os.replace(tmp_path, path)
        harden_config_permissions(path)
    finally:
        if os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass


def load_config():
    config_path = get_config_path()
    try:
        if os.path.exists(config_path):
            with open(config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)
                print(f"[INFO] Configuration loaded from: {config_path}")
                return config
        return create_default_config()
    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON in configuration file: {e}")
        return create_default_config()
    except Exception as e:
        print(f"[ERROR] Error loading configuration: {e}")
        return create_default_config()


def save_config(config):
    try:
        atomic_write_json(get_config_path(), config)
        print(f"[INFO] Configuration saved to: {get_config_path()}")
    except Exception as e:
        print(f"[ERROR] Error saving configuration: {e}")


def get_server_credentials():
    """
    Returns (device_id, key_id, secret_hex, url, canonical_path).

    Strips CR/LF from secrets to avoid Windows editor HMAC breakage.
    """
    config = load_config()
    server = config.get('server', {})
    device_id = _strip_cr(server.get('device_id', ''))
    key_id = _strip_cr(server.get('key_id', ''))
    secret_hex = _strip_cr(server.get('hmac_secret_hex', ''))
    url = _strip_cr(server.get(
        'url',
        'https://polaris.astro.physik.uni-potsdam.de/weather_station/weather_api/datasets/',
    )).rstrip('/') + '/'
    canonical_path = _strip_cr(server.get(
        'canonical_path',
        '/weather_station/weather_api/datasets/',
    ))

    if not device_id or not key_id or not secret_hex:
        print('[WARNING] Incomplete HMAC credentials! Edit the configuration file.')
        print(f'[INFO] Configuration file location: {get_config_path()}')
        if os.name == 'nt':
            print('[INFO] On Windows, keep system clock synchronized (Settings → Time).')

    return device_id, key_id, secret_hex, url, canonical_path


def get_arduino_port_search():
    config = load_config()
    return config.get('arduino', {}).get(
        'port_search',
        ['Arduino', 'ACM', 'CH340', 'USB-SERIAL'],
    )


def get_data_config():
    config = load_config()
    return config.get('data', {})


if __name__ == '__main__':
    print('Weather Station Configuration Test')
    print('=' * 40)
    device_id, key_id, secret_hex, url, path = get_server_credentials()
    print(f'Device: {device_id or "NOT SET"}')
    print(f'Key ID: {key_id or "NOT SET"}')
    print(f'Secret: {"*" * 8 if secret_hex else "NOT SET"}')
    print(f'URL: {url}')
    print(f'Canonical path: {path}')
    print(f'Platform: {sys.platform}')
    print(get_config_path())
