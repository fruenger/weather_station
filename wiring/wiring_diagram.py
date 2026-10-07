#!/usr/bin/env python3
"""
Wiring diagram of the OST weather station (Arduino UNO R4 WiFi).

One description of components, pins and connections is rendered three ways:

  wiring_r4_wifi.svg   static drawing (for docs, printing)
  wiring_r4_wifi.png   same drawing as bitmap (needs matplotlib; PNGs are gitignored)
  wiring_r4_wifi.html  interactive page: hover a wire or pin to highlight its net,
                       click a component for its pin table, full connection list

Usage:
    python3 wiring_diagram.py            # all outputs
    python3 wiring_diagram.py --no-png   # without matplotlib

Power is drawn with net labels (5V, 3V3, GND) instead of wires: every pin with
the same label is connected to the same rail. Signal and I2C lines are drawn.
Keep this file in sync with pin_connections_r4_wifi.txt and the firmware.
"""
import argparse
import html
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASENAME = 'wiring_r4_wifi'
WIDTH, HEIGHT = 1740, 1150
TITLE = 'OST Weather Station — Wiring (Arduino UNO R4 WiFi)'
SUBTITLE = 'Firmware v1.7 · pins as in weather_station_r4_wifi.ino · same net label = same rail'

# ---------------------------------------------------------------------------
# Nets
# ---------------------------------------------------------------------------
SDA_COLOR, SCL_COLOR = '#2a78d6', '#159a6a'
NETS = {
    'VIN': dict(label='VIN 6–24 V', color='#8e2b2b', kind='power',
                note='Supply input: solar/battery via regulator, or USB 5 V on the bench.'),
    '5V': dict(label='5 V rail', color='#d0342c', kind='power',
               note='Board 5 V pin. Multiplexer, all sensors except BME280, rain sensors, anemometer.'),
    '3V3': dict(label='3.3 V', color='#e07b00', kind='power',
                note='Board 3.3 V pin. Only BME280 and the low-voltage side of its level shifter.'),
    'GND': dict(label='GND', color='#2b2b2b', kind='ground', note='Common ground of all parts.'),
    'SDA': dict(label='I²C SDA (main bus)', color=SDA_COLOR, kind='i2c', note='A4 → TCA9548A, 5 V logic.'),
    'SCL': dict(label='I²C SCL (main bus)', color=SCL_COLOR, kind='i2c', note='A5 → TCA9548A, 5 V logic.'),
    'D2': dict(label='D2 rain gauge', color='#4a3aa7', kind='signal',
               note='Tipping-bucket reed contact; pull-up on the module, debounced in firmware (250 ms).'),
    'D3': dict(label='D3 anemometer', color='#c2457a', kind='signal',
               note='Hall sensor pulses, interrupt on RISING edge.'),
    'D5': dict(label='D5 rain drop (digital)', color='#b07a00', kind='signal',
               note='Comparator output of the rain drop module: rain yes/no.'),
    'A1': dict(label='A1 rain drop (analog)', color='#2f7d1f', kind='signal',
               note='Analog output of the rain drop module (diagnostics).'),
    'D6': dict(label='D6 PMSA003I SET', color='#e2622a', kind='signal',
               note='LOW = sleep, HIGH = active; firmware measures every 5 min.'),
    'D4': dict(label='D4 → RESET', color='#795548', kind='signal',
               note='Hourly self-reset: D4 is an input during operation and is switched to '
                    'output LOW to pull RESET (firmware checkAutoReset()).'),
}
for ch, sensor in [(0, 'PMSA003I'), (1, 'BME280 (via level shifter)'), (2, 'SEN0636'),
                   (3, 'MLX90614'), (4, 'TSL2591')]:
    NETS[f'SD{ch}'] = dict(label=f'Channel {ch} SDA', color=SDA_COLOR, kind='i2c',
                           note=f'TCA9548A channel {ch} → {sensor}.')
    NETS[f'SC{ch}'] = dict(label=f'Channel {ch} SCL', color=SCL_COLOR, kind='i2c',
                           note=f'TCA9548A channel {ch} → {sensor}.')
NETS['LSDA'] = dict(label='BME280 SDA (3.3 V side)', color=SDA_COLOR, kind='i2c',
                    note='Level shifter low-voltage side → BME280.')
NETS['LSCL'] = dict(label='BME280 SCL (3.3 V side)', color=SCL_COLOR, kind='i2c',
                    note='Level shifter low-voltage side → BME280.')

