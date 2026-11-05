# Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 27)

This OpenSCAD design is a counterweight arm for a camera cradle's pitch axis. It works as follows:

- **Arm:** clamps to the pitch shaft and has a through-slot with a captive T-nut channel underneath.
- **Carriage:** slides along the slot and locks in place with a thumb knob.
- **Weights:** U-slotted lead plates slip onto the carriage stud from the side, so you can add or remove them without unbolting anything.
- **Balance readout:** a scale on the arm plus an `echo()` balance calculator in the console.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer — Cycle 27
//  Slotted sliding lead weights on a T-slot arm with locking carriage
//  Units: mm, grams
// =====================================================================

/* [Render] */
part          = "assembly"; // [assembly, arm, carriage, weight, knob, weight_mold]
explode       = 0;          // [0:1:30]
$fn           = 48;

/* [Pitch Shaft / Hub] */
shaft_d       = 8.0;    // pitch shaft diameter
shaft_clear   = 0.25;
hub_d         = 26;
hub_len       = 18;
clamp_gap     = 1.6;    // split-clamp gap
clamp_screw_d = 3.4;    // M3 clearance
clamp_nut_af  = 5.7;    // M3 nut across flats

/* [Arm] */
arm_len       = 170;    // from pitch axis to arm tip
arm_w         = 26;
arm_t         = 10;
slot_start    = 40;     // slot begins this far from axis
slot_w        = 6.6;    // M6 clearance
tnut_w        = 11.5;   // M6 square nut / T-nut width + clearance
tnut_h        = 5.4;    // channel height (nut thickness + clearance)
end_stop_t    = 4;      // closes tip after nut is inserted (screwed cap)
scale_step    = 10;     // tick spacing in mm

/* [Carriage] */
car_len       = 34;
car_w         = arm_w + 8;  // wraps over arm edges
car_t         = 5;
car_lip       = 3;          // side lips that ride arm edges
stud_d        = 6;          // M6 stud
stud_len      = 45;

/* [Lead Weights] */
w_len         = 40;     // along arm
w_wid         = 32;     // across arm
w_t           = 6;      // plate thickness
w_count       = 4;      // plates stacked in assembly
w_slot_w      = 6.6;    // U-slot width (slips over M6)
w_key_w       = 10;     // anti-rotation key boss width
lead_density  = 11.34;  // g/cm^3

/* [Balance Calc] */
pos           = 0.55;   // carriage position along slot, 0..1
payload_g     = 650;    // camera + cradle mass
payload_cg_mm = -38;    // payload CG offset from pitch axis (negative = front)

// ---------------- derived ----------------
slot_len   = arm_len - slot_start - end_stop_t - 12;
car_x      = slot_start + car_len/2 + pos*(slot_len - car_len);
plate_vol  = (w_len*w_wid - w_slot_w*(w_len/2 + w_slot_w/2)) * w_t / 1000; // cm^3 (approx.)
plate_g    = plate_vol * lead_density;
stack_g    = plate_g * w_count;

echo(str("Plate mass ≈ ", round(plate_g), " g ; stack ≈ ", round(stack_g), " g"));
echo(str("Carriage CG at ", round(car_x), " mm from pitch axis"));
echo(str("Counter moment = ", round(stack_g*car_x), " g·mm  vs payload moment = ",
         round(abs(payload_g*payload_cg_mm)), " g·mm"));
echo(str("Ideal carriage position = ",
         round(abs(payload_g*payload_cg_mm)/max(stack_g,1)), " mm from axis"));

