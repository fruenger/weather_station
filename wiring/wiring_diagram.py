#!/usr/bin/python3
"""
Weather Station Wiring Diagram Generator

This script creates a schematic wiring diagram showing how all sensors
are connected to the Arduino Uno for the weather station project.

Features:
- Visual representation of all sensor connections
- Color-coded wiring for different sensor types
- Pin assignments and descriptions
- Power and ground connections
- I2C bus connections with multiplexer

Author: Weather Station Project
Version: 4.0 (Updated for I2C Multiplexer and CCS811)
Date: 2024
"""

import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import FancyBboxPatch, Circle, Rectangle
import numpy as np

def create_wiring_diagram():
    """Create a comprehensive wiring diagram for the weather station."""
    
    # Create figure and axis
    fig, ax = plt.subplots(1, 1, figsize=(22, 18))
    ax.set_xlim(0, 22)
    ax.set_ylim(0, 18)
    ax.set_aspect('equal')
    
    # Remove axes
    ax.axis('off')
    
    # Title
    ax.text(11, 17.5, 'Weather Station Wiring Diagram', 
            fontsize=28, fontweight='bold', ha='center')
    ax.text(11, 17, 'Arduino Uno + I2C Multiplexer + Sensors + nRF24L01 Radio', 
            fontsize=18, ha='center', style='italic')
    
    # Draw components in a grid layout
    draw_arduino(ax, 11, 9)
    draw_multiplexer(ax, 11, 13)
    
    # Top row sensors (connected to multiplexer)
    draw_bme280(ax, 3, 15)
    draw_mlx90614(ax, 19, 15)
    draw_tsl2591(ax, 3, 11)
    draw_ccs811(ax, 19, 11)
    
    # Bottom row sensors (direct Arduino connection)
    draw_anemometer(ax, 3, 5)
    draw_rain_sensors(ax, 11, 3)
    draw_nrf24l01(ax, 19, 5)
    
    # Draw connections
    draw_i2c_connections(ax)
    draw_power_connections(ax)
    draw_signal_connections(ax)
    
    # Add legend
    draw_legend(ax)
    
    # Add notes
    draw_notes(ax)
    
    plt.tight_layout()
    return fig

def draw_arduino(ax, x, y):
    """Draw Arduino Uno board."""
    # Arduino board - much larger
    arduino = FancyBboxPatch((x-3, y-2), 6, 4, 
                            boxstyle="round,pad=0.3", 
                            facecolor='lightblue', 
                            edgecolor='darkblue', 
                            linewidth=4)
    ax.add_patch(arduino)
    
    # Arduino label
    ax.text(x, y+2.5, 'ARDUINO UNO', fontsize=18, fontweight='bold', ha='center')
    
    # Pin labels - Left side (larger and clearer)
    left_pins = [
        (x-2.5, y+1.5, '5V', 'red'),
        (x-2.5, y+1.0, '3.3V', 'orange'),
        (x-2.5, y+0.5, 'GND', 'black'),
        (x-2.5, y+0.0, 'GND', 'black'),
        (x-2.5, y-0.5, 'A4', 'purple'),  # SDA
        (x-2.5, y-1.0, 'A5', 'purple'),  # SCL
        (x-2.5, y-1.5, 'A1', 'green'),   # Rain analog
        (x-2.5, y-2.0, 'D2', 'brown'),   # Rain reed
        (x-2.5, y-2.5, 'D3', 'blue'),    # Wind
    ]
    
    # Pin labels - Right side
    right_pins = [
        (x+2.5, y+1.5, 'D4', 'gray'),    # Reset
        (x+2.5, y+1.0, 'D5', 'brown'),   # Rain digital
        (x+2.5, y+0.5, 'D8', 'yellow'),  # CSN
        (x+2.5, y+0.0, 'D9', 'yellow'),  # CE
        (x+2.5, y-0.5, 'D10', 'yellow'), # MOSI
        (x+2.5, y-1.0, 'D11', 'yellow'), # MISO
        (x+2.5, y-1.5, 'D12', 'yellow'), # SCK
        (x+2.5, y-2.0, 'D13', 'yellow'), # LED
    ]
    
    for px, py, label, color in left_pins + right_pins:
        ax.text(px, py, label, fontsize=14, ha='center', va='center',
                bbox=dict(boxstyle="round,pad=0.4", facecolor='white', edgecolor=color, linewidth=3))

