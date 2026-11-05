# Pitch-Axis Counterweight Balancer: OpenSCAD Module

This is a parametric OpenSCAD design for the counterweight arm of a camera cradle. It has these parts:

- **Pitch hub** with pockets for 608 bearings.
- **Slotted counterweight arm** with an engraved position scale.
- **Sliding carriage** that straddles the arm. It is locked by a thumb-nut clamp under the arm.
- **Slotted lead weight discs.** Each disc has a radial slot so it can be added or removed without taking off the clamp nut. Alternating slot orientation keeps the stack captive.
- **Console report** of total lead mass and the balancing moment about the pitch axis.

I treated "Cycle 25" as a design-revision label and tagged the file with it. If it refers to a specific spec you're working from, send those requirements and I'll match them.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle - Counterweight Balancing Mechanism
//  Slotted Sliding Lead Weights  |  Design Revision: Cycle 25
//  Units: mm.  Pitch axis = global X axis through origin.
//  Counterweight arm extends along -Y (rearward of the pitch axis).
// =====================================================================

/* ---------------- Render control ---------------- */
// "assembly" | "arm" | "carriage" | "clamp" | "weight" | "weight_mold"
part = "assembly";
$fn  = 64;

/* ---------------- Top-level parameters ---------------- */
arm_len        = 160;   // hub center to arm tip
arm_w          = 24;    // arm width (X)
arm_t          = 8;     // arm thickness (Z)
slot_w         = 6.4;   // M6 clearance slot
slot_start     = 30;    // slot begins this far from pitch axis
slot_end_margin= 12;    // solid material left at arm tip

hub_r          = 18;    // pitch hub outer radius
hub_len        = 30;    // hub length along X
brg_od         = 22.1;  // 608 bearing OD (+clearance)
brg_w          = 7;     // 608 bearing width
shaft_d        = 8.4;   // pitch shaft clearance

stud_d         = 6;     // M6 stud
weight_d       = 40;    // slotted weight diameter
weight_t       = 6;     // thickness of each lead disc
weight_slot    = 6.6;   // radial slot width (stud clearance)
n_weights      = 4;     // number of discs on the stack
weight_pos     = 110;   // carriage position (distance from pitch axis)

carriage_wall  = 3;     // saddle cheek thickness
carriage_h     = 6;     // saddle top plate thickness
carriage_len   = 30;    // saddle length along arm
clr            = 0.3;   // sliding clearance

lead_density   = 11.34; // g/cm^3

/* =====================================================================
   Main module
   ===================================================================== */
module pitch_counterweight(
    arm_len = arm_len, n_weights = n_weights, weight_pos = weight_pos)
{
    pos = clamp_pos(weight_pos, arm_len);

    color("SteelBlue")  counterweight_arm(arm_len);
    translate([0, -pos, arm_t/2]) {
        color("Orange")  carriage();
        // Weight stack sits on carriage top plate
        translate([0, 0, carriage_h])
            for (i = [0 : n_weights - 1])
                translate([0, 0, i * (weight_t + 0.2)])
                    rotate([0, 0, (i % 2) * 180])       // alternate slots
                        color("DimGray") slotted_weight();
        // Retaining knob on stud
        translate([0, 0, carriage_h + n_weights * (weight_t + 0.2)])
            color("Black") knob();
    }
    // Clamp plate + thumb nut under arm
    translate([0, -pos, -arm_t/2]) color("Gold") clamp_plate();

    report(n_weights, pos);
}

/* ---------------- Arm with pitch hub ---------------- */
module counterweight_arm(L) {
    difference() {
        union() {
            // Hub (axis along X)
            rotate([0, 90, 0]) cylinder(r = hub_r, h = hub_len, center = true);
            // Arm beam
            translate([-arm_w/2, -L, -arm_t/2])
                cube([arm_w, L, arm_t]);
            // Rounded tip
            translate([0, -L, 0]) cylinder(d = arm_w, h = arm_t, center = true);
        }
        // Shaft bore
        rotate([0, 90, 0]) cylinder(d = shaft_d, h = hub_len + 2, center = true);
        // Bearing pockets both ends
        for (s = [-1, 1])
            translate([s * (hub_len/2 - brg_w/2 + 0.01), 0, 0])
                rotate([0, 90, 0]) cylinder(d = brg_od, h = brg_w + 0.02, center = true);
        // Sliding slot
        hull() for (y = [slot_start, L - slot_end_margin])
            translate([0, -y, 0]) cylinder(d = slot_w, h = arm_t + 2, center = true);
        // Engraved scale every 5 mm, long tick every 10 mm
        for (y = [slot_start : 5 : L - slot_end_margin]) {
            tick = (y % 10 == 0) ? 5 : 3;
            for (s = [-1, 1])
                translate([s * (arm_w/2 - tick/2), -y, arm_t/2 - 0.4])
                    cube([tick, 0.6, 1], center = true);
        }
    }
}

/* ---------------- Sliding carriage (saddle + stud) ---------------- */
module carriage() {
    inner = arm_w + 2 * clr;
    outer = inner + 2 * carriage_wall;
    difference() {
        union() {
            // Top plate
            translate([-outer/2, -carriage_len/2, 0])
                cube([outer, carriage_len, carriage_h]);
            // Side cheeks straddling the arm
            for (s = [-1, 1])
                translate([s * (inner/2 + carriage_wall/2) - carriage_wall/2,
                           -carriage_len/2, -arm_t * 0.75])
                    cube([carriage_wall, carriage_len, arm_t * 0.75]);
        }
        // Stud through-hole (M6 bolt passes up from clamp)
        translate([0, 0, -1]) cylinder(d = stud_d + clr, h = carriage_h + 2);
        // Index window to read the scale
        translate([inner/2 - 3, -0.5, -1]) cube([carriage_wall + 4, 1, carriage_h + 2]);
    }
    // Stud (visual: M6 bolt shank)
    stack_h = n_weights * (weight_t + 0.2) + 10;
    color("Silver") translate([0, 0, -arm_t - 4])
        cylinder(d = stud_d, h = arm_t + 4 + carriage_h + stack_h);
}

/* ---------------- Slotted lead weight disc ---------------- */
module slotted_weight() {
    difference() {
        cylinder(d = weight_d, h = weight_t);
        // Central hole
        translate([0, 0, -1]) cylinder(d = weight_slot, h = weight_t + 2);
        // Radial loading slot
        translate([0, -weight_slot/2, -1])
            cube([weight_d, weight_slot, weight_t + 2]);
        // Edge chamfer
        translate([0, 0, weight_t - 0.8])
            difference() {
                cylinder(d = weight_d + 2, h = 2);
                cylinder(d1 = weight_d, d2 = weight_d - 3.2, h = 1.6);
            }
    }
}

/* ---------------- Mold for casting lead discs ---------------- */
// NOTE: Print in high-temp material (or use as a pattern for a
// plaster/sand mold).  Molten lead (~330 C) will melt PLA/PETG.
module weight_mold() {
    wall = 6;
    difference() {
        translate([-(weight_d/2 + wall), -(weight_d/2 + wall), 0])
            cube([weight_d + 2 * wall, weight_d + 2 * wall, weight_t + wall]);
        translate([0, 0, wall]) slotted_weight_negative();
    }
}
module slotted_weight_negative() {
    difference() {
        cylinder(d = weight_d, h = weight_t + 1);
        // Core pins form the hole + slot
        translate([0, 0, -1]) cylinder(d = weight_slot, h = weight_t + 3);
        translate([0, -weight