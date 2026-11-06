#!/usr/bin/env python3
"""Generate the Rev 7.0 schematic for the osmo-pocket-esp32-gimbal controller.

Emits kicad/osmo-pocket-esp32-gimbal.kicad_sch (KiCad 8 format) deterministically:
every UUID is uuid5() of a stable key, so re-running produces a byte-identical file.

No KiCad symbol libraries are assumed to be installed, so every symbol used is
defined inline in (lib_symbols ...) under the custom "gimbal:" library prefix.

Contents:
  * U1  ESP32-S3-WROOM-1-N8R8 with the validated GPIO map from firmware/src/gimbal_pins.h
  * U2  TCA9548APWR I2C mux @0x70 on ENC_SDA/ENC_SCL (GPIO17/GPIO18), RESET_N = GPIO2
  * U3..U5 AS5600-ASOM magnetic encoders (0x36) behind mux channels 0/1/2 (PAN/ROLL/PITCH)
  * J1..J3 4-pin connectors to the remote encoder flex boards
  * pull-ups, decoupling, power symbols, PWR_FLAGs, no-connect flags, address-plan note

Usage:
  python3 kicad/tools/gen_rev7_schematic.py                       # write the schematic
  python3 kicad/tools/gen_rev7_schematic.py --verify-netlist X.net # check an exported netlist
"""
import argparse
import math
import os
import re
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SCH_PATH = os.path.normpath(os.path.join(HERE, "..", "osmo-pocket-esp32-gimbal.kicad_sch"))
PROJECT = "osmo-pocket-esp32-gimbal"
LIB = "gimbal"

NS = uuid.uuid5(uuid.NAMESPACE_URL, "osmo-pocket-esp32-gimbal/schematic/rev7")
_used_keys = set()


def U(key):
    assert key not in _used_keys, f"duplicate uuid key {key}"
    _used_keys.add(key)
    return str(uuid.uuid5(NS, key))


ROOT = U("root-sheet")
P = 2.54  # pin pitch
FONT = "(effects (font (size 1.27 1.27)))"
FONT_HIDE = "(effects (font (size 1.27 1.27)) hide)"


