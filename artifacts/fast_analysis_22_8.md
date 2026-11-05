# Pitch-Axis Counterweight Arm with Slotted Sliding Lead Weights (Cycle 22)

This is a parametric OpenSCAD design. The counterweight arm clamps onto the pitch shaft opposite the camera. A weight carriage slides along a through-slot in the arm and locks with a thumb-nut. Each lead plate has a U-slot, so you can add or remove plates by loosening the nut, without taking the bolt out.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle — Counterweight Balancing Mechanism
//  Revision: Cycle 22
//  Slotted sliding lead weights on a slotted arm, thumb-nut lock
//  Units: mm
// =====================================================================

$fn = 64;

// ---------------- Global parameters ----------------
// Pitch shaft / hub
shaft_d        = 8.0;     // pitch shaft diameter
shaft_clear    = 0.25;    // bore clearance
hub_d          = 28;      // hub outer diameter
hub_len        = 16;      // hub length along shaft
hub_split_gap  = 1.5;     // clamp split width
clamp_screw_d  = 3.4;     // M3 clamp screw clearance
clamp_nut_af   = 5.7;     // M3 nut across-flats (trap)

// Counterweight arm
arm_len        = 110;     // from shaft centre to arm tip
arm_w          = 18;      // arm width
arm_t          = 8;       // arm thickness
slot_w         = 4.5;     // through-slot width (M4 bolt)
slot_start     = 30;      // slot begins this far from shaft centre
slot_end_margin= 10;      // solid material left at tip
scale_step     = 10;      // graduation pitch (mm)

// Sliding carriage (saddle above arm, tray below)
fit            = 0.35;    // sliding clearance
wall           = 3;
saddle_len     = 26;
tongue_h       = 2.5;     // key that rides in the slot (anti-rotation)
bolt_d         = 4.0;     // M4
bolt_head_d    = 7.6;
bolt_head_h    = 4.2;

// Lead plates
plate_w        = 30;      // across the arm
plate_l        = 26;      // along the arm
plate_t        = 5;       // thickness of each plate
plate_slot_w   = 4.6;     // U-slot width (slips over M4 bolt)
lead_density   = 11.34;   // g/cm^3

// Tray that holds plate stack (prevents plate rotation)
tray_floor     = 2.5;
tray_lip       = 2.0;

// Thumb nut
thumb_d        = 22;
thumb_h        = 7;
m4_nut_af      = 7.1;
m4_nut_h       = 3.4;

// ---------------- Derived values ----------------
slot_len   = arm_len - slot_start - slot_end_margin;
function plate_mass_g() =
    let(vol_mm3 = plate_w*plate_l*plate_t
                  - plate_slot_w*(plate_l/2)*plate_t
                  - PI*pow(plate_slot_w/2,2)/2*plate_t)
    vol_mm3/1000 * lead_density;

// =====================================================================
//  Hub: split clamp on the pitch shaft
// =====================================================================
module cw_hub() {
    difference() {
        union() {
            cylinder(d = hub_d, h = hub_len, center = true);
            // Clamp ears
            translate([0, -hub_d/2 - 4, 0])
                cube([12, 12, hub_len], center = true);
        }
        // Shaft bore
        cylinder(d = shaft_d + shaft_clear, h = hub_len + 2, center = true);
        // Split
        translate([0, -hub_d/2, 0])
            cube([hub_split_gap, hub_d, hub_len + 2], center = true);
        // Clamp screw through ears
        translate([0, -hub_d/2 - 5, 0]) rotate([0, 90, 0]) {
            cylinder(d = clamp_screw_d, h = 30, center = true);
            translate([0, 0, 4])
                cylinder(d = clamp_nut_af/cos(30), h = 10, $fn = 6);
        }
    }
}

// =====================================================================
//  Arm: extends along +X, slotted, with graduations
// =====================================================================
module cw_arm() {
    difference() {
        hull() {
            translate([0, 0, 0])
                cylinder(d = hub_d, h = arm_t, center = true);
            translate([arm_len - arm_w/2, 0, 0])
                cylinder(d = arm_w, h = arm_t, center = true);
        }
        // Shaft bore through arm root
        cylinder(d = shaft_d + shaft_clear, h = arm_t + 2, center = true);
        // Long through-slot for the carriage bolt
        hull() {
            translate([slot_start, 0, 0])
                cylinder(d = slot_w, h = arm_t + 2, center = true);
            translate([slot_start + slot_len, 0, 0])
                cylinder(d = slot_w, h = arm_t + 2, center = true);
        }
        // Graduation marks on the top face (every scale_step, long mark every 5)
        for (x = [slot_start : scale_step : slot_start + slot_len]) {
            long = ((x - slot_start) / scale_step) % 5 == 0;
            translate([x, arm_w/2 - (long ? 4 : 2.5)/2 + 0.01, arm_t/2 - 0.3])
                cube([0.8, long ? 4 : 2.5, 0.8], center = true);
        }
    }
}

module cw_arm_assembly() {
    // Hub sits on the arm root, axis along Z (pitch shaft axis)
    translate([0, 0, arm_t/2 + hub_len/2 - 0.01]) cw_hub();
    cw_arm();
}

// =====================================================================
//  Saddle: rides on top of arm, keyed into the slot
// =====================================================================
module cw_saddle() {
    in_w = arm_w + 2*fit;
    difference() {
        union() {
            // Top plate
            translate([0, 0, wall/2])
                cube([saddle_len, in_w + 2*wall, wall], center = true);
            // Side skirts that straddle the arm
            for (s = [-1, 1])
                translate([0, s*(in_w/2 + wall/2), -arm_t/2 + wall/2 + 0.5])
                    cube([saddle_len, wall, arm_t + wall - 1], center = true);
            // Anti-rotation tongue in the slot
            translate([0, 0, -tongue_h/2])
                cube([saddle_len - 6, slot_w - 2*fit, tongue_h], center = true);
        }
        // Bolt hole + counterbore for bolt head
        cylinder(d = bolt_d + 0.3, h = 40, center = true);
        translate([0, 0, wall - bolt_head_h + 0.01])
            cylinder(d = bolt_head_d, h = bolt_head_h + 1);
        // Pointer notch for reading the scale
        translate([0, in_w/2 + wall, wall])
            rotate([0, 0, 45]) cube([2, 2, 2*wall + 2], center = true);
    }
}

// =====================================================================
//  Lead plate: U-slot from one end to centre
// =====================================================================
module lead_plate() {
    difference() {
        translate([0, 0, plate_t/2])
            cube([plate_l, plate_w, plate_t], center = true);
        // U-slot opening toward -X (toward the hub)
        hull() {
            translate([0, 0, -1])
                cylinder(d = plate_slot_w, h = plate_t + 2);
            translate([-plate_l, 0, -1])
                cylinder(d = plate_slot_w, h = plate_t + 2);
        }
        // Chamfered corners (easier handling)
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx*plate_l/2, sy*plate_w/2, plate_t/2])
                rotate([0, 0, 45]) cube([3, 3, plate_t + 2], center = true);
    }
}

// =====================================================================
//  Tray: hangs under the arm and cradles the plate stack
// =====================================================================
module cw_tray(n_plates) {
    in_l = plate_l + 2*fit;
    in_w = plate_w + 2*fit;
    h    = tray_floor + n_plates*plate_t + tray_lip;
    difference() {
        translate([0, 0, -h/2])
            cube([in_l + 2*wall, in_w + 2*wall, h], center = true);
        // Plate pocket (open on top, i.e. toward the arm)
        translate([0, 0, -h + tray_floor + (h)/2])
            cube([in_l, in