# ---------------------------------------------------------------------------
# Components: geometry (x, y, w, h), text lines and pins
# Pins: (id, label, side, position along the side in px from the box corner)
# ---------------------------------------------------------------------------
SKY = 'sky-facing'
COMPONENTS = {
    'psu': dict(title='Power supply', lines=['6–24 V DC', 'solar / battery', 'via regulator'],
                box=(40, 220, 190, 130), kind='power',
                pins=[('+', '+', 'right', 45), ('-', '−', 'bottom', 95)],
                info='Feeds VIN. For bench tests the board can run from USB instead.'),
    'r4': dict(title='Arduino UNO R4 WiFi', lines=['5 V logic', 'WiFi: on-board ESP32-S3', '', 'Firmware v1.7'],
               box=(330, 220, 260, 440), kind='mcu',
               pins=[('VIN', 'VIN', 'left', 45), ('5V', '5V', 'left', 105), ('3V3', '3.3V', 'left', 155),
                     ('GND', 'GND', 'left', 205),
                     ('D6', 'D6', 'right', 50), ('A4', 'A4 / SDA', 'right', 120), ('A5', 'A5 / SCL', 'right', 160),
                     ('D2', 'D2', 'bottom', 30), ('D3', 'D3', 'bottom', 75), ('D5', 'D5', 'bottom', 120),
                     ('A1', 'A1', 'bottom', 165), ('D4', 'D4', 'bottom', 205), ('RST', 'RESET', 'bottom', 238)],
               info='Reads all sensors and uploads every 60 s via HTTPS. D4 is wired to RESET for the '
                    'hourly self-reset; it stays an input until the reset (never driven HIGH).'),
    'tca': dict(title='TCA9548A', lines=['I²C multiplexer', 'address 0x70'],
                box=(780, 300, 200, 400), kind='mux',
                pins=[('SDA', 'SDA', 'left', 40), ('SCL', 'SCL', 'left', 80), ('VIN', 'VIN', 'left', 160),
                      ('GND', 'GND', 'left', 200), ('ADDR', 'A0–A2', 'left', 240)]
                + [p for ch in range(5) for p in (
                    (f'SD{ch}', f'SD{ch}', 'right', 30 + 80 * ch), (f'SC{ch}', f'SC{ch}', 'right', 60 + 80 * ch))],
                info='Only one channel is switched on at a time; the firmware selects the channel '
                     'before each sensor access. Address pins A0–A2 to GND give 0x70.'),
    'lvl': dict(title='Level shifter', lines=['5 V ↔ 3.3 V'],
                box=(1110, 240, 150, 160), kind='helper',
                pins=[('HV1', 'HV1', 'left', 70), ('HV2', 'HV2', 'left', 100),
                      ('LV1', 'LV1', 'right', 70), ('LV2', 'LV2', 'right', 100),
                      ('HV', 'HV', 'bottom', 30), ('GND', 'GND', 'bottom', 75), ('LV', 'LV', 'bottom', 120)],
                info='Bidirectional I²C level shifter (MOSFET type) between multiplexer channel 1 (5 V) '
                     'and the 3.3 V-only BME280.'),
    'pms': dict(title='PMSA003I', lines=['Particulate matter', '0x12 · channel 0', 'fan needs 5 V'],
                box=(1340, 90, 310, 150), kind='sensor',
                pins=[('SET', 'SET', 'left', 35), ('SDA', 'SDA', 'left', 75), ('SCL', 'SCL', 'left', 105),
                      ('VIN', 'VIN', 'right', 55), ('GND', 'GND', 'right', 95)],
                info='Sleeps between measurements (SET = LOW); 60–120 mA while the fan runs. '
                     'RESET pin not connected (internal pull-up).'),
    'bme': dict(title='BME280', lines=['Temp. / humidity / pressure', '0x76 · channel 1', '3.3 V only'],
                box=(1340, 270, 310, 110), kind='sensor',
                pins=[('SDA', 'SDA', 'left', 40), ('SCL', 'SCL', 'left', 70),
                      ('VIN', 'VIN', 'right', 40), ('GND', 'GND', 'right', 75)],
                info='Mount in the radiation shield, away from the warm electronics box.'),
    'uv': dict(title='SEN0636 UV', lines=['UV index', '0x23 · channel 2', f'{SKY} (quartz window)'],
               box=(1340, 450, 310, 110), kind='sensor',
               pins=[('DR', 'D/R (SDA)', 'left', 40), ('CT', 'C/T (SCL)', 'left', 70),
                     ('VCC', 'VCC', 'right', 40), ('GND', 'GND', 'right', 75)],
               info='DFRobot Gravity module; the mode switch must be set to I²C (switch only without power).'),
    'mlx': dict(title='MLX90614', lines=['IR sky temperature', '0x5A · channel 3', f'{SKY} (open view)'],
                box=(1340, 600, 310, 110), kind='sensor',
                pins=[('SDA', 'SDA', 'left', 40), ('SCL', 'SCL', 'left', 70),
                      ('VIN', 'VIN', 'right', 40), ('GND', 'GND', 'right', 75)],
                info='Object temperature = sky, die temperature = sensor body. Needs a free view of the sky '
                     '(no plexiglass: it blocks thermal infrared).'),
    'tsl': dict(title='TSL2591', lines=['Illuminance', '0x29 · channel 4', SKY],
                box=(1340, 750, 310, 110), kind='sensor',
                pins=[('SDA', 'SDA', 'left', 40), ('SCL', 'SCL', 'left', 70),
                      ('VCC', 'VCC', 'right', 40), ('GND', 'GND', 'right', 75)],
                info='Auto-ranging driver (firmware ≥ 1.6), Adafruit lux formula (≥ 1.7).'),
    'reed': dict(title='Rain gauge', lines=['reed module', 'pull-up on module', '1.25 mm per tip'],
                 box=(60, 820, 200, 130), kind='sensor',
                 pins=[('DO', 'OUT', 'top', 140), ('VCC', 'VCC', 'bottom', 60), ('GND', 'GND', 'bottom', 140)],
                 info='Tipping bucket with reed contact; tips are counted per 500 ms snapshot.'),
    'wind': dict(title='Anemometer', lines=['Hall sensor', 'pulses → interrupt'],
                 box=(300, 820, 200, 130), kind='sensor',
                 pins=[('SIG', 'SIG', 'top', 120), ('VCC', 'VCC', 'bottom', 60), ('GND', 'GND', 'bottom', 140)],
                 info='Raw revolutions per 500 ms are uploaded; the server converts to m/s.'),
    'drop': dict(title='Rain drop sensor', lines=['module with comparator'],
                 box=(540, 820, 230, 130), kind='sensor',
                 pins=[('DO', 'DO', 'top', 60), ('AO', 'AO', 'top', 120), ('VCC', 'VCC', 'bottom', 80),
                       ('GND', 'GND', 'bottom', 160)],
                 info='Digital output for rain yes/no, analog output for diagnostics.'),
}

