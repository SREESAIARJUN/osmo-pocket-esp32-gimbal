# Pitch-Axis Counterweight Balancer, Cycle 5

This is a parametric OpenSCAD module for a pitch-axis counterweight. A slotted arm bolts to the camera cradle. A clamp carriage slides along the slot. Open-ended U-slotted lead plates slip sideways onto the carriage stud, so you can add or remove weight without taking off the thumb nut.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights  (Cycle 5)
//  Units: mm.  Pitch axis = X axis through origin.
//  Arm extends toward -Y (behind the camera).
// =====================================================================

/* ---------------- [ Global parameters ] ---------------- */
$fn = 48;

// Arm
arm_len        = 120;   // length from pitch axis to arm tip
arm_w          = 20;    // arm width (X)
arm_t          = 6;     // arm thickness (Z)
arm_drop       = 12;    // arm centreline below pitch axis (lowers CG)
slot_w         = 4.4;   // clamp slot width (M4 clearance)
slot_start     = 30;    // slot begins this far from the axis
slot_end_margin= 10;    // solid material left at tip

// Mounting flange (bolts to cradle side plate)
flange_l       = 26;
flange_h       = 30;
flange_t       = 6;
flange_bolt_d  = 3.4;   // M3
flange_bolt_sp = 18;    // vertical bolt spacing
axis_bore_d    = 8.2;   // clearance for the pitch shaft / bearing boss

// Carriage
car_len        = 28;
car_wall       = 3;
car_clear      = 0.3;   // sliding clearance around the arm
stud_d         = 4;     // M4 stud
stud_len       = 40;
knob_d         = 18;
knob_h         = 8;

// Lead plates
plate_w        = 34;
plate_l        = 28;
plate_t        = 5;
plate_slot_w   = 4.6;   // U-slot width (stud + clearance)
plate_chamfer  = 0.8;
lead_density   = 11.34; // g/cm^3

// Assembly state
weight_pos     = 85;    // carriage centre distance from pitch axis (mm)
n_plates       = 3;     // number of stacked lead plates
explode        = 0;     // exploded-view spacing (0 = assembled)

/* ---------------- [ Derived values ] ---------------- */
slot_len   = arm_len - slot_start - slot_end_margin;
car_in_w   = arm_w + 2*car_clear;
car_in_t   = arm_t + 2*car_clear;
car_out_w  = car_in_w + 2*car_wall;
car_out_t  = car_in_t + 2*car_wall;

pos_min    = slot_start + car_len/2;
pos_max    = arm_len - slot_end_margin - car_len/2;
wpos       = min(max(weight_pos, pos_min), pos_max);

/* ---------------- [ Balance report ] ---------------- */
function plate_volume_cm3() =
    (plate_w*plate_l - plate_slot_w*(plate_w/2) - PI*pow(plate_slot_w/2,2)/2)
    * plate_t / 1000;

plate_mass = plate_volume_cm3() * lead_density;
total_mass = plate_mass * n_plates;

echo(str("[CW Cycle5] Plate mass  : ", round(plate_mass*10)/10, " g"));
echo(str("[CW Cycle5] Total lead  : ", round(total_mass*10)/10, " g"));
echo(str("[CW Cycle5] Position    : ", wpos, " mm (range ",
         pos_min, "–", pos_max, ")"));
echo(str("[CW Cycle5] Moment      : ",
         round(total_mass*wpos/10)/100, " kg·mm about pitch axis"));
if (weight_pos != wpos)
    echo("[CW Cycle5] WARNING: weight_pos clamped to slot travel");

/* =====================================================================
   Modules
   ===================================================================== */

