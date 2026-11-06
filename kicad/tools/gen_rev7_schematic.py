#!/usr/bin/env python3
"""Generate the Rev 56.0 schematic for the osmo-pocket-esp32-gimbal controller.

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
  * Rev 56: U6 TPS563201 VBAT->+5V buck (J4, C11-C15, L1, R11-R14, TP1/TP3) and the
    handle-IMU analog island: U7 ICM-42688-P on +3V3_IMU (FB1 ferrite) / GNDA, joined to
    GND only through net tie NT1; R15 MISO series termination, C16-C18, TP2/TP4-TP7

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


def inductor():
    arcs = []
    for k in range(4):
        y1 = 2.54 - k * 1.27
        arcs.append(f"(arc (start 0 {f(y1)}) (mid 0.635 {f(y1 - 0.635)}) (end 0 {f(y1 - 1.27)}) "
                    f"(stroke (width 0) (type default)) (fill (type none)))")
    sx = f"""(symbol "{LIB}:L" (pin_numbers hide) (pin_names (offset 1.016) hide) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("L", "L", (2.032, 0), (0, 0), "", "~", "Inductor / ferrite bead")}
    (symbol "L_0_1"
      {chr(10).join('      ' + a for a in arcs).lstrip()}
    )
    (symbol "L_1_1"
      {_pin("passive", 0, 3.81, 270, 1.27, "1", "1")}
      {_pin("passive", 0, -3.81, 90, 1.27, "2", "2")}
    )
  )"""
    return LibSym("L", sx, {"1": (0, 3.81, 270, "1"), "2": (0, -3.81, 90, "2")})


def net_tie():
    """2-pin net tie; KiCad 8 treats it as a tie through the footprint's net_tie_pad_groups."""
    sx = f"""(symbol "{LIB}:NetTie_2" (pin_numbers hide) (pin_names (offset 0) hide) (exclude_from_sim no) (in_bom no) (on_board yes)
    {_props("NT", "NetTie_2", (2.032, 0), (0, 0), "NetTie:NetTie-2_SMD_Pad0.5mm", "~", "Net tie, 2 pins (single-point GND/GNDA star)")}
    (property "ki_keywords" "net tie short" (at 0 0 0) {FONT_HIDE})
    (symbol "NetTie_2_0_1"
      (polyline (pts (xy 0 1.27) (xy 0 -1.27)) (stroke (width 0.762) (type default)) (fill (type none)))
    )
    (symbol "NetTie_2_1_1"
      {_pin("passive", 0, 2.54, 270, 1.27, "1", "1")}
      {_pin("passive", 0, -2.54, 90, 1.27, "2", "2")}
    )
  )"""
    s = LibSym("NetTie_2", sx, {"1": (0, 2.54, 270, "1"), "2": (0, -2.54, 90, "2")})
    s.in_bom = False
    return s


def test_point():
    """1-pin test point, pin to the left (glabel/power attaches on the left)."""
    sx = f"""(symbol "{LIB}:TestPoint" (pin_numbers hide) (pin_names (offset 0.762) hide) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("TP", "TestPoint", (2.54, 1.27), (2.54, -1.27), "TestPoint:TestPoint_Pad_D1.0mm", "~", "Test point (1.0 mm SMD pad)")}
    (symbol "TestPoint_0_1"
      (circle (center 0.762 0) (radius 0.762) (stroke (width 0) (type default)) (fill (type none)))
    )
    (symbol "TestPoint_1_1"
      {_pin("passive", -2.54, 0, 0, 2.54, "1", "1")}
    )
  )"""
    return LibSym("TestPoint", sx, {"1": (-2.54, 0, 0, "1")})


def conn2_batt():
    pins = {"1": (-5.08, 0, 0, "VBAT+"), "2": (-5.08, -P, 0, "VBAT-")}
    psx = [_pin("passive", -5.08, y, 0, 3.81, nm, n) for n, (_, y, _, nm) in pins.items()]
    sx = f"""(symbol "{LIB}:Conn_01x02_Battery" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    {_props("J", "Conn_01x02_Battery", (0, 6.35), (0, 3.81), "", "~", "2-pin battery connector (1 = VBAT+, 2 = GND); MP pads mechanical only")}
    (symbol "Conn_01x02_Battery_0_1"
      (rectangle (start -1.27 1.27) (end 7.62 -3.81) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "Conn_01x02_Battery_1_1"
      {chr(10).join('      ' + p for p in psx).lstrip()}
    )
  )"""
    s = LibSym("Conn_01x02_Battery", sx, pins)
    s.top, s.bot = 1.27, -3.81
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