# Power/ground net labels at pins: (component, pin, net)
FLAGS = [
    ('psu', '-', 'GND'),
    ('r4', '5V', '5V'), ('r4', '3V3', '3V3'), ('r4', 'GND', 'GND'),
    ('tca', 'VIN', '5V'), ('tca', 'GND', 'GND'), ('tca', 'ADDR', 'GND'),
    ('lvl', 'HV', '5V'), ('lvl', 'GND', 'GND'), ('lvl', 'LV', '3V3'),
    ('pms', 'VIN', '5V'), ('pms', 'GND', 'GND'),
    ('bme', 'VIN', '3V3'), ('bme', 'GND', 'GND'),
    ('uv', 'VCC', '5V'), ('uv', 'GND', 'GND'),
    ('mlx', 'VIN', '5V'), ('mlx', 'GND', 'GND'),
    ('tsl', 'VCC', '5V'), ('tsl', 'GND', 'GND'),
    ('reed', 'VCC', '5V'), ('reed', 'GND', 'GND'),
    ('wind', 'VCC', '5V'), ('wind', 'GND', 'GND'),
    ('drop', 'VCC', '5V'), ('drop', 'GND', 'GND'),
]
NOT_CONNECTED = []


def pin_pos(comp, pin):
    x, y, w, h = COMPONENTS[comp]['box']
    for pid, _, side, off in COMPONENTS[comp]['pins']:
        if pid == pin:
            return {'left': (x, y + off), 'right': (x + w, y + off),
                    'top': (x + off, y), 'bottom': (x + off, y + h)}[side], side
    raise KeyError((comp, pin))


def route(a, b, xs=(), ys=()):
    """Orthogonal polyline from pin a to pin b.

    ``xs``/``ys`` alternate: first a vertical lane at xs[0] (for horizontal
    starts) or a horizontal lane at ys[0] (for vertical starts), and so on.
    """
    (x0, y0), side = pin_pos(*a)
    (x1, y1), _ = pin_pos(*b)
    pts = [(x0, y0)]
    if side in ('left', 'right'):
        if not xs:
            if y0 != y1:
                raise ValueError(f'{a} -> {b}: pins not aligned, give a lane')
            return pts + [(x1, y1)]
        x = xs[0]
        pts += [(x, y0)]
        if ys:
            pts += [(x, ys[0]), (xs[1], ys[0]), (xs[1], y1)]
        else:
            pts += [(x, y1)]
    else:
        y = ys[0]
        pts += [(x0, y), (x1, y)]
    pts.append((x1, y1))
    return pts


# Signal/I2C wires: (net, from (comp, pin), to (comp, pin), routing lanes)
UP_LANES = {'SD0': 1000, 'SC0': 1012, 'SD1': 1024, 'SC1': 1036}
DOWN_LANES = {'SC4': 1000, 'SD4': 1012, 'SC3': 1024, 'SD3': 1036}
WIRES = [
    ('VIN', ('psu', '+'), ('r4', 'VIN'), {}),
    ('SDA', ('r4', 'A4'), ('tca', 'SDA'), {}),
    ('SCL', ('r4', 'A5'), ('tca', 'SCL'), {}),
    ('D6', ('r4', 'D6'), ('pms', 'SET'), dict(xs=(650, 1320), ys=(62,))),
    ('D4', ('r4', 'D4'), ('r4', 'RST'), dict(ys=(690,))),
    ('SD0', ('tca', 'SD0'), ('pms', 'SDA'), dict(xs=(UP_LANES['SD0'],))),
    ('SC0', ('tca', 'SC0'), ('pms', 'SCL'), dict(xs=(UP_LANES['SC0'],))),
    ('SD1', ('tca', 'SD1'), ('lvl', 'HV1'), dict(xs=(UP_LANES['SD1'],))),
    ('SC1', ('tca', 'SC1'), ('lvl', 'HV2'), dict(xs=(UP_LANES['SC1'],))),
    ('LSDA', ('lvl', 'LV1'), ('bme', 'SDA'), {}),
    ('LSCL', ('lvl', 'LV2'), ('bme', 'SCL'), {}),
    ('SD2', ('tca', 'SD2'), ('uv', 'DR'), {}),
    ('SC2', ('tca', 'SC2'), ('uv', 'CT'), {}),
    ('SD3', ('tca', 'SD3'), ('mlx', 'SDA'), dict(xs=(DOWN_LANES['SD3'],))),
    ('SC3', ('tca', 'SC3'), ('mlx', 'SCL'), dict(xs=(DOWN_LANES['SC3'],))),
    ('SD4', ('tca', 'SD4'), ('tsl', 'SDA'), dict(xs=(DOWN_LANES['SD4'],))),
    ('SC4', ('tca', 'SC4'), ('tsl', 'SCL'), dict(xs=(DOWN_LANES['SC4'],))),
    ('D2', ('r4', 'D2'), ('reed', 'DO'), dict(ys=(765,))),
    ('D3', ('r4', 'D3'), ('wind', 'SIG'), dict(ys=(785,))),
    ('D5', ('r4', 'D5'), ('drop', 'DO'), dict(ys=(765,))),
    ('A1', ('r4', 'A1'), ('drop', 'AO'), dict(ys=(740,))),
]

