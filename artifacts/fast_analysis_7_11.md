# Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 7)

This is a parametric OpenSCAD module. A counterweight arm is fixed to the pitch-axis hub and has a long adjustment slot. A saddle carriage slides along the arm and locks in place with a thumbscrew. Slotted lead plates slip sideways onto a stud under the carriage, so you can add or remove weight without taking the stud apart. A knurled nut holds the stack in place.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights  |  Cycle 7
//  Units: mm      Pitch axis = Y      Arm extends along -X
// =====================================================================

$fn = 64;

// ---------------- Global parameters ----------------
/* [Pitch hub] */
bearing_od      = 22;     // 608 bearing
bearing_w       = 7;
shaft_d         = 8;
hub_od          = 32;
hub_len         = 22;     // along Y (pitch axis)

/* [Counterweight arm] */
arm_len         = 130;    // from pivot centre to arm tip
arm_w           = 20;     // along Y
arm_t           = 8;      // along Z
slot_w          = 5.4;    // clears M5
slot_start      = 30;     // slot begins this far from pivot
slot_end_margin = 10;     // solid material left at tip
tick_pitch      = 5;      // graduation spacing
tick_depth      = 0.6;

/* [Cradle mount tab] */
tab_len         = 28;
tab_hole_d      = 3.4;    // M3
tab_hole_pitch  = 14;

/* [Sliding carriage] */
clr             = 0.3;    // sliding clearance
car_len         = 24;     // along X
car_wall        = 4;
car_top         = 6;
stud_d          = 6;      // M6 stud for weights
stud_len        = 40;
lock_screw_d    = 4.2;    // M4 thumbscrew bore (tap or heat insert)

/* [Slotted lead weights] */
wt_d            = 36;
wt_t            = 6;
wt_slot_w       = stud_d + 0.6;
lead_density    = 11.34e-3;   // g/mm^3

/* [Assembly] */
weight_pos      = 85;     // carriage centre distance from pivot (mm)
n_weights       = 4;
explode         = 0;      // 0..1 exploded view

// ---------------- Derived / checks ----------------
slot_x0 = slot_start;
slot_x1 = arm_len - slot_end_margin;
assert(slot_x1 > slot_x0 + car_len, "Arm too short for slot + carriage");
assert(n_weights * wt_t + 8 <= stud_len, "Stud too short for weight stack");

function wt_mass() =
    lead_density * wt_t *
    (PI*pow(wt_d/2,2) - wt_slot_w*(wt_d/2) - PI*pow(wt_slot_w/2,2)/2);

function clamp_pos(p) = max(slot_x0 + car_len/2, min(slot_x1 - car_len/2, p));

echo(str("Cycle 7 | single plate mass = ", round(wt_mass()*10)/10, " g"));
echo(str("Stack mass = ", round(wt_mass()*n_weights), " g, moment ≈ ",
         round(wt_mass()*n_weights*clamp_pos(weight_pos)/1000*100)/100,
         " g·m"));

// =====================================================================
//  Parts
// =====================================================================

// Pitch hub with bearing seats on both faces
module pitch_hub() {
    difference() {
        rotate([90,0,0]) cylinder(d=hub_od, h=hub_len, center=true);
        // shaft clearance
        rotate([90,0,0]) cylinder(d=shaft_d+1, h=hub_len+2, center=true);
        // bearing pockets
        for (s=[-1,1])
            translate([0, s*(hub_len/2 - bearing_w/2 + 0.01), 0])
                rotate([90,0,0])
                    cylinder(d=bearing_od+0.1, h=bearing_w, center=true);
    }
}

// Counterweight arm with adjustment slot, graduations, cradle tab
module counterweight_arm() {
    difference() {
        union() {
            // main beam (-X)
            translate([-arm_len, -arm_w/2, -arm_t/2])
                cube([arm_len - hub_od/2 + 2, arm_w, arm_t]);
            // cradle mount tab (+X)
            translate([hub_od/2 - 2, -arm_w/2, -arm_t/2])
                cube([tab_len, arm_w, arm_t]);
            // fillet blocks into hub
            for (s=[-1,1]) translate([s*(hub_od/2-4) - 3, -arm_w/2, -arm_t/2])
                cube([6, arm_w, arm_t]);
        }
        // hub bore cut-through
        rotate([90,0,0]) cylinder(d=hub_od-0.01, h=arm_w+2, center=true);
        // adjustment slot
        hull() for (x=[slot_x0, slot_x1])
            translate([-x, 0, 0]) cylinder(d=slot_w, h=arm_t+2, center=true);
        // graduation ticks on top face (long tick every 25 mm)
        for (x=[slot_x0 : tick_pitch : slot_x1])
            translate([-x - 0.3, arm_w/2 - (x%25==0 ? 6 : 3),
                       arm_t/2 - tick_depth])
                cube([0.6, 10, tick_depth+1]);
        // cradle tab holes
        for (dx=[-tab_hole_pitch/2, tab_hole_pitch/2])
            translate([hub_od/2 + tab_len/2 + dx, 0, 0])
                cylinder(d=tab_hole_d, h=arm_t+2, center=true);
    }
    pitch_hub();
    // tip end-stop lip (stops carriage sliding off)
    translate([-arm_len, -arm_w/2, arm_t/2]) cube([3, arm_w, 2]);
}

// Saddle carriage: wraps arm, thumbscrew lock, downward weight stud
module slider_carriage() {
    in_w = arm_w + 2*clr;
    in_t = arm_t + 2*clr;
    out_w = in_w + 2*car_wall;
    difference() {
        translate([-car_len/2, -out_w/2, -in_t/2 - car_wall])
            cube([car_len, out_w, in_t + car_wall + car_top]);
        // arm channel
        translate([-car_len/2 - 1, -in_w/2, -in_t/2])
            cube([car_len + 2, in_w, in_t]);
        // lock thumbscrew bore (bears on arm top face)
        translate([0, arm_w/4, in_t/2 - 1])
            cylinder(d=lock_screw_d, h=car_top + 2);
        // index window for reading graduations
        translate([-0.5, arm_w/2 - 5, in_t/2 - 1]) cube([1, 6, car_top + 2]);
        // stud bore through b