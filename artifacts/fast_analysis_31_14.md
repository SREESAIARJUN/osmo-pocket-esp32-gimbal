# Pitch-Axis Counterweight Balancer: OpenSCAD Module (Cycle 31)

This parametric OpenSCAD design is a counterweight arm for a camera cradle's pitch axis. Slotted lead plates ride on a sliding carriage, which you clamp anywhere along a slotted, graduated arm.

**How it works**
- A clamping hub grips the pitch shaft.
- A slotted arm carries engraved graduation marks.
- A sliding carriage straddles the arm and carries a stack of U-slotted lead plates. The plates load sideways, so you never have to remove the bolt.
- A keyed clamp pad with a captive nut sits under the arm. A single M5 thumbscrew locks the carriage and all plates.
- A removable end-stop pin keeps the carriage from sliding off the arm.
- At render time, the code echoes the counterweight mass and the moment it produces.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer  — Cycle 31
//  Slotted sliding lead-weight mechanism
//  Units: mm  |  Pitch axis = X axis  |  Arm extends along +Y
// =====================================================================

/* [Render] */
part          = "assembly"; // [assembly, arm, carriage, clamp_pad, lead_plate, print_layout]
weight_pos    = 55;   // carriage centre distance from pitch axis (mm)
n_plates      = 3;    // number of lead plates on the carriage
explode       = 0;    // exploded-view spacing (mm)

/* [Pitch Shaft / Hub] */
shaft_d       = 8.0;  // pitch shaft diameter
shaft_clear   = 0.15; // bore clearance
hub_d         = 24;
hub_len       = 14;
clamp_screw_d = 3.4;  // M3 clamp screw
clamp_nut_af  = 5.7;  // M3 nut across flats
slit_w        = 1.2;

/* [Arm] */
arm_len       = 110;  // from axis to tip
arm_w         = 16;
arm_t         = 6;
slot_w        = 5.4;  // M5 clearance slot
slot_start    = 22;   // slot begins this far from axis
slot_end_gap  = 10;   // solid material at arm tip
stop_pin_d    = 3.2;  // end-stop pin / M3 screw
tick_pitch    = 5;    // graduation spacing
tick_depth    = 0.6;

/* [Carriage] */
carr_len      = 26;   // along arm (Y)
carr_wall     = 3;
carr_top_t    = 4;
fit_gap       = 0.3;  // sliding clearance
fence_h       = 3;    // anti-rotation fences for plates

/* [Clamp Bolt] */
bolt_d        = 5.0;  // M5
bolt_clear    = 5.4;
nut_af        = 8.1;  // M5 nut across flats (+clearance)
nut_h         = 4.2;
pad_t         = 7;

/* [Lead Plates] */
plate_w       = 34;   // across arm (X)
plate_l       = carr_len - 0.6; // along arm (Y), fits between fences
plate_t       = 3.0;
plate_chamfer = 0.6;
lead_density  = 11.34e-3; // g/mm^3

$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------------
//  Derived values
// ---------------------------------------------------------------------
carr_w      = arm_w + 2*fit_gap + 2*carr_wall;
slot_len    = arm_len - slot_end_gap - slot_start;
plate_vol   = plate_w*plate_l*plate_t
            - (bolt_clear*(plate_w/2) + PI*pow(bolt_clear/2,2)/2)*plate_t;
plate_mass  = plate_vol * lead_density;

echo(str("Cycle31: one lead plate = ", round(plate_mass*10)/10, " g"));
echo(str("Cycle31: stack of ", n_plates, " = ",
         round(n_plates*plate_mass*10)/10, " g"));
echo(str("Cycle31: lead moment at ", weight_pos, " mm = ",
         round(n_plates*plate_mass*weight_pos), " g*mm"));
echo(str("Cycle31: adjustable range ", slot_start + carr_len/2, " .. ",
         arm_len - slot_end_gap - carr_len/2, " mm"));

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
module hex_prism(af, h) cylinder(d = af/cos(30), h = h, $fn = 6);

module chamfer_box(size, c) {
    hull() {
        translate([c,c,0])        cube([size[0]-2*c, size[1]-2*c, size[2]]);
        translate([0,0,c])        cube([size[0], size[1], size[2]-2*c]);
    }
}

// ---------------------------------------------------------------------
//  1. Counterweight arm with clamping hub
// ---------------------------------------------------------------------
module cw_arm() {
    difference() {
        union() {
            // hub
            rotate([0,90,0]) cylinder(d = hub_d, h = hub_len, center = true);
            // clamp ears (opposite side to arm)
            translate([-hub_len/2, -hub_d/2 - 7, -7])
                cube([hub_len, 7 + hub_d/2, 14]);
            // arm beam, blended into hub
            hull() {
                translate([-arm_w/2, 0, -arm_t/2]) cube([arm_w, eps, arm_t]);
                translate([-hub_len/2, -2, -hub_d/4]) cube([hub_len, eps, hub_d/2]);
            }
            translate([-arm_w/2, 0, -arm_t/2]) cube([arm_w, arm_len, arm_t]);
        }
        // shaft bore
        rotate([0,90,0])
            cylinder(d = shaft_d + 2*shaft_clear, h = hub_len + 2, center = true);
        // clamp slit
        translate([-hub_len/2 - 1, -hub_d/2 - 8, -slit_w/2])
            cube([hub_len + 2, hub_d/2 + 8, slit_w]);
        // clamp screw + captive nut
        translate([0, -hub_d/2 - 3.2, -10]) {
            cylinder(d = clamp_screw_d, h = 20);
            hex_prism(clamp_nut_af, 3 + 2.4);
        }
        // adjustment slot
        hull() {
            translate([0, slot_start,