LEGEND_POS = (800, 905)  # legend: 4 columns x 3 rows
NOTES = [
    'Power is drawn as net labels: all pins labelled 5V, 3V3 or GND are connected to that rail.',
    'TCA9548A and all sensors except BME280 run on 5 V (UNO R4 has 5 V logic on A4/A5; PMSA003I fan needs 5 V).',
    'BME280 is 3.3 V only: connected through the level shifter on channel 1.',
    'D4 → RESET for the hourly self-reset: firmware keeps D4 an input and only pulls it LOW to reset. D8–D12 are free.',
    'Bulk capacitor on the 5 V rail and 100 nF at every module recommended (PMSA003I fan, WiFi bursts).',
]


# ---------------------------------------------------------------------------
# Layout -> drawing primitives
# ---------------------------------------------------------------------------
# Title baselines where the centred text block would collide with pin labels
TITLE_Y = {'r4': 220 + 255, 'lvl': 240 + 30}
KIND_FILL = {'mcu': '#e8f0fb', 'mux': '#eef0f3', 'sensor': '#f7f8fa', 'helper': '#fbf5e9', 'power': '#fbeceb'}
KIND_STROKE = {'mcu': '#2a5ea8', 'mux': '#5b6573', 'sensor': '#5b6573', 'helper': '#a87a2a', 'power': '#8e2b2b'}
INK, MUTED = '#1d2229', '#5b6573'


def build_primitives():
    prims = []

    def add(kind, net=None, comp=None, **kw):
        prims.append(dict(type=kind, net=net, comp=comp, **kw))

    add('text', x=40, y=44, text=TITLE, size=24, weight='bold', anchor='start', color=INK)
    add('text', x=40, y=70, text=SUBTITLE, size=13, anchor='start', color=MUTED)

    pin_net = {}
    for net, a, b, _ in WIRES:
        pin_net[a] = net
        pin_net[b] = net
    for comp, pin, net in FLAGS:
        pin_net[(comp, pin)] = net

    # wires first (below boxes' pin markers)
    for net, a, b, lanes in WIRES:
        add('wire', net=net, points=route(a, b, **lanes), color=NETS[net]['color'])

    for cid, c in COMPONENTS.items():
        x, y, w, h = c['box']
        add('rect', comp=cid, x=x, y=y, w=w, h=h, fill=KIND_FILL[c['kind']], stroke=KIND_STROKE[c['kind']])
        n_lines = len(c['lines'])
        # text block centred, title above info lines
        block_h = 20 + 16 * n_lines
        ty = y + h / 2 - block_h / 2 + 15
        if any(side == 'top' for _, _, side, _ in c['pins']):
            ty += 6  # leave room for the labels of top pins
        ty = TITLE_Y.get(cid, ty)
        add('text', comp=cid, x=x + w / 2, y=ty, text=c['title'], size=15, weight='bold', color=INK)
        for k, line in enumerate(c['lines']):
            color = '#0f6e8c' if SKY in line else MUTED
            add('text', comp=cid, x=x + w / 2, y=ty + 20 + 16 * k, text=line, size=12, color=color)
        for pid, label, side, off in c['pins']:
            (px, py), _ = pin_pos(cid, pid)
            net = pin_net.get((cid, pid))
            color = NETS[net]['color'] if net else MUTED
            add('pin', comp=cid, net=net, x=px, y=py, color=color, pin=pid)
            if side == 'left':
                add('text', comp=cid, net=net, x=px + 9, y=py + 4, text=label, size=11, anchor='start', color=INK)
            elif side == 'right':
                add('text', comp=cid, net=net, x=px - 9, y=py + 4, text=label, size=11, anchor='end', color=INK)
            elif side == 'top':
                add('text', comp=cid, net=net, x=px, y=py + 18, text=label, size=11, color=INK)
            else:
                add('text', comp=cid, net=net, x=px, y=py - 9, text=label, size=11, color=INK)

    for comp, pin, net in FLAGS:
        (px, py), side = pin_pos(comp, pin)
        add('flag', comp=comp, net=net, x=px, y=py, side=side, color=NETS[net]['color'],
            text={'5V': '5V', '3V3': '3V3', 'GND': ''}[net], ground=net == 'GND')

    for comp, pin in NOT_CONNECTED:
        (px, py), _ = pin_pos(comp, pin)
        add('nc', comp=comp, x=px, y=py + 16)
        add('text', comp=comp, x=px, y=py + 44, text='n.c.', size=11, color=MUTED)

    # legend
    lx, ly = LEGEND_POS
    add('text', x=lx, y=ly, text='Legend', size=14, weight='bold', anchor='start', color=INK)
    entries = ['5V', '3V3', 'GND', 'VIN', 'SDA', 'SCL', 'D2', 'D3', 'D5', 'A1', 'D6', 'D4']
    labels = {'SDA': 'I²C SDA (all buses)', 'SCL': 'I²C SCL (all buses)'}
    for k, net in enumerate(entries):
        col, row = divmod(k, 3)
        ex, ey = lx + col * 225, ly + 28 + row * 24
        if NETS[net]['kind'] in ('power', 'ground') and net != 'VIN':
            add('flag', net=net, x=ex + 6, y=ey - 4, side='right', color=NETS[net]['color'],
                text={'5V': '5V', '3V3': '3V3', 'GND': ''}[net], ground=net == 'GND', legend=True)
        else:
            add('wire', net=net, points=[(ex, ey - 4), (ex + 44, ey - 4)], color=NETS[net]['color'])
        add('text', net=net, x=ex + 70, y=ey, text=labels.get(net, NETS[net]['label']), size=12,
            anchor='start', color=INK)
    # notes below everything, full width
    ny = 1035
    add('text', x=40, y=ny, text='Notes', size=14, weight='bold', anchor='start', color=INK)
    for k, note in enumerate(NOTES):
        add('text', x=40, y=ny + 22 + 19 * k, text='•  ' + note, size=12, anchor='start', color=INK)
    return prims


