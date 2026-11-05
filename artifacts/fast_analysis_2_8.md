# Pitch-Axis Counterweight Balancer with Slotted Sliding Lead Weights (Cycle 2)

This OpenSCAD module models the counterweight assembly for a gimbal pitch axis. A slotted arm extends from the pitch hub. One or more carriages slide along the arm and clamp in place with a thumbscrew. Slotted lead discs slide onto a post on each carriage, like weights on a slotted-mass hanger.

**Cycle 2 changes:**
- **Keyed carriage:** A tongue on the carriage rides in the arm slot, so the carriage cannot yaw.
- **Engraved scale:** The arm has graduated ticks for repeatable balancing.
- **Clamp plate:** A clamp plate with a captive nut replaces the loose nut.
- **Alternating disc slots:** Disc slots alternate direction so the stack cannot walk off the post.
- **Retaining clip:** A snap-on clip locks the top of the stack.
- **Printable casting mold:** A two-part mold for casting the lead discs is included, since lead cannot be printed.
- **Mass and torque estimates:** These are reported in the console with `echo`.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights — CYCLE 2
//  Units: mm
// =====================================================================

/* ---------------- Global parameters ---------------- */
$fn = 64;
eps = 0.01;
clr = 0.25;                 // printing clearance

// Pitch hub (608 bearing seat)
hub_od        = 32;
hub_len       = 14;
brg_od        = 22;
brg_w         = 7;
shaft_d       = 8.2;

// Counterweight arm
arm_len       = 120;        // from pitch axis to arm tip
arm_w         = 20;
arm_t         = 6;
slot_w        = 4.4;        // M4 clearance
slot_start    = 25;         // slot begins this far from axis
slot_end_gap  = 8;          // solid material at tip
tick_pitch    = 5;          // scale graduations
arm_tilt      = 0;          // arm angle about pitch axis (deg)

// Carriage
car_len       = 26;
car_wall      = 3;
car_top       = 5;
key_h         = 2.5;        // tongue depth into slot
post_d        = 8;          // post for slotted weights
post_h        = 30;
bolt_d        = 4.3;
nut_af        = 7.0;        // M4 nut across flats
nut_h         = 3.4;

// Slotted lead discs
disc_d        = 34;
disc_t        = 5;
disc_slot_w   = post_d + 0.6;
lead_density  = 11.34;      // g/cm^3

// Assembly configuration
carriage_positions = [70, 105];   // distance from axis (mm)
discs_per_carriage = [3, 5];

/* ---------------- Utility ---------------- */
module slot2d(len, w) {
    hull() {
        circle(d = w);
        translate([len, 0]) circle(d = w);
    }
}

module hex_nut_trap(af, h) {
    cylinder(d = af / cos(30), h = h, $fn = 6);
}

/* ---------------- Pitch hub ---------------- */
module pitch_hub() {
    difference() {
        rotate([0, 90, 0]) cylinder(d = hub_od, h = hub_len, center = true);
        // bearing seats both sides
        for (s = [-1, 1])
            translate([s * (hub_len / 2 - brg_w / 2 + eps), 0, 0])
                rotate([0, 90, 0])
                    cylinder(d = brg_od + clr, h = brg_w, center = true);
        // shaft through
        rotate([0, 90, 0]) cylinder(d = shaft_d, h = hub_len + 2, center = true);
    }
}

