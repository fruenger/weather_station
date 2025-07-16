import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime, timedelta
import astropy.units as u

import matplotlib as mpl
mpl.rcParams['axes.formatter.useoffset'] = False

DATA_FILE = "data.dat"

data = np.loadtxt(DATA_FILE, unpack=True)

fig, axs = plt.subplots(
    figsize=(12,16),
    nrows=6,
    ncols=1,
    gridspec_kw={
        "hspace" : 0.,
        "wspace" : 0.4
    }
)

time, time_weird, temperature, pressure, humidity, ambient_light, precipitation_voltage, wind_rate, unassigned = data

with open(DATA_FILE, "r") as file:
    date, t = file.readline().split(" ")[3:5]

fig.suptitle("Weather Station Measurements\n" + date + " " + t, fontsize=40)

# define the datetime axis
times = datetime(*[int(i) for i in date.split("-")], *[int(i) for i in t.split(":")])

times = [times + timedelta(seconds=elapsed_time - time.max()) for elapsed_time in time]

# Temperature plot
ax = axs[0]
ax.plot(times, temperature)
ax.set_ylabel("Temperature\n[C]")
ax.grid()

# pressure_plot
ax = axs[1]
ax.plot(times, pressure/100.)
ax.set_ylabel("Pressure\n[hPa]")
ax.grid()

# humidity_plot
ax = axs[2]
ax.plot(times, humidity)
ax.set_ylabel("relative\nHumidity")
ax.grid()

# ambient_light
ax = axs[3]
ax.plot(times, np.convolve(ambient_light, np.full(10, 0.1), mode="same"))
ax.set_ylabel("Ambient\nLight\n[Lux]")
ax.set_ylim(0, 25)
ax.grid()

# precipitation voltage
ax = axs[4]
ax.plot(times, precipitation_voltage, color="gray")
ax.set_ylabel("Precipitation\nVoltage")
ax.grid()

# wind rate
ax = axs[5]
ax.plot(times, wind_rate, color="gray")
ax.set_ylabel("Anemometer\nRate\n[Hz]")
ax.set_xlabel("Time elapsed since measurement begin [s]")
ax.grid()

plt.savefig("measurements.pdf")
plt.show()