# ---------------------------------------------------------------------------
# SVG backend
# ---------------------------------------------------------------------------
def _attr(**kw):
    return ' '.join(f'{k.replace("_", "-")}="{html.escape(str(v), quote=True)}"' for k, v in kw.items()
                    if v is not None)


def flag_geometry(p):
    """Line segments, ground bars and label box of a power/ground flag."""
    x, y, side = p['x'], p['y'], p['side']
    d = {'left': (-1, 0), 'right': (1, 0), 'top': (0, -1), 'bottom': (0, 1)}[side]
    stub = 14 if p.get('legend') else 22
    ex, ey = x + d[0] * stub, y + d[1] * stub
    segs = [((x, y), (ex, ey))]
    bars, box = [], None
    if p['ground']:
        if d[1] == 0:  # horizontal stub, then drop down to the symbol
            segs.append(((ex, ey), (ex, ey + 8)))
            gx, gy = ex, ey + 8
        else:
            gx, gy = ex, ey
        for k, half in enumerate((9, 6, 3)):
            bars.append(((gx - half, gy + 3 * k), (gx + half, gy + 3 * k)))
    else:
        bw, bh = 34, 18
        if d[0] > 0:
            box = (ex, ey - bh / 2, bw, bh)
        elif d[0] < 0:
            box = (ex - bw, ey - bh / 2, bw, bh)
        elif d[1] > 0:
            box = (ex - bw / 2, ey, bw, bh)
        else:
            box = (ex - bw / 2, ey - bh, bw, bh)
    return segs, bars, box


def render_svg(prims):
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
           f'font-family="Inter, Segoe UI, Helvetica, Arial, sans-serif" class="wiring">',
           f'<title>{html.escape(TITLE)}</title>',
           f'<rect x="0" y="0" width="{WIDTH}" height="{HEIGHT}" fill="#ffffff"/>']
    for p in prims:
        common = dict(data_net=p['net'], data_comp=p['comp'])
        t = p['type']
        if t == 'wire':
            pts = ' '.join(f'{x:g},{y:g}' for x, y in p['points'])
            out.append(f'<polyline {_attr(points=pts, fill="none", stroke=p["color"], stroke_width=2.6, stroke_linejoin="round", stroke_linecap="round", class_="wire", **common)}/>')
        elif t == 'rect':
            out.append(f'<rect {_attr(x=p["x"], y=p["y"], width=p["w"], height=p["h"], rx=10, fill=p["fill"], stroke=p["stroke"], stroke_width=2, class_="comp", **common)}/>')
        elif t == 'pin':
            out.append(f'<rect {_attr(x=p["x"] - 5, y=p["y"] - 5, width=10, height=10, rx=2, fill="#ffffff", stroke=p["color"], stroke_width=2.4, class_="pin", **common)}/>')
        elif t == 'text':
            anchor = p.get('anchor', 'middle')
            out.append(f'<text {_attr(x=p["x"], y=p["y"], font_size=p["size"], font_weight=p.get("weight"), text_anchor=anchor, fill=p["color"], class_="label", **common)}>{html.escape(p["text"])}</text>')
        elif t == 'flag':
            segs, bars, box = flag_geometry(p)
            parts = [f'<line {_attr(x1=a[0], y1=a[1], x2=b[0], y2=b[1], stroke=p["color"], stroke_width=2.4)}/>'
                     for a, b in segs + bars]
            if box:
                bx, by, bw, bh = box
                parts.append(f'<rect {_attr(x=bx, y=by, width=bw, height=bh, rx=4, fill="#ffffff", stroke=p["color"], stroke_width=1.8)}/>')
                parts.append(f'<text {_attr(x=bx + bw / 2, y=by + bh / 2 + 4, font_size=11, font_weight="bold", text_anchor="middle", fill=p["color"])}>{p["text"]}</text>')
            out.append(f'<g {_attr(class_="flag", **common)}>' + ''.join(parts) + '</g>')
        elif t == 'nc':
            x, y = p['x'], p['y']
            out.append(f'<g {_attr(class_="nc", **common)}><line {_attr(x1=x, y1=p["y"] - 16, x2=x, y2=y, stroke=MUTED, stroke_width=2)}/>'
                       f'<line {_attr(x1=x - 7, y1=y - 7, x2=x + 7, y2=y + 7, stroke=MUTED, stroke_width=2.4)}/>'
                       f'<line {_attr(x1=x - 7, y1=y + 7, x2=x + 7, y2=y - 7, stroke=MUTED, stroke_width=2.4)}/></g>')
    out.append('</svg>')
    return '\n'.join(out)