def f(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


def q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


# ---------------------------------------------------------------------------
# Library symbols
# ---------------------------------------------------------------------------
class LibSym:
    def __init__(self, name, sexpr, pins, power=False):
        self.name = name          # without library prefix
        self.lib_id = f"{LIB}:{name}"
        self.sexpr = sexpr
        self.pins = pins          # num -> (x, y, angle, pin_name)  (lib coords, y up)
        self.power = power


def _props(ref, value, ref_at, val_at, footprint="", datasheet="", desc="",
           ref_hide=False, val_hide=False):
    out = [
        f"(property \"Reference\" {q(ref)} (at {f(ref_at[0])} {f(ref_at[1])} 0) {FONT_HIDE if ref_hide else FONT})",
        f"(property \"Value\" {q(value)} (at {f(val_at[0])} {f(val_at[1])} 0) {FONT_HIDE if val_hide else FONT})",
        f"(property \"Footprint\" {q(footprint)} (at 0 0 0) {FONT_HIDE})",
        f"(property \"Datasheet\" {q(datasheet)} (at 0 0 0) {FONT_HIDE})",
        f"(property \"Description\" {q(desc)} (at 0 0 0) {FONT_HIDE})",
    ]
    return "\n    ".join(out)


def _pin(ptype, x, y, ang, length, name, num, hide=False):
    h = " hide" if hide else ""
    return (f"(pin {ptype} line (at {f(x)} {f(y)} {ang}) (length {f(length)}){h} "
            f"(name {q(name)} {FONT}) (number {q(num)} {FONT}))")


def ic_symbol(name, ref, value, left, right, bottom=(), hw=10.16, footprint="",
              datasheet="", desc=""):
    """left/right: lists of (num, name, type) top-to-bottom; bottom: left-to-right."""
    L = P
    n = max(len(left), len(right))
    y0 = math.ceil((n - 1) / 2) * P
    top = y0 + P
    bot = y0 - (n - 1) * P - P
    pins, pin_sx = {}, []
    for i, (num, pn, pt) in enumerate(left):
        x, y = -hw - L, y0 - i * P
        pins[num] = (x, y, 0, pn)
        pin_sx.append(_pin(pt, x, y, 0, L, pn, num))
    for i, (num, pn, pt) in enumerate(right):
        x, y = hw + L, y0 - i * P
        pins[num] = (x, y, 180, pn)
        pin_sx.append(_pin(pt, x, y, 180, L, pn, num))
    nb = len(bottom)
    for i, (num, pn, pt) in enumerate(bottom):
        x, y = (i - (nb - 1) / 2) * P, bot - L
        pins[num] = (x, y, 90, pn)
        pin_sx.append(_pin(pt, x, y, 90, L, pn, num))
    sx = f"""(symbol "{LIB}:{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props(ref, value, (0, top + 3.81), (0, top + 1.27), footprint, datasheet, desc)}
    (symbol "{name}_0_1"
      (rectangle (start {f(-hw)} {f(top)}) (end {f(hw)} {f(bot)}) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "{name}_1_1"
      {chr(10).join('      ' + p for p in pin_sx).lstrip()}
    )
  )"""
    s = LibSym(name, sx, pins)
    s.top, s.bot = top, bot
    return s


def resistor():
    sx = f"""(symbol "{LIB}:R" (pin_numbers hide) (pin_names (offset 0)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("R", "R", (2.032, 0), (0, 0), "", "~", "Resistor")}
    (symbol "R_0_1"
      (rectangle (start -1.016 -2.54) (end 1.016 2.54) (stroke (width 0.254) (type default)) (fill (type none)))
    )
    (symbol "R_1_1"
      {_pin("passive", 0, 3.81, 270, 1.27, "~", "1")}
      {_pin("passive", 0, -3.81, 90, 1.27, "~", "2")}
    )
  )"""
    return LibSym("R", sx, {"1": (0, 3.81, 270, "~"), "2": (0, -3.81, 90, "~")})


def capacitor():
    sx = f"""(symbol "{LIB}:C" (pin_numbers hide) (pin_names (offset 0.254)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("C", "C", (0.635, 2.54), (0.635, -2.54), "", "~", "Unpolarized capacitor")}
    (symbol "C_0_1"
      (polyline (pts (xy -2.032 -0.762) (xy 2.032 -0.762)) (stroke (width 0.508) (type default)) (fill (type none)))
      (polyline (pts (xy -2.032 0.762) (xy 2.032 0.762)) (stroke (width 0.508) (type default)) (fill (type none)))
    )
    (symbol "C_1_1"
      {_pin("passive", 0, 3.81, 270, 3.048, "~", "1")}
      {_pin("passive", 0, -3.81, 90, 3.048, "~", "2")}
    )
  )"""
    return LibSym("C", sx, {"1": (0, 3.81, 270, "~"), "2": (0, -3.81, 90, "~")})


def conn4():
    names = ["3V3", "GND", "SDA", "SCL"]
    pins = {}
    psx = []
    for i, nm in enumerate(names):
        y = P - i * P
        pins[str(i + 1)] = (-5.08, y, 0, nm)
        psx.append(_pin("passive", -5.08, y, 0, 3.81, nm, str(i + 1)))
    sx = f"""(symbol "{LIB}:Conn_01x04_Encoder" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("J", "Conn_01x04_Encoder", (0, 6.35), (0, 3.81 + 0.635), "", "~", "4-pin I2C encoder connector (3V3, GND, SDA, SCL)")}
    (symbol "Conn_01x04_Encoder_0_1"
      (rectangle (start -1.27 3.81) (end 6.35 -6.35) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "Conn_01x04_Encoder_1_1"
      {chr(10).join('      ' + p for p in psx).lstrip()}
    )
  )"""
    s = LibSym("Conn_01x04_Encoder", sx, pins)
    s.top, s.bot = 3.81, -6.35
    return s


def power_sym(name, graphics, val_y, desc):
    sx = f"""(symbol "{LIB}:{name}" (power) (pin_names (offset 0)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("#PWR", name, (0, -val_y * 1.6), (0, val_y), "", "", desc, ref_hide=True)}
    (symbol "{name}_0_1"
      {graphics}
    )
    (symbol "{name}_1_1"
      {_pin("power_in", 0, 0, 90 if val_y > 0 else 270, 0, name, "1", hide=True)}
    )
  )"""
    s = LibSym(name, sx, {"1": (0, 0, 90, name)}, power=True)
    s.val_y = val_y
    return s


def pwr_flag():
    sx = f"""(symbol "{LIB}:PWR_FLAG" (power) (pin_numbers hide) (pin_names (offset 0) hide) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("#FLG", "PWR_FLAG", (0, 1.905), (0, 3.81), "", "~", "Special symbol for telling ERC where power comes from", ref_hide=True)}
    (symbol "PWR_FLAG_0_0"
      {_pin("power_out", 0, 0, 90, 0, "pwr", "1")}
    )
    (symbol "PWR_FLAG_0_1"
      (polyline (pts (xy 0 0) (xy 0 1.27) (xy -1.016 1.905) (xy 0 2.54) (xy 1.016 1.905) (xy 0 1.27)) (stroke (width 0) (type default)) (fill (type none)))
    )
  )"""
    s = LibSym("PWR_FLAG", sx, {"1": (0, 0, 90, "pwr")}, power=True)
    s.val_y = 3.81
    return s


BID, INP, OUT, PIN, PAS = "bidirectional", "input", "output", "power_in", "passive"

ESP_LEFT = [("2", "3V3", PIN), ("3", "EN", INP), ("4", "IO4", BID), ("5", "IO5", BID),
            ("6", "IO6", BID), ("7", "IO7", BID), ("8", "IO15", BID), ("9", "IO16", BID),
            ("10", "IO17", BID), ("11", "IO18", BID), ("12", "IO8", BID), ("13", "IO19", BID),
            ("14", "IO20", BID), ("15", "IO3", BID), ("16", "IO46", BID), ("17", "IO9", BID),
            ("18", "IO10", BID), ("19", "IO11", BID), ("20", "IO12", BID), ("21", "IO13", BID),
            ("22", "IO14", BID)]
ESP_RIGHT = [("23", "IO21", BID), ("24", "IO47", BID), ("25", "IO48", BID), ("26", "IO45", BID),
             ("27", "IO0", BID), ("28", "IO35", BID), ("29", "IO36", BID), ("30", "IO37", BID),
             ("31", "IO38", BID), ("32", "IO39", BID), ("33", "IO40", BID), ("34", "IO41", BID),
             ("35", "IO42", BID), ("36", "RXD0/IO44", BID), ("37", "TXD0/IO43", BID),
             ("38", "IO2", BID), ("39", "IO1", BID)]
ESP_BOTTOM = [("1", "GND", PIN), ("40", "GND", PIN), ("41", "EPAD/GND", PIN)]

TCA_LEFT = [("24", "VCC", PIN), ("23", "SDA", BID), ("22", "SCL", INP), ("3", "~{RESET}", INP),
            ("1", "A0", INP), ("2", "A1", INP), ("21", "A2", INP), ("12", "GND", PIN)]
TCA_RIGHT = []
for ch, (sd, sc) in enumerate([(4, 5), (6, 7), (8, 9), (10, 11), (13, 14), (15, 16), (17, 18), (19, 20)]):
    TCA_RIGHT += [(str(sd), f"SD{ch}", BID), (str(sc), f"SC{ch}", BID)]

AS_LEFT = [("1", "VDD5V", PIN), ("2", "VDD3V3", PIN), ("6", "SDA", BID), ("7", "SCL", INP)]
AS_RIGHT = [("3", "OUT", OUT), ("5", "PGO", INP), ("8", "DIR", INP), ("4", "GND", PIN)]

SYMS = {
    "ESP": ic_symbol("ESP32-S3-WROOM-1-N8R8", "U", "ESP32-S3-WROOM-1-N8R8", ESP_LEFT, ESP_RIGHT,
                     ESP_BOTTOM, hw=15.24, footprint="RF_Module:ESP32-S3-WROOM-1",
                     datasheet="https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf",
                     desc="ESP32-S3 Wi-Fi/BLE module, 8MB flash, 8MB octal PSRAM"),
    "TCA": ic_symbol("TCA9548APWR", "U", "TCA9548APWR", TCA_LEFT, TCA_RIGHT, hw=10.16,
                     footprint="Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm",
                     datasheet="https://www.ti.com/lit/ds/symlink/tca9548a.pdf",
                     desc="8-channel I2C switch with reset, TSSOP-24"),
    "AS": ic_symbol("AS5600-ASOM", "U", "AS5600-ASOM", AS_LEFT, AS_RIGHT, hw=7.62,
                    footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
                    datasheet="https://ams-osram.com/products/sensors/position-sensors/ams-as5600-position-sensor",
                    desc="12-bit contactless magnetic rotary position sensor, I2C 0x36, SOIC-8"),
    "R": resistor(),
    "C": capacitor(),
    "J": conn4(),
    "+3V3": power_sym("+3V3",
                      "(polyline (pts (xy -0.762 1.27) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))\n"
                      "      (polyline (pts (xy 0 0) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))\n"
                      "      (polyline (pts (xy 0 2.54) (xy 0.762 1.27)) (stroke (width 0) (type default)) (fill (type none)))",
                      3.556, "Power symbol creates a global label with name \"+3V3\""),
    "GND": power_sym("GND",
                     "(polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27)) (stroke (width 0) (type default)) (fill (type none)))",
                     -3.81, "Power symbol creates a global label with name \"GND\" , ground"),
    "FLG": pwr_flag(),
}

# ---------------------------------------------------------------------------
# Schematic items
# ---------------------------------------------------------------------------
items = []           # sexpr strings in emission order
expected = {}        # net name -> set of (ref, pin)
pwr_count = [0]
flg_count = [0]


def rot(x, y, ang):
    """Rotate a screen-space offset (y down) counter-clockwise on screen by ang degrees."""
    for _ in range((ang // 90) % 4):
        x, y = y, -x
    return x, y


class Inst:
    def __init__(self, sym, ref, value, x, y, ang=0, footprint=None, mpn=None, hide_value=False):
        self.sym, self.ref, self.value, self.x, self.y, self.ang = sym, ref, value, x, y, ang
        props = []
        if sym.power:
            vx, vy = rot(0, -sym.val_y, ang)
            props.append(f"(property \"Reference\" {q(ref)} (at {f(x)} {f(y)} 0) {FONT_HIDE})")
            # side-mounted (rotated) power symbols: the graphic is unambiguous, hide the text
            vfont = FONT_HIDE if (ang or hide_value) else FONT
            props.append(f"(property \"Value\" {q(value)} (at {f(x + vx)} {f(y + vy)} 0) {vfont})")
            fp = ""
        elif sym.name in ("R", "C"):
            props.append(f"(property \"Reference\" {q(ref)} (at {f(x + 2.54)} {f(y - 1.27)} 0) (effects (font (size 1.27 1.27)) (justify left)))")
            props.append(f"(property \"Value\" {q(value)} (at {f(x + 2.54)} {f(y + 1.27)} 0) (effects (font (size 1.27 1.27)) (justify left)))")
            fp = footprint or ""
        else:
            props.append(f"(property \"Reference\" {q(ref)} (at {f(x)} {f(y - sym.top - 3.81)} 0) {FONT})")
            props.append(f"(property \"Value\" {q(value)} (at {f(x)} {f(y - sym.top - 1.27)} 0) {FONT})")
            fp = footprint or ""
        if not fp and not sym.power:
            fp = re.search(r'"Footprint" "([^"]*)"', sym.sexpr).group(1)
        ds = re.search(r'"Datasheet" "([^"]*)"', sym.sexpr).group(1)
        desc = re.search(r'"Description" "((?:[^"\\]|\\.)*)"', sym.sexpr).group(1)
        props.append(f"(property \"Footprint\" {q(fp)} (at {f(x)} {f(y)} 0) {FONT_HIDE})")
        props.append(f"(property \"Datasheet\" {q(ds)} (at {f(x)} {f(y)} 0) {FONT_HIDE})")
        props.append(f"(property \"Description\" \"{desc}\" (at {f(x)} {f(y)} 0) {FONT_HIDE})")
        if mpn:
            props.append(f"(property \"MPN\" {q(mpn)} (at {f(x)} {f(y)} 0) {FONT_HIDE})")
        pins = "\n    ".join(f"(pin {q(n)} (uuid {q(U(f'pin/{ref}/{n}'))}))" for n in sym.pins)
        bom = "no" if sym.power else "yes"
        items.append(f"""(symbol (lib_id {q(sym.lib_id)}) (at {f(x)} {f(y)} {ang}) (unit 1)
    (exclude_from_sim no) (in_bom {bom}) (on_board yes) (dnp no)
    (uuid {q(U(f'sym/{ref}'))})
    {chr(10).join('    ' + p for p in props).lstrip()}
    {pins}
    (instances (project {q(PROJECT)} (path {q('/' + ROOT)} (reference {q(ref)}) (unit 1))))
  )""")

    def pin_pos(self, num):
        px, py, pang, _ = self.sym.pins[num]
        dx, dy = rot(px, -py, self.ang)
        # direction away from body (screen coords) for an unrotated symbol
        away = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[pang]
        away = rot(away[0], away[1], self.ang)
        return (self.x + dx, self.y + dy), away


def power(kind, x, y, ang=0, hide_value=False):
    pwr_count[0] += 1
    ref = f"#PWR0{pwr_count[0]:02d}"
    return Inst(SYMS[kind], ref, kind, x, y, ang, hide_value=hide_value)


def flag(x, y):
    flg_count[0] += 1
    return Inst(SYMS["FLG"], f"#FLG0{flg_count[0]}", "PWR_FLAG", x, y)


def wire(a, b):
    items.append(f"(wire (pts (xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])})) "
                 f"(stroke (width 0) (type default)) (uuid {q(U(f'wire/{f(a[0])},{f(a[1])}-{f(b[0])},{f(b[1])}'))}))")


ANG = {(-1, 0): 180, (1, 0): 0, (0, -1): 90, (0, 1): 270}
JUST = {0: "left bottom", 90: "left bottom", 180: "right bottom", 270: "right bottom"}
GJUST = {0: "left", 90: "left", 180: "right", 270: "right"}
PWR_ROT = {"GND": {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270},
           "+3V3": {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}}


def connect(inst, num, kind, name=None, length=5.08, shape="output", hide_value=False):
    """Attach a pin: kind in {'nc', 'label', 'glabel', '+3V3', 'GND'}."""
    (px, py), away = inst.pin_pos(num)
    if kind == "nc":
        items.append(f"(no_connect (at {f(px)} {f(py)}) (uuid {q(U(f'nc/{inst.ref}/{num}'))}))")
        return
    ex, ey = px + away[0] * length, py + away[1] * length
    wire((px, py), (ex, ey))
    a = ANG[away]
    if kind == "label":
        items.append(f"(label {q(name)} (at {f(ex)} {f(ey)} {a}) (fields_autoplaced yes) "
                     f"(effects (font (size 1.27 1.27)) (justify {JUST[a]})) (uuid {q(U(f'label/{name}/{inst.ref}/{num}'))}))")
        net = name
    elif kind == "glabel":
        items.append(f"(global_label {q(name)} (shape {shape}) (at {f(ex)} {f(ey)} {a}) (fields_autoplaced yes) "
                     f"(effects (font (size 1.27 1.27)) (justify {GJUST[a]})) (uuid {q(U(f'glabel/{name}/{inst.ref}/{num}'))}) "
                     f"(property \"Intersheetrefs\" \"${{INTERSHEET_REFS}}\" (at {f(ex)} {f(ey)} 0) {FONT_HIDE}))")
        net = name
    else:
        power(kind, ex, ey, PWR_ROT[kind][away], hide_value)
        net = kind
    expected.setdefault(net, set()).add((inst.ref, num))


def passive(sym, ref, value, x, y, top, bottom, fp):
    """Vertical R/C at (x, y). top/bottom = (kind, name)."""
    i = Inst(SYMS[sym], ref, value, x, y, footprint=fp)
    connect(i, "1", top[0], top[1], length=2.54)
    connect(i, "2", bottom[0], bottom[1], length=2.54)
    return i


R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
V33 = ("+3V3", None)
GND = ("GND", None)


def lbl(n):
    return ("label", n)


# ---------------------------------------------------------------- power flags
pa = power("+3V3", 30.48, 30.48)
wire((30.48, 30.48), (38.1, 30.48))
flag(38.1, 30.48)
pg = power("GND", 53.34, 30.48)
wire((53.34, 30.48), (60.96, 30.48))
flag(60.96, 30.48)
expected.setdefault("+3V3", set()).add(("#FLG01", "1"))
expected.setdefault("GND", set()).add(("#FLG02", "1"))

# ---------------------------------------------------------------- U1 ESP32-S3
U1 = Inst(SYMS["ESP"], "U1", "ESP32-S3-WROOM-1-N8R8", 101.6, 139.7, mpn="ESP32-S3-WROOM-1-N8R8")
U1_MAP = {
    "2": ("+3V3", None), "3": ("label", "ESP_EN"),
    # motors (3PWM + EN)
    "4": ("glabel", "PAN_PWM_A"), "5": ("glabel", "PAN_PWM_B"), "6": ("glabel", "PAN_PWM_C"),
    "7": ("glabel", "PAN_EN"),
    "12": ("glabel", "ROLL_PWM_A"), "17": ("glabel", "ROLL_PWM_B"), "18": ("glabel", "ROLL_PWM_C"),
    "19": ("glabel", "ROLL_EN"),
    "20": ("glabel", "PITCH_PWM_A"), "21": ("glabel", "PITCH_PWM_B"), "22": ("glabel", "PITCH_PWM_C"),
    "8": ("glabel", "PITCH_EN"),
    # encoder I2C
    "10": ("label", "ENC_SDA"), "11": ("label", "ENC_SCL"), "38": ("label", "ENC_MUX_RST_N"),
    # IMU SPI
    "23": ("glabel", "IMU_SPI_SCK"), "31": ("glabel", "IMU_SPI_MOSI"), "24": ("glabel", "IMU_SPI_MISO"),
    "25": ("glabel", "IMU_CS_CAMERA"), "9": ("glabel", "IMU_CS_HANDLE"),
    # analog
    "39": ("glabel", "VBAT_SENSE"),
    # grounds
    "1": GND, "40": GND, "41": GND,
}
GLABEL_SHAPE = {"IMU_SPI_MISO": "input", "VBAT_SENSE": "input"}
for num in U1.sym.pins:
    kind, name = U1_MAP.get(num, ("nc", None))
    if kind == "GND":
        connect(U1, num, kind, length=2.54, hide_value=(num != "1"))
    else:
        connect(U1, num, kind, name, shape=GLABEL_SHAPE.get(name, "output"))

# EN RC + module decoupling
passive("R", "R1", "10k", 30.48, 88.9, V33, lbl("ESP_EN"), R0402)
passive("C", "C1", "1uF", 40.64, 88.9, lbl("ESP_EN"), GND, C0402)
passive("C", "C2", "10uF", 55.88, 88.9, V33, GND, C0603)
passive("C", "C3", "100nF", 66.04, 88.9, V33, GND, C0402)

# ---------------------------------------------------------------- U2 TCA9548A
U2 = Inst(SYMS["TCA"], "U2", "TCA9548APWR", 213.36, 101.6, mpn="TCA9548APWR")
U2_MAP = {"24": V33, "23": lbl("ENC_SDA"), "22": lbl("ENC_SCL"), "3": lbl("ENC_MUX_RST_N"),
          "1": GND, "2": GND, "21": GND, "12": GND}
AXES = ["PAN", "ROLL", "PITCH"]
for ch, ax in enumerate(AXES):
    sd, sc = TCA_RIGHT[2 * ch][0], TCA_RIGHT[2 * ch + 1][0]
    U2_MAP[sd] = lbl(f"ENC_{ax}_SDA")
    U2_MAP[sc] = lbl(f"ENC_{ax}_SCL")
for num in U2.sym.pins:
    kind, name = U2_MAP.get(num, ("nc", None))
    connect(U2, num, kind, name)

passive("R", "R3", "2.2k", 157.48, 50.8, V33, lbl("ENC_SDA"), R0402)
passive("R", "R4", "2.2k", 167.64, 50.8, V33, lbl("ENC_SCL"), R0402)
passive("R", "R2", "10k", 177.8, 50.8, V33, lbl("ENC_MUX_RST_N"), R0402)
passive("C", "C4", "100nF", 187.96, 50.8, V33, GND, C0402)

# ---------------------------------------------------------------- encoder channels
JST = "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal"
for ch, ax in enumerate(AXES):
    by = 45.72 + ch * 50.8
    sda, scl = f"ENC_{ax}_SDA", f"ENC_{ax}_SCL"
    passive("R", f"R{5 + 2 * ch}", "4.7k", 254.0, by, V33, lbl(sda), R0402)
    passive("R", f"R{6 + 2 * ch}", "4.7k", 264.16, by, V33, lbl(scl), R0402)
    J = Inst(SYMS["J"], f"J{1 + ch}", f"ENC_{ax} (JST-SH 4p)", 304.8, by, footprint=JST)
    connect(J, "1", "+3V3")
    connect(J, "2", "GND")
    connect(J, "3", "label", sda)
    connect(J, "4", "label", scl)
    A = Inst(SYMS["AS"], f"U{3 + ch}", "AS5600-ASOM", 350.52, by + 2.54, mpn="AS5600-ASOM")
    connect(A, "1", "+3V3")
    connect(A, "2", "+3V3")
    connect(A, "6", "label", sda)
    connect(A, "7", "label", scl)
    connect(A, "3", "nc")
    connect(A, "5", "GND")
    connect(A, "8", "GND")
    connect(A, "4", "GND")
    passive("C", f"C{5 + 2 * ch}", "100nF", 342.9, by + 22.86, V33, GND, C0402)
    passive("C", f"C{6 + 2 * ch}", "1uF", 353.06, by + 22.86, V33, GND, C0402)

# ---------------------------------------------------------------- notes
NOTE = """REV 7.0 - ENCODER I2C ADDRESS PLAN
I2C0 (ENC_SDA = GPIO17, ENC_SCL = GPIO18), 2.2k pull-ups to +3V3 on the upstream bus
  U2 TCA9548A @ 0x70 (A2 = A1 = A0 = GND), RESET_N = GPIO2 (ENC_MUX_RST_N, 10k pull-up)
  ch0 PAN   -> J1 -> U3 AS5600 @ 0x36   (4.7k pull-ups, encoder side of the mux)
  ch1 ROLL  -> J2 -> U4 AS5600 @ 0x36
  ch2 PITCH -> J3 -> U5 AS5600 @ 0x36
  ch3..ch7 unused (no-connect)
AS5600 address 0x36 is fixed, so the three encoders can only share I2C0 behind the mux.
U3..U5 sit on the remote encoder flex boards; drawn here with their 3.3V-mode wiring
(VDD5V tied to VDD3V3, DIR = GND, PGO = GND, OUT unused).
GPIO22-25 do not exist on ESP32-S3; Rev6 SCL=GPIO22 bug fixed (SCL now GPIO18).
Motor / IMU / VBAT nets are global labels only (drivers + IMUs on other sheets / future revs).
GPIO map source of truth: firmware/src/gimbal_pins.h"""
items.append(f"(text {q(NOTE)} (exclude_from_sim no) (at 20.32 200.66 0) "
             f"(effects (font (size 1.524 1.524)) (justify left top)) (uuid {q(U('text/address-plan'))}))")


def render():
    libs = "\n  ".join(s.sexpr for s in SYMS.values())
    body = "\n  ".join(items)
    return f"""(kicad_sch
  (version 20231120)
  (generator "gen_rev7_schematic")
  (generator_version "8.0")
  (uuid {q(ROOT)})
  (paper "A3")
  (title_block
    (title "3-Axis Ultra-Smooth Gimbal Controller (DJI Osmo Pocket Form Factor)")
    (rev "7.0")
    (company "Sreesai Arjun Engineering")
    (comment 1 "ESP32-S3 Dual LX7 + 3x TMC6300 FOC Drivers + Dual ICM-42688-P IMUs")
    (comment 2 "Production Revision 7.0 / 150 - TCA9548A AS5600 address-conflict resolution + validated ESP32-S3 GPIO map")
  )
  (lib_symbols
  {libs}
  )
  {body}
  (sheet_instances
    (path "/" (page "1"))
  )
)
"""


def verify(netlist_path):
    txt = open(netlist_path).read()
    nets = {}
    for m in re.finditer(r'\(net \(code "\d+"\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)', txt, re.S):
        name = m.group(1).lstrip("/")
        nodes = set(re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)))
        nets[name] = nodes
    ok = True
    for net, want in sorted(expected.items()):
        want = {(r, p) for r, p in want if not r.startswith("#")}
        got = nets.get(net)
        if got is None:
            print(f"FAIL missing net {net}")
            ok = False
            continue
        if want != got:
            print(f"FAIL {net}: missing={sorted(want - got)} extra={sorted(got - want)}")
            ok = False
        else:
            print(f"ok   {net}: {' '.join(f'{r}.{p}' for r, p in sorted(got))}")
    extra = [n for n in nets if n not in expected and not n.startswith("unconnected-")]
    if extra:
        print("FAIL unexpected nets:", extra)
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify-netlist")
    ap.add_argument("-o", "--output", default=SCH_PATH)
    a = ap.parse_args()
    if a.verify_netlist:
        sys.exit(0 if verify(a.verify_netlist) else 1)
    with open(a.output, "w", newline="\n") as fh:
        fh.write(render())
    print(f"wrote {a.output}")


if __name__ == "__main__":
    main()