// Slotted counterweight arm with integral mounting flange
module cw_arm() {
    difference() {
        union() {
            // Arm bar
            translate([-arm_w/2, -arm_len, -arm_drop - arm_t/2])
                cube([arm_w, arm_len, arm_t]);
            // Flange at the axis end
            translate([-arm_w/2, -flange_l, -flange_h/2 - arm_drop/2])
                cube([flange_t, flange_l, flange_h + arm_drop]);
            // Gusset
            hull() {
                translate([-arm_w/2, -flange_l, -arm_drop - arm_t/2])
                    cube([flange_t, flange_l, 0.01]);
                translate([-arm_w/2, -flange_l - 15, -arm_drop - arm_t/2])
                    cube([0.01 + flange_t*0.5, 0.01, arm_t]);
            }
        }
        // Clamp slot (rounded ends)
        hull() for (y = [slot_start, slot_start + slot_len])
            translate([0, -y, -arm_drop - arm_t])
                cylinder(d = slot_w, h = arm_t*2);

        // Pitch-axis bore through flange
        translate([-arm_w/2 - 1, -flange_l/2, 0])
            rotate([0, 90, 0]) cylinder(d = axis_bore_d, h = flange_t + 2);

        // Flange bolts
        for (z = [-flange_bolt_sp/2, flange_bolt_sp/2])
            translate([-arm_w/2 - 1, -flange_l/2, z - arm_drop/2])
                rotate([0, 90, 0]) cylinder(d = flange_bolt_d, h = flange_t + 2);

        // Graduation notches every 10 mm along the slot edge
        for (y = [slot_start : 10 : slot_start + slot_len])
            translate([arm_w/2 - 1.5, -y - 0.4, -arm_drop + arm_t/2 - 0.6])
                cube([2, 0.8, 1]);
    }
}

// Sliding clamp carriage (wraps the arm, stud passes through slot)
module cw_carriage() {
    difference() {
        translate([-car_out_w/2, -car_len/2, -car_out_t/2])
            cube([car_out_w, car_len, car_out_t]);
        // Channel for the arm
        translate([-car_in_w/2, -car_len/2 - 1, -car_in_t/2])
            cube([car_in_w, car_len + 2, car_in_t]);
        // Stud hole
        cylinder(d = stud_d + 0.3, h = car_out_t + 2, center = true);
        // Captive hex nut pocket underneath (M4)
        translate([0, 0, -car_out_t/2 - 0.01])
            cylinder(d = 8.1, h = 3.4, $fn = 6);
        // Index window to read the arm graduations
        translate([car_out_w/2 - car_wall - 0.5, -1, -1])
            cube([car_wall + 1, 2, car_out_t]);
    }
}

// Single U-slotted lead plate (slides on sideways from +X)
module lead_plate() {
    difference() {
        minkowski() {
            translate([-plate_w/2 + plate_chamfer, -plate_l/2 + plate_chamfer, 0])
                cube([plate_w - 2*plate_chamfer, plate_l - 2*plate_chamfer,
                      plate_t - 0.01]);
            cylinder(r = plate_chamfer, h = 0.01, $fn = 16);
        }
        // U-slot from centre to +X edge
        translate([0, 0, -1]) {
            cylinder(d = plate_slot_w, h = plate_t + 2);
            translate([0, -plate_slot_w/2, 0])
                cube([plate_w, plate_slot_w, plate_t + 2]);
        }
        // Lead-in flare at slot mouth
        translate([plate_w/2, 0, -1])
            cylinder(d = plate_slot_w + 3, h = plate_t + 2, $fn = 4);
        // Identification dimple (one per plate face)
        translate([-plate_w/2 + 5, plate_l/2 - 5, plate_t - 0.5])
            cylinder(d = 2.5, h = 1);
    }
}

// Thumb knob with embedded M4 nut
module thumb_knob() {
    difference() {
        union() {
            cylinder(d = knob_d, h = knob_h);
            for (a = [0 : 45 : 359])
                rotate(a) translate([knob_d/2, 0, 0])
                    cylinder(d = 3, h = knob_h);
        }
        translate([0, 0, -1]) cylinder(d = stud_d + 0.3, h = knob_h + 2);
        translate([0, 0, knob_h - 3.3]) cylinder(d = 8.1,