# ---------------------------------------------------------------- Rev 56 symbols
ARROW_UP = ("(polyline (pts (xy -0.762 1.27) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))\n"
                      "      (polyline (pts (xy 0 0) (xy 0 2.54)) (stroke (width 0) (type default)) (fill (type none)))\n"
                      "      (polyline (pts (xy 0 2.54) (xy 0.762 1.27)) (stroke (width 0) (type default)) (fill (type none)))")
GND_TRI = ("(polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27)) "
           "(stroke (width 0) (type default)) (fill (type none)))")
TPS_LEFT = [("3", "VIN", PIN), ("5", "EN", INP), ("1", "GND", PIN)]
TPS_RIGHT = [("6", "VBST", INP), ("2", "SW", "power_out"), ("4", "VFB", INP)]
NCP = PAS  # RESV: passive pin + explicit no-connect flag (no_connect type + flag trips ERC warnings)
ICM_LEFT = [("8", "VDD", PIN), ("5", "VDDIO", PIN), ("6", "GND", PIN), ("7", "RESV", PAS),
            ("2", "RESV", NCP), ("3", "RESV", NCP), ("10", "RESV", NCP), ("11", "RESV", NCP)]
ICM_RIGHT = [("12", "AP_CS", INP), ("13", "AP_SCLK", INP), ("14", "AP_SDI", INP), ("1", "AP_SDO", OUT),
             ("4", "INT1", OUT), ("9", "INT2/FSYNC", INP)]
SYMS.update({
    "TPS": ic_symbol("TPS563201DDCR", "U", "TPS563201DDCR", TPS_LEFT, TPS_RIGHT, hw=7.62,
                     footprint="Package_TO_SOT_SMD:SOT-23-6",
                     datasheet="https://www.ti.com/lit/ds/symlink/tps563201.pdf",
                     desc="4.5-17V 3A synchronous buck, D-CAP2, 580 kHz, SOT-23-6"),
    "ICM": ic_symbol("ICM-42688-P", "U", "ICM-42688-P", ICM_LEFT, ICM_RIGHT, hw=10.16,
                     footprint="Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y",
                     datasheet="https://invensense.tdk.com/wp-content/uploads/2020/04/ds-000347_icm-42688-p-datasheet.pdf",
                     desc="6-axis MEMS IMU (gyro + accel), SPI/I2C, LGA-14 3x2.5 mm"),
    "L": inductor(),
    "NT": net_tie(),
    "TP": test_point(),
    "J2": conn2_batt(),
    "VBAT": power_sym("VBAT", ARROW_UP, 3.556, "Power symbol creates a global label with name \"VBAT\""),
    "+5V": power_sym("+5V", ARROW_UP, 3.556, "Power symbol creates a global label with name \"+5V\""),
    "+3V3_IMU": power_sym("+3V3_IMU", ARROW_UP, 3.556,
                          "Power symbol creates a global label with name \"+3V3_IMU\" (ferrite-isolated IMU rail)"),
    "GNDA": power_sym("GNDA", GND_TRI, -3.81,
                      "Power symbol creates a global label with name \"GNDA\" , analog (IMU island) ground"),
})

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
        elif sym.name in ("R", "C", "L", "NetTie_2", "TestPoint"):
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
        bom = "no" if (sym.power or not getattr(sym, "in_bom", True)) else "yes"
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


def flagged(kind, x, y, dx=7.62):
    """Power symbol + PWR_FLAG pair (power symbol at x, flag dx mm to the right)."""
    power(kind, x, y)
    wire((x, y), (x + dx, y))
    flag(x + dx, y)
    expected.setdefault(kind, set()).add((f"#FLG0{flg_count[0]}", "1"))


def wire(a, b):
    items.append(f"(wire (pts (xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])})) "
                 f"(stroke (width 0) (type default)) (uuid {q(U(f'wire/{f(a[0])},{f(a[1])}-{f(b[0])},{f(b[1])}'))}))")


ANG = {(-1, 0): 180, (1, 0): 0, (0, -1): 90, (0, 1): 270}
JUST = {0: "left bottom", 90: "left bottom", 180: "right bottom", 270: "right bottom"}
GJUST = {0: "left", 90: "left", 180: "right", 270: "right"}
PWR_ROT = {"GND": {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270},
           "+3V3": {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}}
PWR_ROT["GNDA"] = PWR_ROT["GND"]
for _k in ("VBAT", "+5V", "+3V3_IMU"):
    PWR_ROT[_k] = PWR_ROT["+3V3"]