def draw_multiplexer(ax, x, y):
    """Draw I2C Multiplexer (TCA9548A)."""
    multiplexer = FancyBboxPatch((x-2, y-0.8), 4, 1.6, 
                                boxstyle="round,pad=0.2", 
                                facecolor='lightgray', 
                                edgecolor='darkgray', 
                                linewidth=4)
    ax.add_patch(multiplexer)
    ax.text(x, y+1.0, 'TCA9548A', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'I2C Multiplexer', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'Address: 0x70', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.5, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.8, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x-0.1, y-0.5, 'SDA', fontsize=11, ha='center')
    ax.text(x+0.6, y-0.5, 'SCL', fontsize=11, ha='center')

def draw_bme280(ax, x, y):
    """Draw BME280 sensor."""
    sensor = FancyBboxPatch((x-1.5, y-0.8), 3, 1.6, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightgreen', 
                           edgecolor='darkgreen', 
                           linewidth=4)
    ax.add_patch(sensor)
    ax.text(x, y+1.0, 'BME280', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'Temp/Hum/Press', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'I2C: 0x76', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.0, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.3, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x+0.4, y-0.5, 'SDA', fontsize=11, ha='center')
    ax.text(x+1.1, y-0.5, 'SCL', fontsize=11, ha='center')

def draw_mlx90614(ax, x, y):
    """Draw MLX90614 infrared sensor."""
    sensor = FancyBboxPatch((x-1.5, y-0.8), 3, 1.6, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightcoral', 
                           edgecolor='darkred', 
                           linewidth=4)
    ax.add_patch(sensor)
    ax.text(x, y+1.0, 'MLX90614', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'IR Temperature', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'I2C: 0x5A', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.0, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.3, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x+0.4, y-0.5, 'SDA', fontsize=11, ha='center')
    ax.text(x+1.1, y-0.5, 'SCL', fontsize=11, ha='center')

def draw_tsl2591(ax, x, y):
    """Draw TSL2591 light sensor."""
    sensor = FancyBboxPatch((x-1.5, y-0.8), 3, 1.6, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightyellow', 
                           edgecolor='orange', 
                           linewidth=4)
    ax.add_patch(sensor)
    ax.text(x, y+1.0, 'TSL2591', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'Light Sensor', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'I2C: 0x29', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.0, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.3, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x+0.4, y-0.5, 'SDA', fontsize=11, ha='center')
    ax.text(x+1.1, y-0.5, 'SCL', fontsize=11, ha='center')

def draw_ccs811(ax, x, y):
    """Draw CCS811 air quality sensor."""
    sensor = FancyBboxPatch((x-1.5, y-0.8), 3, 1.6, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightcyan', 
                           edgecolor='darkcyan', 
                           linewidth=4)
    ax.add_patch(sensor)
    ax.text(x, y+1.0, 'CCS811', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'Air Quality', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'I2C: 0x5B', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.0, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.3, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x+0.4, y-0.5, 'SDA', fontsize=11, ha='center')
    ax.text(x+1.1, y-0.5, 'SCL', fontsize=11, ha='center')

def draw_anemometer(ax, x, y):
    """Draw anemometer."""
    sensor = FancyBboxPatch((x-1.5, y-0.8), 3, 1.6, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightblue', 
                           edgecolor='blue', 
                           linewidth=4)
    ax.add_patch(sensor)
    ax.text(x, y+1.0, 'ANEMOMETER', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.5, 'Wind Speed', fontsize=12, ha='center')
    ax.text(x, y+0.0, 'Hall Sensor', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.0, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.3, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x+0.4, y-0.5, 'SIG', fontsize=11, ha='center')