# ---------------------------------------------------------------------------
# PNG backend (matplotlib)
# ---------------------------------------------------------------------------
def render_png(prims, path, dpi=150):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import FancyBboxPatch

    fig = plt.figure(figsize=(WIDTH / 100, HEIGHT / 100), dpi=dpi)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, WIDTH)
    ax.set_ylim(HEIGHT, 0)
    ax.axis('off')
    pt = 72 / 100  # 1 px of the layout in points
    for p in prims:
        t = p['type']
        if t == 'wire':
            xs, ys = zip(*p['points'])
            ax.plot(xs, ys, color=p['color'], lw=2.6 * pt, solid_capstyle='round', solid_joinstyle='round', zorder=1)
        elif t == 'rect':
            ax.add_patch(FancyBboxPatch((p['x'], p['y']), p['w'], p['h'], boxstyle='round,pad=0,rounding_size=10',
                                        fc=p['fill'], ec=p['stroke'], lw=2 * pt, zorder=2))
        elif t == 'pin':
            ax.add_patch(FancyBboxPatch((p['x'] - 5, p['y'] - 5), 10, 10, boxstyle='round,pad=0,rounding_size=2',
                                        fc='white', ec=p['color'], lw=2.4 * pt, zorder=4))
        elif t == 'text':
            ha = {'start': 'left', 'end': 'right', 'middle': 'center'}[p.get('anchor', 'middle')]
            ax.text(p['x'], p['y'], p['text'], fontsize=p['size'] * pt, ha=ha, va='baseline',
                    fontweight=p.get('weight', 'normal'), color=p['color'], zorder=5)
        elif t == 'flag':
            segs, bars, box = flag_geometry(p)
            for a, b in segs + bars:
                ax.plot([a[0], b[0]], [a[1], b[1]], color=p['color'], lw=2.4 * pt, zorder=3)
            if box:
                bx, by, bw, bh = box
                ax.add_patch(FancyBboxPatch((bx, by), bw, bh, boxstyle='round,pad=0,rounding_size=4',
                                            fc='white', ec=p['color'], lw=1.8 * pt, zorder=4))
                ax.text(bx + bw / 2, by + bh / 2 + 4, p['text'], fontsize=11 * pt, ha='center', va='baseline',
                        fontweight='bold', color=p['color'], zorder=5)
        elif t == 'nc':
            x, y = p['x'], p['y']
            ax.plot([x, x], [y - 16, y], color=MUTED, lw=2 * pt, zorder=3)
            ax.plot([x - 7, x + 7], [y - 7, y + 7], color=MUTED, lw=2.4 * pt, zorder=3)
            ax.plot([x - 7, x + 7], [y + 7, y - 7], color=MUTED, lw=2.4 * pt, zorder=3)
    fig.savefig(path, dpi=dpi, facecolor='white')
    plt.close(fig)


# ---------------------------------------------------------------------------
# Interactive HTML
# ---------------------------------------------------------------------------
def connection_data():
    """Per component: its pins with net and the pins they connect to."""
    endpoints = {}
    for net, a, b, _ in WIRES:
        endpoints.setdefault(net, []).extend([a, b])
    for comp, pin, net in FLAGS:
        endpoints.setdefault(net, []).append((comp, pin))
    nets_of = {}
    for net, eps in endpoints.items():
        for ep in eps:
            nets_of[ep] = net
    comps = {}
    for cid, c in COMPONENTS.items():
        rows = []
        for pid, label, side, _ in c['pins']:
            net = nets_of.get((cid, pid))
            if (cid, pid) in NOT_CONNECTED:
                rows.append(dict(pin=label, net=None, netLabel='not connected', to=[]))
                continue
            others = [] if net is None else [
                f"{COMPONENTS[o[0]]['title']} {dict((p[0], p[1]) for p in COMPONENTS[o[0]]['pins'])[o[1]]}"
                for o in endpoints[net] if o != (cid, pid)]
            if net and NETS[net]['kind'] in ('power', 'ground') and net != 'VIN':
                others = [f'{NETS[net]["label"]} rail ({len(endpoints[net]) - 1} other pins)']
            rows.append(dict(pin=label, net=net, netLabel=NETS[net]['label'] if net else '–', to=others))
        comps[cid] = dict(title=c['title'], lines=[l for l in c['lines'] if l], info=c['info'], pins=rows)
    wires = [dict(net=net, label=NETS[net]['label'], color=NETS[net]['color'], note=NETS[net]['note'],
                  a=f"{COMPONENTS[a[0]]['title']} · {dict((p[0], p[1]) for p in COMPONENTS[a[0]]['pins'])[a[1]]}",
                  b=f"{COMPONENTS[b[0]]['title']} · {dict((p[0], p[1]) for p in COMPONENTS[b[0]]['pins'])[b[1]]}")
             for net, a, b, _ in WIRES]
    power = [dict(net=net, label=NETS[net]['label'], color=NETS[net]['color'], note=NETS[net]['note'],
                  pins=[f"{COMPONENTS[c]['title']} · {dict((p[0], p[1]) for p in COMPONENTS[c]['pins'])[p]}"
                        for c, p in endpoints[net]])
             for net in ('5V', '3V3', 'GND')]
    nets = {k: dict(label=v['label'], note=v['note']) for k, v in NETS.items()}
    return dict(components=comps, wires=wires, power=power, nets=nets)