/* ---------------- Slotted arm ---------------- */
module counterweight_arm() {
    slot_len = arm_len - slot_start - slot_end_gap;
    difference() {
        union() {
            // bar (extends along -Y)
            hull() {
                translate([-arm_w / 2, -arm_len + arm_w / 2, -arm_t / 2])
                    cube([arm_w, arm_len - arm_w / 2 - hub_od / 2 + 4, arm_t]);
                translate([0, -arm_len + arm_w / 2, -arm_t / 2])
                    cylinder(d = arm_w, h = arm_t);
            }
            // gusset into hub
            hull() {
                rotate([0, 90, 0]) cylinder(d = hub_od - 2, h = arm_w, center = true);
                translate([-arm_w / 2, -hub_od / 2 - 12, -arm_t / 2])
                    cube([arm_w, 1, arm_t]);
            }
        }
        // hub clearance for bearings and shaft
        rotate([0, 90, 0]) cylinder(d = brg_od + clr, h = hub_len + 2, center = true);
        // adjustment slot
        translate([0, -slot_start, -arm_t])
            linear_extrude(arm_t * 2)
                rotate(-90) slot2d(slot_len, slot_w);
        // engraved scale (top face)
        for (d = [slot_start : tick_pitch : arm_len - slot_end_gap]) {
            major = (d % 10 == 0);
            translate([arm_w / 2 - (major ? 4 : 2.5), -d - 0.3, arm_t / 2 - 0.6])
                cube([major ? 4 + eps : 2.5 + eps, 0.6, 1]);
        }
    }
    pitch_hub();
}

/* ---------------- Sliding carriage ---------------- */
module weight_carriage() {
    // sits on top face of arm, local origin = arm top centre
    difference() {
        union() {
            // saddle body wraps over the sides of the arm
            translate([-(arm_w / 2 + car_wall), -car_len / 2, -arm_t * 0.6])
                cube([arm_w + 2 * car_wall, car_len, arm_t * 0.6 + car_top]);
            // key tongue riding in slot
            translate([0, 0, -key_h])
                linear_extrude(key_h + eps)
                    translate([0, car_len / 2 - slot_w / 2 - 2])
                        rotate(-90) slot2d(car_len - slot_w - 4, slot_w - 2 * clr);
            // weight post
            translate([0, 0, car_top - eps]) cylinder(d = post_d, h = post_h);
            // clip groove collar is cut below
        }
        // arm channel
        translate([-(arm_w / 2 + clr), -car_len, -arm_t - 1])
            cube([arm_w + 2 * clr, car_len * 2, arm_t + 1]);
        // clamp bolt (enters from below, threads into nut in post base)
        translate([0, 0, -arm_t - 5]) cylinder(d = bolt_d, h = arm_t + car_top + 20);
        translate([0, 0, car_top - nut_h - 0.5]) hex_nut_trap(nut_af + clr, nut_h + 0.5 + eps);
        // retaining-clip groove near post top
        translate([0, 0, car_top + post_h - 4])
            difference() {
                cylinder(d = post_d + 1, h = 1.6);
                cylinder(d = post_d - 1.6, h = 1.6);
            }
        // lightening / bolt access bore in post
        translate([0, 0, car_top]) cylinder(d = bolt_d, h = post_h + 1);
    }
}

/* ---------------- Clamp plate (under arm) ---------------- */
module clamp_plate() {
    difference() {
        translate([-(arm_w / 2), -car_len / 2 + 2, -3])
            cube([arm_w, car_len - 4, 3]);
        translate([0, 0, -5]) cylinder(d = bolt_d, h = 10);
    }
}

/* ---------------- Thumbscrew knob ---------------- */
module thumb_knob() {
    difference() {
        union() {
            cylinder(d = 18, h = 6);
            for (a = [0 : 60 : 359])
                rotate(a) translate([9, 0, 0]) cylinder(d = 4, h = 6);
        }
        translate([0, 0, 1.5]) hex_nut_trap(7.0 + clr, 10); // M4 bolt head trap
        translate([0, 0, -1]) cylinder(d = bolt_d, h = 10);
    }
}

/* ---------------- Slotted lead disc ---------------- */
module slotted_disc(flip = false) {
    rotate(flip ? 180 : 0)
    difference() {
        cylinder(d = disc_d, h = disc_t);
        translate([0, 0, -1]) cylinder(d = post_d + 0.6, h = disc_t + 2);
        translate([0, -disc_slot_w / 2, -1]) cube([disc_d, disc_slot_w, disc_t +