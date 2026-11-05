# Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 9)

I don't have the earlier cycles, so this is a self-contained revision. All interface dimensions (bearing size, arm section, bolt size) are parameters at the top. Adjust them to match your cradle.

**What the design includes:**

- **Pitch hub**: a 608 bearing boss plus a flange with bolt holes for mounting to the camera cradle.
- **Counterweight arm**: extends opposite the camera. It has a long clamp slot, graduation ticks every 5 mm (longer every 25 mm), and an end-stop that keeps weights from sliding off.
- **Sliding weight carriages**:
  - The arm passes through a clearance tunnel in each carriage.
  - Four cylindrical pockets hold lead (slugs or shot plus epoxy), with recesses for press-fit caps.
  - An M4 bolt passes through the slot and is clamped by a captive nut in a hex trap.
  - An index notch lines up with the arm ticks so positions are repeatable.
- **Balance calculator**: `echo()` reports the mass and moment of every carriage and the total, so you can compare it against the camera's moment.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle — Counterweight Balancing Mechanism
//  Slotted arm + sliding lead-filled weight carriages      (Cycle 9)
//  Units: mm, grams
// =====================================================================

$fn = 48;

// ---------------- Global parameters ----------------
// Pitch bearing (608 default)
brg_od      = 22;
brg_w       = 7;
brg_clr     = 0.15;
axle_d      = 8;

// Hub
hub_od      = 32;
hub_len     = 14;

// Counterweight arm (flat bar)
arm_len     = 130;      // from pitch axis to arm tip
arm_w       = 18;
arm_t       = 6;
arm_drop    = 0;        // Z offset of arm centre below pitch axis (CG trim)
slot_w      = 4.4;      // M4 clearance
slot_start  = 30;       // distance from axis where slot begins
slot_end    = arm_len - 10;
tick_pitch  = 5;
stop_h      = 4;        // end-stop lip height

// Sliding weight carriage
clr         = 0.35;     // sliding clearance around arm
car_len     = 22;       // along the arm
side_wall   = 9;        // room for lead pockets beside bolt
top_h       = 20;       // body above the arm (holds lead)
bot_h       = 7;        // body below the arm (nut trap)
lead_d      = 9.5;      // lead slug / pocket diameter
lead_depth  = 16;
cap_recess  = 1.5;      // recess for press-fit pocket caps
bolt_d      = 4.4;
nut_af      = 7.2;      // M4 nut across-flats + clearance
nut_h       = 3.4;

// Materials
rho_lead    = 11.34e-3; // g/mm^3
rho_plastic = 1.24e-3;  // PLA, g/mm^3
infill      = 0.6;      // effective solid fraction of print

// ---------------- Derived ----------------
car_w = arm_w + 2*clr + 2*side_wall;
tun_w = arm_w + 2*clr;
tun_h = arm_t + 2*clr;

// =====================================================================
//  Top-level module
// =====================================================================
// positions : distances (mm) of each carriage centre from the pitch axis
// explode   : vertical exploded-view offset of carriages
// show_lead : render lead slugs
module pitch_counterweight(positions = [55, 95],
                           explode   = 0,
                           show_lead = true)
{
    color("SlateGray") pitch_hub();
    color("LightSteelBlue") cw_arm();

    for (p = positions) {
        assert(p - car_len/2 >= slot_start - 2 && p + car_len/2 <= arm_len - stop_h,
               str("Carriage at ", p, " mm is outside the slot travel"));
        translate([0, -p, -arm_drop + explode])
            weight_carriage(show_lead);
    }
    balance_report(positions);
}

// =====================================================================
//  Pitch hub (bearing boss + cradle mounting flange)
// =====================================================================
module pitch_hub() {
    difference() {
        union() {
            rotate([0, 90, 0]) cylinder(d = hub_od, h = hub_len, center = true);
            // flange toward cradle (+Y side) with 2x M3 holes
            translate([-hub_len/2, 0, -hub_od/2])
                cube([hub_len, hub_od/2 + 8, 6]);
        }
        // bearing seat (both sides) + axle pass-through
        for (s = [-1, 1])
            translate([s * (hub_len/2 - brg_w/2 + 0.01), 0, 0])
                rotate([0, 90, 0])
                    cylinder(d = brg_od + 2*brg_clr, h = brg_w, center = true);
        rotate([0, 90, 0]) cylinder(d = axle_d + 2, h = hub_len + 2, center = true);
        // cradle bolts
        for (x = [-hub_len/4, hub_len/4])
            translate([x, hub_od/2 + 3, -hub_od/2 - 1]) cylinder(d = 3.3, h = 10);
    }
}

// =====================================================================
//  Slotted counterweight arm
// =====================================================================
module cw_arm() {
    difference() {
        union() {
            // root blend into hub
            hull() {
                rotate([0, 90, 0]) cylinder(d = hub_od * 0.8, h = arm_w, center = true);
                translate([-arm_w/2, -slot_start + 4, -arm_drop - arm_t/2])
                    cube([arm_w, 1, arm_t]);
            }
            // bar
            translate([-arm_w/2, -arm_len, -arm_drop - arm_t/2])
                cube([arm_w, arm_len - slot_start + 5, arm_t]);
            // end stop
            translate([-arm_w/2, -arm_len, -arm_drop + arm_t/2])
                cube([arm_w, stop_h, stop_h]);
        }
        // clear bearing/axle region
        rotate([0, 90, 0]) cylinder(d = brg_od + 2*brg_clr, h = hub_len + 0.2, center = true);
        // clamp slot
        hull() for (y = [slot_start, slot_end])
            translate([0, -y, -arm_drop - arm_t])
                cylinder(d = slot_w, h = arm_t * 2);
        // graduation ticks (every 5 mm, long every 25 mm)
        for (y = [slot_start : tick_pitch : slot_end]) {
            L = (y % 25 == 0) ? 5 : 2.5;
            translate([arm_w/2 - L, -y - 0.3, -arm_drop + arm_t/2 - 0.5])
                cube([L + 0.1, 0.6, 1]);
        }
    }
}

// =====================================================================
//  Sliding weight carriage (centred on its clamp bolt)
// =====================================================================
function pocket_xy() = [
    for (sx = [-1, 1], sy = [-1, 1])
        [sx * (tun_w/2 + side_wall/2 - 0.5 - (side_wall < lead_d + 2 ? 0 : 0)) * 0 +
         sx * (car_w/2 - lead_d/2 - 1.6),
         sy * (car_len/2 - lead_d/2 - 1.6)]
];

module weight_carriage(show_lead = true) {
    difference() {
        // body
        translate([-car_w/2, -car_len/2, -arm_t/2 - clr - bot_h])
            cube([car_w, car_len, bot_h + tun_h + top_h]);
        // arm tunnel
        translate([-tun_w/2, -car_len/2 - 1, -tun_h/2])
            cube([tun_w, car_len + 2, tun_h]);
        // clamp bolt through everything
        translate([0, 0, -50]