HTML_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Weather Station Wiring</title>
<style>
:root {
  --bg: #f4f6f8; --surface: #ffffff; --ink: #1d2229; --ink-2: #5b6573; --line: #d9dee5;
  --accent: #2a5ea8; --accent-soft: #e8f0fb;
  --font: Inter, "Segoe UI", system-ui, -apple-system, Helvetica, Arial, sans-serif;
  --mono: ui-monospace, "SFMono-Regular", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme: dark;
    --bg: #11151a; --surface: #1a2027; --ink: #e6ebf0; --ink-2: #a2adba; --line: #2c343e;
    --accent: #7fb0f0; --accent-soft: #1b2a3e;
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --bg: #11151a; --surface: #1a2027; --ink: #e6ebf0; --ink-2: #a2adba; --line: #2c343e;
  --accent: #7fb0f0; --accent-soft: #1b2a3e;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.5 var(--font); padding-inline: 20px; padding-block: 0 48px; }
.page { max-width: 1480px; margin: 0 auto; }
header { padding-block: 28px 16px; }
h1 { font-size: 26px; margin: 0 0 4px; text-wrap: balance; }
h2 { font-size: 18px; margin: 32px 0 10px; }
.sub { color: var(--ink-2); margin: 0; max-width: 70rem; }
.layout { display: grid; grid-template-columns: minmax(0, 1fr) 340px; gap: 18px; align-items: start; }
@media (max-width: 1100px) { .layout { grid-template-columns: 1fr; } }
.diagram { background: #ffffff; border: 1px solid var(--line); border-radius: 12px; overflow-x: auto; }
.diagram svg { display: block; width: 100%; min-width: 900px; height: auto; }
.panel { background: var(--surface); border: 1px solid var(--line); border-radius: 12px; padding: 16px 18px; position: sticky; top: 12px; }
.panel h3 { margin: 0 0 4px; font-size: 18px; }
.panel .meta { color: var(--ink-2); margin: 0 0 10px; font-size: 14px; }
.panel p { margin: 0 0 12px; font-size: 14px; }
.hint { color: var(--ink-2); font-size: 14px; }
table { border-collapse: collapse; width: 100%; font-size: 13.5px; }
th, td { text-align: left; padding: 6px 8px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { font-weight: 600; color: var(--ink-2); font-size: 12.5px; text-transform: uppercase; letter-spacing: .04em; }
td.pin { font-family: var(--mono); white-space: nowrap; }
.swatch { display: inline-block; width: 18px; height: 4px; border-radius: 2px; vertical-align: middle; margin-right: 8px; }
.table-wrap { background: var(--surface); border: 1px solid var(--line); border-radius: 12px; overflow-x: auto; }
.table-wrap table th:first-child, .table-wrap table td:first-child { padding-left: 14px; }
button.reset { font: inherit; font-size: 13px; background: var(--accent-soft); color: var(--accent); border: 1px solid var(--line);
  border-radius: 8px; padding: 4px 10px; cursor: pointer; }
button.reset:focus-visible, .comp:focus-visible { outline: 3px solid var(--accent); outline-offset: 2px; }
/* diagram interaction */
svg.wiring .comp { cursor: pointer; }
svg.wiring .wire, svg.wiring .pin, svg.wiring .flag { cursor: pointer; transition: opacity .12s; }
svg.wiring.focus [data-net]:not(.hl), svg.wiring.focus .comp:not(.hl) { opacity: .18; }
svg.wiring.focus text.label:not([data-net]) { opacity: .45; }
svg.wiring .wire.hl { stroke-width: 4.5; }
svg.wiring .comp.hl { stroke-width: 3.5; }
tr.hl td { background: var(--accent-soft); }
@media (prefers-reduced-motion: reduce) { svg.wiring * { transition: none !important; } }
</style>
</head>
<body>
<div class="page">
<header>
  <h1>Weather station wiring</h1>
  <p class="sub">Arduino UNO R4 WiFi with TCA9548A I²C multiplexer. Hover a wire, pin or rail label to highlight its connections;
  click a component for its pin table. Power is drawn as rail labels: every pin labelled 5V, 3V3 or GND is connected to that rail.</p>
</header>
<div class="layout">
  <div class="diagram">__SVG__</div>
  <aside class="panel" id="panel" aria-live="polite">
    <h3 id="p-title">Select a component</h3>
    <p class="meta" id="p-meta">Click a box in the diagram.</p>
    <p id="p-info" class="hint">The panel lists every pin of the selected component and what it is connected to.</p>
    <div id="p-table"></div>
    <p style="margin-top:12px"><button class="reset" id="reset" type="button">Clear selection</button></p>
  </aside>
</div>
<h2>Signal and I²C connections</h2>
<div class="table-wrap"><table id="wires"><thead><tr><th>Net</th><th>From</th><th>To</th><th>Notes</th></tr></thead><tbody></tbody></table></div>
<h2>Power rails</h2>
<div class="table-wrap"><table id="power"><thead><tr><th>Rail</th><th>Pins</th><th>Notes</th></tr></thead><tbody></tbody></table></div>
<h2>Notes</h2>
<ul>__NOTES__</ul>
</div>
<script>
const DATA = __DATA__;
const svg = document.querySelector('svg.wiring');
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let selected = null;

function highlight(nets, comps) {
  svg.querySelectorAll('.hl').forEach(e => e.classList.remove('hl'));
  document.querySelectorAll('tr.hl').forEach(e => e.classList.remove('hl'));
  if (!nets.size && !comps.size) { svg.classList.remove('focus'); return; }
  svg.classList.add('focus');
  svg.querySelectorAll('[data-net]').forEach(e => { if (nets.has(e.dataset.net)) e.classList.add('hl'); });
  svg.querySelectorAll('.comp').forEach(e => { if (comps.has(e.dataset.comp)) e.classList.add('hl'); });
  document.querySelectorAll('tr[data-net]').forEach(r => { if (nets.has(r.dataset.net)) r.classList.add('hl'); });
}
function compNets(cid) {
  const s = new Set();
  svg.querySelectorAll(`[data-comp="${cid}"][data-net]`).forEach(e => s.add(e.dataset.net));
  return s;
}
function netComps(net) {
  const s = new Set();
  svg.querySelectorAll(`[data-net="${net}"][data-comp]`).forEach(e => s.add(e.dataset.comp));
  return s;
}
function showComp(cid) {
  const c = DATA.components[cid];
  document.getElementById('p-title').textContent = c.title;
  document.getElementById('p-meta').textContent = c.lines.join(' · ');
  document.getElementById('p-info').textContent = c.info;
  document.getElementById('p-info').classList.remove('hint');
  const rows = c.pins.map(p => `<tr><td class="pin">${esc(p.pin)}</td><td>${esc(p.netLabel)}</td><td>${p.to.map(esc).join('<br>') || '–'}</td></tr>`).join('');
  document.getElementById('p-table').innerHTML = `<table><thead><tr><th>Pin</th><th>Net</th><th>Connected to</th></tr></thead><tbody>${rows}</tbody></table>`;
}
function select(cid) {
  selected = cid;
  if (!cid) { highlight(new Set(), new Set()); return; }
  showComp(cid);
  highlight(compNets(cid), new Set([cid]));
}
svg.querySelectorAll('.comp').forEach(r => {
  r.setAttribute('tabindex', '0');
  r.setAttribute('role', 'button');
  r.setAttribute('aria-label', DATA.components[r.dataset.comp].title);
  r.addEventListener('click', () => select(r.dataset.comp));
  r.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); select(r.dataset.comp); } });
});
svg.querySelectorAll('[data-net]').forEach(e => {
  e.addEventListener('mouseenter', () => { const n = e.dataset.net; highlight(new Set([n]), netComps(n)); });
  e.addEventListener('mouseleave', () => select(selected));
  e.addEventListener('click', ev => { if (e.dataset.comp) { ev.stopPropagation(); select(e.dataset.comp); } });
  const t = DATA.nets[e.dataset.net];
  if (t) { const title = document.createElementNS('http://www.w3.org/2000/svg', 'title'); title.textContent = `${t.label}: ${t.note}`; e.appendChild(title); }
});
document.getElementById('reset').addEventListener('click', () => {
  selected = null; select(null);
  document.getElementById('p-title').textContent = 'Select a component';
  document.getElementById('p-meta').textContent = 'Click a box in the diagram.';
  const info = document.getElementById('p-info'); info.textContent = 'The panel lists every pin of the selected component and what it is connected to.'; info.classList.add('hint');
  document.getElementById('p-table').innerHTML = '';
});
document.querySelector('#wires tbody').innerHTML = DATA.wires.map(w =>
  `<tr data-net="${w.net}"><td><span class="swatch" style="background:${w.color}"></span>${esc(w.label)}</td><td>${esc(w.a)}</td><td>${esc(w.b)}</td><td>${esc(w.note)}</td></tr>`).join('');