def draw_rain_sensors(ax, x, y):
    """Draw rain sensors."""
    # Reed sensor
    reed = FancyBboxPatch((x-2.5, y-0.8), 2.2, 1.6, 
                          boxstyle="round,pad=0.2", 
                          facecolor='lightgreen', 
                          edgecolor='green', 
                          linewidth=4)
    ax.add_patch(reed)
    ax.text(x-1.4, y+1.0, 'REED', fontsize=16, fontweight='bold', ha='center')
    ax.text(x-1.4, y+0.5, 'Rain Amount', fontsize=12, ha='center')
    ax.text(x-1.4, y+0.0, 'D2', fontsize=12, ha='center')
    
    # Rain drop sensor
    drop = FancyBboxPatch((x+0.3, y-0.8), 2.2, 1.6, 
                          boxstyle="round,pad=0.2", 
                          facecolor='lightcoral', 
                          edgecolor='red', 
                          linewidth=4)
    ax.add_patch(drop)
    ax.text(x+1.4, y+1.0, 'RAIN DROP', fontsize=16, fontweight='bold', ha='center')
    ax.text(x+1.4, y+0.5, 'Rain Detection', fontsize=12, ha='center')
    ax.text(x+1.4, y+0.0, 'A1/D5', fontsize=12, ha='center')

def draw_nrf24l01(ax, x, y):
    """Draw nRF24L01 radio module."""
    radio = FancyBboxPatch((x-2, y-0.6), 4, 1.2, 
                           boxstyle="round,pad=0.2", 
                           facecolor='lightgray', 
                           edgecolor='gray', 
                           linewidth=4)
    ax.add_patch(radio)
    ax.text(x, y+0.8, 'nRF24L01', fontsize=16, fontweight='bold', ha='center')
    ax.text(x, y+0.3, 'Radio Module', fontsize=12, ha='center')
    ax.text(x, y-0.2, 'SPI', fontsize=12, ha='center')
    
    # Pins
    ax.text(x-1.5, y-0.5, 'VCC', fontsize=11, ha='center')
    ax.text(x-0.8, y-0.5, 'GND', fontsize=11, ha='center')
    ax.text(x-0.1, y-0.5, 'CE', fontsize=11, ha='center')
    ax.text(x+0.6, y-0.5, 'CSN', fontsize=11, ha='center')

def draw_i2c_connections(ax):
    """Draw I2C bus connections with multiplexer."""
    # Main I2C bus from Arduino to multiplexer
    ax.plot([8.5, 8.5, 9, 9], [9, 12, 12, 12.2], 'purple', linewidth=5, alpha=0.8)
    ax.plot([9.5, 9.5, 10, 10], [9, 12, 12, 12.2], 'purple', linewidth=5, alpha=0.8)
    
    # Multiplexer to sensors
    # BME280 (Channel 0)
    ax.plot([9, 9, 1.5, 1.5], [12.2, 14, 14, 14.2], 'purple', linewidth=4, alpha=0.8)
    # MLX90614 (Channel 1)
    ax.plot([10, 10, 17.5, 17.5], [12.2, 14, 14, 14.2], 'purple', linewidth=4, alpha=0.8)
    # TSL2591 (Channel 2)
    ax.plot([9, 9, 1.5, 1.5], [12.2, 10, 10, 10.2], 'purple', linewidth=4, alpha=0.8)
    # CCS811 (Channel 3)
    ax.plot([10, 10, 17.5, 17.5], [12.2, 10, 10, 10.2], 'purple', linewidth=4, alpha=0.8)
    
    # I2C labels
    ax.text(11, 11.5, 'I2C Bus (SDA/SCL)', fontsize=14, color='purple', fontweight='bold')
    ax.text(11, 11, 'TCA9548A Multiplexer', fontsize=14, color='purple', fontweight='bold')