def connect(inst, num, kind, name=None, length=5.08, shape="output", hide_value=False):
    """Attach a pin: kind in {'nc', 'label', 'glabel'} or a power-symbol name (see PWR_ROT)."""
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


def passive(sym, ref, value, x, y, top, bottom, fp, mpn=None):
    """Vertical R/C/L/net-tie at (x, y). top/bottom = (kind, name)."""
    i = Inst(SYMS[sym], ref, value, x, y, footprint=fp, mpn=mpn)
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

# ---------------------------------------------------------------- Rev 56: 5V buck
C0805 = "Capacitor_SMD:C_0805_2012Metric"
VBAT = ("VBAT", None)
V5 = ("+5V", None)
V33I = ("+3V3_IMU", None)
GNDA = ("GNDA", None)
TP_FP = "TestPoint:TestPoint_Pad_D1.0mm"

flagged("VBAT", 165.1, 190.5)
flagged("+5V", 185.42, 190.5)

J4 = Inst(SYMS["J2"], "J4", "BATT 2S (JST-PH 2p)", 175.26, 208.28,
          footprint="Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal", mpn="S2B-PH-SM4-TB")
connect(J4, "1", "VBAT")
connect(J4, "2", "GND")

U6 = Inst(SYMS["TPS"], "U6", "TPS563201DDCR", 215.9, 210.82, mpn="TPS563201DDCR")
U6_MAP = {"3": VBAT, "5": lbl("EN_5V"), "1": GND, "6": lbl("BST_5V"), "2": lbl("SW_5V"), "4": lbl("FB_5V")}
for num in U6.sym.pins:
    connect(U6, num, *U6_MAP[num])

BUCK_X = [165.1, 184.15, 203.2, 222.25, 241.3]
# input side: hot-loop caps, EN UVLO divider, bootstrap
passive("C", "C11", "10uF 25V X7R", BUCK_X[0], 231.14, VBAT, GND, C0805)
passive("C", "C12", "100nF 25V X7R", BUCK_X[1], 231.14, VBAT, GND, C0402)
passive("R", "R13", "100k 1%", BUCK_X[2], 231.14, VBAT, lbl("EN_5V"), R0402)
passive("R", "R14", "24.9k 1%", BUCK_X[3], 231.14, lbl("EN_5V"), GND, R0402)
passive("C", "C13", "100nF 16V", BUCK_X[4], 231.14, lbl("BST_5V"), lbl("SW_5V"), C0402)
# output side: inductor, COUT, feedback divider (Kelvin-sensed at COUT)
passive("L", "L1", "3.3uH 5A", BUCK_X[0], 256.54, lbl("SW_5V"), V5, "Inductor_SMD:L_Coilcraft_XGL4030",
        mpn="XGL4030-332MEC")
passive("C", "C14", "22uF 10V X5R", BUCK_X[1], 256.54, V5, GND, C0805)
passive("C", "C15", "22uF 10V X5R", BUCK_X[2], 256.54, V5, GND, C0805)
passive("R", "R11", "54.9k 1%", BUCK_X[3], 256.54, V5, lbl("FB_5V"), R0402)
passive("R", "R12", "10k 1%", BUCK_X[4], 256.54, lbl("FB_5V"), GND, R0402)


def test_point(ref, value, x, y, kind, name=None):
    t = Inst(SYMS["TP"], ref, value, x, y, footprint=TP_FP)
    connect(t, "1", kind, name, length=2.54 if name is None else 5.08,
            shape=GLABEL_SHAPE.get(name, "output"))
    return t



# ---------------------------------------------------------------- Rev 56: handle IMU analog island
flagged("+3V3_IMU", 365.76, 190.5, dx=12.7)
flagged("GNDA", 391.16, 190.5)

U7 = Inst(SYMS["ICM"], "U7", "ICM-42688-P", 325.12, 205.74, mpn="ICM-42688-P")
U7_MAP = {"8": V33I, "5": V33I, "6": GNDA, "7": GNDA,
          "12": ("glabel", "IMU_CS_HANDLE"), "13": ("glabel", "IMU_SPI_SCK"),
          "14": ("glabel", "IMU_SPI_MOSI"), "1": lbl("IMU_SDO_RAW")}
for num in U7.sym.pins:
    kind, name = U7_MAP.get(num, ("nc", None))      # 2, 3, 4, 9, 10, 11 -> no-connect
    connect(U7, num, kind, name, length=2.54 if kind in PWR_ROT else 5.08, shape="input")

IMU_X = [297.18, 316.23, 335.28, 354.33, 373.38, 392.43]
passive("L", "FB1", "600R@100MHz", IMU_X[0], 228.6, V33, V33I, "Inductor_SMD:L_0402_1005Metric",
        mpn="BLM15AG601SN1D")