document.querySelector('#power tbody').innerHTML = DATA.power.map(p =>
  `<tr data-net="${p.net}"><td><span class="swatch" style="background:${p.color}"></span>${esc(p.label)}</td><td>${p.pins.map(esc).join('<br>')}</td><td>${esc(p.note)}</td></tr>`).join('');
document.querySelectorAll('tr[data-net]').forEach(r => {
  r.addEventListener('mouseenter', () => highlight(new Set([r.dataset.net]), netComps(r.dataset.net)));
  r.addEventListener('mouseleave', () => select(selected));
});
</script>
</body>
</html>
"""


def render_html(svg):
    notes = ''.join(f'<li>{html.escape(n)}</li>' for n in NOTES)
    data = json.dumps(connection_data(), ensure_ascii=False).replace('</', '<\\/')
    # the HTML page shows its own title, legend and notes outside the drawing
    return (HTML_TEMPLATE.replace('__SVG__', svg).replace('__NOTES__', notes)
            .replace('__DATA__', data))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument('--no-png', action='store_true', help='skip the PNG (no matplotlib needed)')
    args = parser.parse_args()
    prims = build_primitives()
    svg = render_svg(prims)
    (HERE / f'{BASENAME}.svg').write_text(svg, encoding='utf-8')
    (HERE / f'{BASENAME}.html').write_text(render_html(svg), encoding='utf-8')
    written = [f'{BASENAME}.svg', f'{BASENAME}.html']
    if not args.no_png:
        render_png(prims, HERE / f'{BASENAME}.png')
        written.append(f'{BASENAME}.png')
    print('wrote', ', '.join(written))


if __name__ == '__main__':
    main()