// =====================================================================
//  ARM
// =====================================================================
module counterweight_arm() {
    difference() {
        union() {
            // hub
            cylinder(d = hub_d, h = hub_len, center = true);
            // beam
            translate([0, -arm_w/2, -arm_t/2])
                cube([arm_len, arm_w, arm_t]);
            // clamp ears
            translate([-hub_d/2 - 8, -6, -hub_len/2])
                cube([10, 12, hub_len]);
        }
        // shaft bore
        cylinder(d = shaft_d + shaft_clear, h = hub_len + 2, center = true);
        // split clamp slot
        translate([-hub_d/2 - 10, -clamp_gap/2, -hub_len/2 - 1])
            cube([hub_d/2 + 10 - shaft_d/2 + 1, clamp_gap, hub_len + 2]);
        // clamp screw + captive nut
        translate([-hub_d/2 - 3, 0, 0]) rotate([90, 0, 0]) {
            cylinder(d = clamp_screw_d, h = 30, center = true);
            translate([0, 0, 4]) cylinder(d = clamp_nut_af/cos(30), h = 10, $fn = 6);
        }
        // through slot for stud
        hull() for (x = [slot_start, slot_start + slot_len])
            translate([x, 0, 0]) cylinder(d = slot_w, h = arm_t + 2, center = true);
        // T-nut channel underneath, open at tip for nut insertion
        translate([slot_start - tnut_w/2, -tnut_w/2, -arm_t/2 - 0.01])
            cube([arm_len - slot_start + tnut_w, tnut_w, tnut_h]);
        // end-cap screw holes (M3) at tip
        for (s = [-1, 1]) translate([arm_len - 3, s*(arm_w/2 - 4), 0])
            rotate([0, 90, 0]) cylinder(d = 2.6, h = 12, center = true);
        // position scale ticks on top face
        for (x = [slot_start : scale_step : slot_start + slot_len])
            translate([x - 0.4, arm_w/2 - (x % 50 == 0 ? 6 : 3.5), arm_t/2 - 0.6])
                cube([0.8, 8, 1]);
        // lightening pockets between hub and slot
        translate([hub_d/2 + 3, -arm_w/2 + 4, -arm_t/2 + 2.5])
            cube([slot_start - hub_d/2 - 10, arm_w - 8, arm_t]);
    }
}

// tip end cap: retains T-nut in channel
module end_cap() {
    difference() {
        translate([0, -arm_w/2, -arm_t/2]) cube([end_stop_t, arm_w, arm_t]);
        for (s = [-1, 1]) translate([0, s*(arm_w/2 - 4), 0])
            rotate([0, 90, 0]) cylinder(d = 3.4, h = 20, center = true);
    }
}

// =====================================================================
//  CARRIAGE (sliding shoe with M6 stud)
// =====================================================================
module carriage() {
    difference() {
        union() {
            // top plate
            translate([-car_len/2, -car_w/2, 0]) cube([car_len, car_w, car_t]);
            // side lips riding arm edges
            for (s = [-1, 1])
                translate([-car_len/2, s > 0 ? arm_w/2 + 0.3 : -car_w/2, -car_lip])
                    cube([car_len, (car_w - arm_w)/2 - 0.3, car_lip]);
            // tongue that tracks in slot
            translate([-car_len/2 + 4, -(slot_w - 0.6)/2, -2])
                cube([car_len - 8, slot_w - 0.6, 2]);
            // anti-rotation key for weights
            translate([-w_key_w/2 - 6, -w_key_w/2, car_t])
                cube([w_key_w/2, w_key_w, 2]);
        }
        cylinder(d = stud_d + 0.4, h = 50, center = true);
    }
    // stud (visual)
    color("silver") translate([0, 0, -arm_t - 2]) cylinder(d = stud_d, h = stud_len);
}

// =====================================================================
//  SLOTTED LEAD WEIGHT PLATE
// =====================================================================
module slotted_weight() {
    difference() {
        // rounded plate
        hull() for (x = [-1, 1], y = [-1, 1])
            translate([x*(w_len/2 - 3), y*(w_wid/2 - 3), 0])
                cylinder(r = 3, h = w_t);
        // U-slot from +X edge to centre: slides on sideways over stud
        hull() {
            cylinder(d = w_slot_w, h = 3*w_t, center = true);
            translate([w_len, 0, 0]) cylinder(d = w_slot_w, h = 3*w_t, center = true);
        }
        // key notch on -X side (indexes onto carriage key / plate below)
        translate([-w_key_w/2 - 6.2, -w_key_w/2 - 0.2, -1])
            cube([w_key_w/2 + 0.4, w_key_w + 0.4