def draw_power_connections(ax):
    """Draw power and ground connections with multiplexer."""
    # 5V line (red) - from Arduino to sensors
    # BME280 5V
    ax.plot([8.5, 8.5, 1.5, 1.5], [8.5, 13.5, 13.5, 13.8], 'red', linewidth=4, alpha=0.8)
    # MLX90614 5V
    ax.plot([8.5, 8.5, 17.5, 17.5], [8.5, 13.5, 13.5, 13.8], 'red', linewidth=4, alpha=0.8)
    # TSL2591 5V
    ax.plot([8.5, 8.5, 1.5, 1.5], [8.5, 9.5, 9.5, 9.8], 'red', linewidth=4, alpha=0.8)
    # CCS811 5V
    ax.plot([8.5, 8.5, 17.5, 17.5], [8.5, 9.5, 9.5, 9.8], 'red', linewidth=4, alpha=0.8)
    # Anemometer 5V
    ax.plot([8.5, 8.5, 1.5, 1.5], [8.5, 4.5, 4.5, 4.8], 'red', linewidth=4, alpha=0.8)
    # nRF24L01 5V
    ax.plot([8.5, 8.5, 17.5, 17.5], [8.5, 4.5, 4.5, 4.8], 'red', linewidth=4, alpha=0.8)
    
    # 3.3V line (orange) - from Arduino to sensors
    # BME280 3.3V
    ax.plot([9.5, 9.5, 2.5, 2.5], [8.5, 13.5, 13.5, 13.8], 'orange', linewidth=4, alpha=0.8)
    # MLX90614 3.3V
    ax.plot([9.5, 9.5, 16.5, 16.5], [8.5, 13.5, 13.5, 13.8], 'orange', linewidth=4, alpha=0.8)
    # TSL2591 3.3V
    ax.plot([9.5, 9.5, 2.5, 2.5], [8.5, 9.5, 9.5, 9.8], 'orange', linewidth=4, alpha=0.8)
    # CCS811 3.3V
    ax.plot([9.5, 9.5, 16.5, 16.5], [8.5, 9.5, 9.5, 9.8], 'orange', linewidth=4, alpha=0.8)
    
    # GND line (black) - from Arduino to sensors
    # BME280 GND
    ax.plot([9.0, 9.0, 2.0, 2.0], [8.5, 13.5, 13.5, 13.8], 'black', linewidth=4, alpha=0.8)
    # MLX90614 GND
    ax.plot([9.0, 9.0, 17.0, 17.0], [8.5, 13.5, 13.5, 13.8], 'black', linewidth=4, alpha=0.8)
    # TSL2591 GND
    ax.plot([9.0, 9.0, 2.0, 2.0], [8.5, 9.5, 9.5, 9.8], 'black', linewidth=4, alpha=0.8)
    # CCS811 GND
    ax.plot([9.0, 9.0, 17.0, 17.0], [8.5, 9.5, 9.5, 9.8], 'black', linewidth=4, alpha=0.8)
    # Anemometer GND
    ax.plot([9.0, 9.0, 2.0, 2.0], [8.5, 4.5, 4.5, 4.8], 'black', linewidth=4, alpha=0.8)
    # nRF24L01 GND
    ax.plot([9.0, 9.0, 17.0, 17.0], [8.5, 4.5, 4.5, 4.8], 'black', linewidth=4, alpha=0.8)
    
    # Power labels
    ax.text(11, 9.5, '5V', fontsize=14, color='red', fontweight='bold')
    ax.text(11, 9, '3.3V', fontsize=14, color='orange', fontweight='bold')
    ax.text(11, 8.5, 'GND', fontsize=14, color='black', fontweight='bold')

