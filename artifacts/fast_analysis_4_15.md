# Pitch-Axis Counterweight Arm with Slotted Sliding Lead Weights (Cycle 4)

## Design Overview

The pitch axis is the local **Z** axis. The counterweight arm extends along **+X**, away from the camera side.

- **Hub**: a split clamping hub on the pitch shaft, tightened with an M3 screw across a tab.
- **Arm**: a slotted rail with engraved graduation ticks, so you can repeat a balance setting.
- **Carriage**: a U-saddle under the arm that captures an M5 hex bolt head. The bolt stud rises through the slot.
- **Slotted lead plates**: U-slotted plates slide sideways onto the stud without removing the bolt. Alternate plates are flipped 180° so the slot openings interlock, which stops a loose plate from walking off.
- **Thumb knob**: a knurled knob with a captive nut clamps the whole stack to the rail at any position.
- **Moment report**: echoes each plate's mass, each carriage's mass, and the total static moment about the pitch axis.

```openscad
// ============================================================
//  Pitch-Axis Camera Cradle Counterweight — Cycle 4
//  Slotted sliding lead-weight balancing mechanism
//  Units: mm.  Local frame: pitch axis = Z, arm extends +X
// ============================================================

/* [Render] */
part = "assembly"; // [assembly, arm, carriage, plate, knob, plate_pattern]
explode = 0;       // exploded-view spacing (assembly only)

/* [Pitch shaft / hub] */
shaft_d      = 8.0;    // pitch shaft diameter
shaft_clr    = 0.15;   // diametral clearance before clamping
hub_od       = 24;
hub_len      = 14;
clamp_slit   = 1.5;
clamp_screw  = 3.4;    // M3 clearance
clamp_tab_l  = 9;
clamp_tab_w  = 14;

/* [Arm / rail] */
arm_len      = 130;    // from pitch axis to arm tip
arm_w        = 20;
arm_t        = 8;
slot_w       = 5.4;    // M5 clearance
slot_start   = 28;     // closest carriage position to axis
slot_end     = 118;    // farthest carriage position
tick_pitch   = 5;
tick_depth   = 0.6;

/* [Carriage] */
carr_l       = 30;
carr_wall    = 3;
carr_base    = 5;
carr_lip     = 5;      // how far the saddle wraps up the arm sides
carr_clr     = 0.3;
bolt_d       = 5.0;
bolt_head_af = 8.0;    // M5 hex head across flats
bolt_head_h  = 3.6;

/* [Lead plates] */
plate_l      = 30;
plate_w      = 20;
plate_t      = 6;
plate_slot_w = 5.6;
plate_gap    = 0.0;    // visual gap between stacked plates
lead_density = 0.01134; // g/mm^3 (11.34 g/cm^3)

/* [Knob] */
knob_d       = 22;
knob_h       = 10;
nut_af       = 8.0;    // M5 nut across flats
nut_h        = 4.2;
knurls       = 18;

/* [Configuration: carriages] */
// Each entry: [distance from pitch axis (mm), number of plates]
carriages = [ [60, 2], [105, 3] ];

$fn = 64;
eps = 0.01;

// ------------------------------------------------------------
// Derived values
// ------------------------------------------------------------
function hex_d(af) = af / cos(30);   // across-corners diameter

plate_slot_area =
    plate_slot_w * (plate_l / 2) + PI * pow(plate_slot_w / 2, 2) / 2;
plate_vol  = plate_l * plate_w * plate_t - plate_slot_area * plate_t;
plate_mass = plate_vol * lead_density;            // grams

function clamp_pos(x) = min(max(x, slot_start), slot_end);
function stack_h(n)   = n * (plate_t + plate_gap);

function moment(c, i = 0) =
    i >= len(c) ? 0
    : clamp_pos(c[i][0]) * c[i][1] * plate_mass + moment(c, i + 1);

function total_mass(c, i = 0) =
    i >= len(c) ? 0 : c[i][1] * plate_mass + total_mass(c, i + 1);

// ------------------------------------------------------------
// Hub + slotted arm
// ------------------------------------------------------------
module counterweight_arm() {
    difference() {
        union() {
            cylinder(d = hub_od, h = hub_len);
            // Arm beam
            translate([hub_od / 2 - 3, -arm_w / 2, 0])
                cube([arm_len - hub_od / 2 + 3, arm_w, arm_t]);
            // Rounded tip
            translate([arm_len, 0, 0]) cylinder(d = arm_w, h = arm_t);
            // Clamp tab (opposite side to arm)
            translate([-hub_od / 2 - clamp_tab_l + 2, -clamp_tab_w / 2, 0])
                cube([clamp_tab_l, clamp_tab_w, hub_len]);
        }
        // Shaft bore
        translate([0, 0, -eps])
            cylinder(d = shaft_d + shaft_clr, h = hub_len + 2 * eps);
        // Clamp slit
        translate([