passive("C", "C16", "1uF 10V X5R", IMU_X[1], 228.6, V33I, GNDA, C0402)
passive("C", "C17", "100nF 10V X7R", IMU_X[2], 228.6, V33I, GNDA, C0402)
passive("C", "C18", "100nF 10V X7R", IMU_X[3], 228.6, V33I, GNDA, C0402)
passive("NT", "NT1", "NetTie_2", IMU_X[4], 228.6, GND, GNDA, "NetTie:NetTie-2_SMD_Pad0.5mm")
R15 = Inst(SYMS["R"], "R15", "33R", IMU_X[5], 223.52, footprint=R0402)
connect(R15, "1", "label", "IMU_SDO_RAW", length=2.54)
connect(R15, "2", "glabel", "IMU_SPI_MISO", length=2.54, shape=GLABEL_SHAPE["IMU_SPI_MISO"])

# test points (TP2, TP4-TP7 sit on the digital side of the island moat)
TP_ROWS = [("TP1", "+5V"), ("TP3", "GND"), ("TP2", "+3V3"), ("TP4", "IMU_SPI_SCK"),
           ("TP5", "IMU_SPI_MOSI"), ("TP6", "IMU_SPI_MISO"), ("TP7", "IMU_CS_HANDLE")]
for k, (ref, net) in enumerate(TP_ROWS):
    if net in PWR_ROT:
        test_point(ref, net, 281.94, 228.6 + k * 7.62, net)
    else:
        test_point(ref, net, 281.94, 228.6 + k * 7.62, "glabel", net)

NOTE56 = """REV 56.0 - 5V BUCK (U6) + HANDLE IMU ANALOG ISLAND (U7)
5V BUCK  VBAT = 2S Li-ion 6.0-8.4 V (J4) -> U6 TPS563201DDCR (D-CAP2, fsw = 580 kHz) -> +5V
  VOUT = 0.768 V x (1 + R11/R12) = 0.768 x (1 + 54.9k/10k) = 4.984 V
  EN UVLO: VON = 1.2 V x (1 + R13/R14) = 1.2 x (1 + 100k/24.9k) = 6.02 V (2S cut-off)
  L1 3.3uH: dIL = (VIN-VOUT) x VOUT / (VIN x L x fsw) = 0.44 A p-p @ 6.0 V ... 1.06 A p-p @ 8.4 V
  Hot loop: C12 100nF 0402 (nearest, pad edge <= 1.0 mm) + C11 10uF -> U6 VIN(3) / GND(1),
    GND vias at C11/C12/U6.1 straight into In1.Cu GND (0.21 mm prepreg). No vias on SW_5V.
  C13 bootstrap VBST-SW; COUT = C14 + C15 2x 22uF; FB_5V Kelvin-sensed at COUT, away from SW/L1.
HANDLE IMU ANALOG ISLAND (GNDA)
  +3V3 -> FB1 600R@100MHz ferrite -> +3V3_IMU; C16 1uF + C17 (VDD) / C18 (VDDIO) 100nF to GNDA.
  GNDA is its own net and pour; the ONLY GND-GNDA connection is NT1 (single-point star net tie).
  IMU_SPI_SCK / MOSI / MISO / CS_HANDLE cross the moat beside NT1 (return-current bridge).
  R15 33R source-terminates MISO at U7 (IMU_SDO_RAW -> IMU_SPI_MISO).
  U7 pin 7 RESV -> GNDA; pins 2, 3, 4 (INT1), 9 (INT2), 10, 11 not connected.
  Test points: TP1 +5V, TP2 +3V3, TP3 GND, TP4-TP7 IMU SPI + CS (digital side of the moat)."""
items.append(f"(text {q(NOTE56)} (exclude_from_sim no) (at 20.32 241.3 0) "
             f"(effects (font (size 1.524 1.524)) (justify left top)) (uuid {q(U('text/rev56-buck-imu'))}))")

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
Motor / camera-IMU / VBAT_SENSE nets are global labels only (handle IMU U7: Rev 56 section).
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
    (rev "56.0")
    (company "Sreesai Arjun Engineering")
    (comment 1 "ESP32-S3 Dual LX7 + 3x TMC6300 FOC Drivers + Dual ICM-42688-P IMUs")
    (comment 2 "Production Revision 56.0 / 150 - 5V buck hot loop + handle-IMU GNDA island")
    (comment 3 "Rev 7.0 - TCA9548A AS5600 address fix + validated ESP32-S3 GPIO map")
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