def draw_signal_connections(ax):
    """Draw signal connections with simple straight lines."""
    # Wind sensor (blue) - D3 to anemometer
    ax.plot([8.5, 8.5, 1.5, 1.5], [8.5, 6, 6, 4.2], 'blue', linewidth=4, alpha=0.8)
    
    # Rain reed (brown) - D2 to reed sensor
    ax.plot([9.5, 9.5, 8.5, 8.5], [8.5, 3, 3, 1.2], 'brown', linewidth=4, alpha=0.8)
    
    # Rain drop analog (green) - A1 to rain drop sensor
    ax.plot([8.5, 8.5, 10.5, 10.5], [8.5, 3, 3, 1.2], 'green', linewidth=4, alpha=0.8)
    
    # Rain drop digital (brown) - D5 to rain drop sensor
    ax.plot([9.0, 9.0, 11.5, 11.5], [8.5, 3, 3, 1.2], 'brown', linewidth=4, alpha=0.8)
    
    # SPI connections (yellow) - D8-D13 to nRF24L01
    # CSN (D8)
    ax.plot([12.5, 12.5, 8.5, 8.5], [9, 4.5, 4.5, 4.8], 'yellow', linewidth=4, alpha=0.8)
    # CE (D9)
    ax.plot([13.0, 13.0, 9.0, 9.0], [9, 4.5, 4.5, 4.8], 'yellow', linewidth=4, alpha=0.8)
    # MOSI (D10)
    ax.plot([13.5, 13.5, 9.5, 9.5], [9, 4.5, 4.5, 4.8], 'yellow', linewidth=4, alpha=0.8)
    # MISO (D11)
    ax.plot([14.0, 14.0, 10.0, 10.0], [9, 4.5, 4.5, 4.8], 'yellow', linewidth=4, alpha=0.8)
    # SCK (D12)
    ax.plot([14.5, 14.5, 10.5, 10.5], [9, 4.5, 4.5, 4.8], 'yellow', linewidth=4, alpha=0.8)
    
    # Signal labels
    ax.text(11, 6.5, 'D3 (Wind)', fontsize=14, color='blue', fontweight='bold')
    ax.text(11, 6, 'D2 (Reed)', fontsize=14, color='brown', fontweight='bold')
    ax.text(11, 5.5, 'A1 (Rain Analog)', fontsize=14, color='green', fontweight='bold')
    ax.text(11, 5, 'D5 (Rain Digital)', fontsize=14, color='brown', fontweight='bold')
    ax.text(11, 4.5, 'SPI (D8-D13)', fontsize=14, color='yellow', fontweight='bold')

def draw_legend(ax):
    """Draw color legend."""
    legend_items = [
        ('I2C Bus (SDA/SCL)', 'purple'),
        ('5V Power', 'red'),
        ('3.3V Power', 'orange'),
        ('Ground', 'black'),
        ('Digital Signal', 'blue'),
        ('Analog Signal', 'green'),
        ('SPI Bus', 'yellow'),
    ]
    
    y_start = 1.0
    for i, (label, color) in enumerate(legend_items):
        ax.plot([1.0, 2.0], [y_start - i*0.5, y_start - i*0.5], 
                color=color, linewidth=5, alpha=0.8)
        ax.text(2.3, y_start - i*0.5, label, fontsize=14, va='center', fontweight='bold')

def draw_notes(ax):
    """Draw important notes."""
    notes = [
        "NOTES:",
        "• I2C Multiplexer (TCA9548A): Address 0x70",
        "• I2C Addresses: BME280=0x76, MLX90614=0x5A, TSL2591=0x29, CCS811=0x5B",
        "• Rain Reed Sensor: Counts tips for rain amount (D2)",
        "• Rain Drop Sensor: Binary detection (A1 + D5)",
        "• Anemometer: Hall sensor on D3 (interrupt)",
        "• nRF24L01: SPI communication (D8-D13)",
        "• CCS811: Air quality sensor (CO2, TVOC)",
        "• All sensors have pull-up resistors on I2C lines",
        "• Use external 3.3V supply for stable operation",
        "• Wind sensor requires interrupt pin (D3)",
        "• I2C multiplexer allows future sensor expansion"
    ]
    
    for i, note in enumerate(notes):
        ax.text(1.0, 10.5 - i*0.5, note, fontsize=13, 
                fontweight='bold' if i == 0 else 'normal')

def save_diagram(filename='weather_station_wiring.png', dpi=300):
    """Create and save the wiring diagram."""
    fig = create_wiring_diagram()
    fig.savefig(filename, dpi=dpi, bbox_inches='tight')
    print(f"Wiring diagram saved as: {filename}")
    plt.close(fig)

def show_diagram():
    """Create and display the wiring diagram."""
    fig = create_wiring_diagram()
    plt.show()
    return fig

if __name__ == "__main__":
    print("Weather Station Wiring Diagram Generator")
    print("Version 4.0 - I2C Multiplexer + CCS811")
    print("=" * 40)
    
    # Create and save the diagram
    save_diagram()
    
    # Also show it interactively
    show_diagram()
    
    print("\nDiagram features:")
    print("• Color-coded connections")
    print("• Pin assignments")
    print("• I2C bus layout with multiplexer")
    print("• Power distribution")
    print("• Sensor locations")
    print("• Important notes") 