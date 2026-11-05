# OpenSCAD Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 18)

This module models a pitch-axis counterweight arm for a camera cradle. It has these parts:

- A **pitch hub** with a 608 bearing pocket and clamp split.
- A **slotted balance arm** that extends opposite the camera. It has an engraved graduated scale.
- A **sliding carriage** that runs in the arm slot and is locked by a thumbscrew.
- **Slotted lead weight plates** with open-ended U-slots. Each plate slides onto the carriage post, so you can add or remove one without disassembly.
- A **knurled retaining nut**.

```openscad
// ============================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted Sliding Lead Weights  (Cycle 18)
//  Units: mm
// ============================================================

$fn = 48;

/* ---------------- Parameters ---------------- */
// Pitch hub
hub_od          = 34;
hub_len         = 18;
bearing_od      = 22.15;   // 608 bearing + clearance
bearing_w       = 7.2;
shaft_d         = 8.3;
clamp_gap       = 1.5;
clamp_screw_d   = 3.4;     // M3

// Balance arm
arm_len         = 140;     // from hub centre
arm_w           = 22;
arm_t           = 8;
slot_w          = 5.4;     // carriage key / M5 thumbscrew
slot_start      = 30;      // from hub centre
slot_end        = 130;
scale_pitch     = 5;       // graduation spacing

// Carriage
car_len         = 30;
car_w           = arm_w + 8;
car_t           = 5;
post_d          = 6;       // weight stud (M6)
post_h          = 30;

// Lead weight plates
wt_len          = 34;
wt_w            = 40;
wt_t            = 6;       // ≈ 90 g each in lead (11.34 g/cc)
wt_slot_w       = post_d + 0.6;
wt_count        = 3;
wt_chamfer      = 1;

// Thumbscrew / nut
knob_d          = 16;
knob_h          = 8;

// Assembly state
weight_pos      = 40 + 70 * (0.5 + 0.5 * sin($t * 360)); // animated slide
show_exploded   = false;

/* ---------------- Utility ---------------- */
module rounded_box(size, r=2) {
    hull()
        for (x=[r, size[0]-r], y=[r, size[1]-r])
            translate([x, y, 0]) cylinder(r=r, h=size[2]);
}

module knurled_knob(d, h, n=18) {
    difference() {
        cylinder(d=d, h=h);
        for (i=[0:n-1])
            rotate([0, 0, i*360/n])
                translate([d/2, 0, -0.1]) cylinder(d=1.6, h=h+0.2, $fn=8);
    }
}

/* ---------------- Pitch hub ---------------- */
module pitch_hub() {
    difference() {
        union() {
            rotate([90, 0, 0]) cylinder(d=hub_od, h=hub_len, center=true);
            // clamp ears
            translate([0, -hub_len/2, hub_od/2 - 4])
                cube([12, hub_len, 10]);
        }
        // shaft bore
        rotate([90, 0, 0]) cylinder(d=shaft_d, h=hub_len+2, center=true);
        // bearing pockets, both faces
        for (s=[-1, 1])
            translate([0, s*(hub_len/2 - bearing_w/2 + 0.01), 0])
                rotate([90, 0, 0]) cylinder(d=bearing_od, h=bearing_w, center=true);
        // clamp split
        translate([-clamp_gap/2, -hub_len, 0]) cube([clamp_gap, hub_len*2, hub_od]);
        // clamp screw
        translate([-10, 0, hub_od/2 + 1])
            rotate([0, 90, 0]) cylinder(d=clamp_screw_d, h=30);
    }
}

/* ---------------- Balance arm ---------------- */
module balance_arm() {
    difference() {
        hull() {
            rotate([90, 0, 0]) cylinder(d=hub_od, h=arm_w, center=true);
            translate([-arm_len, -arm_w/2, -arm_t/2])
                rounded_box([10, arm_w, arm_t], 3);
        }
        // trim the hull to a flat bar outside the hub
        translate([-arm_len-1, -arm_w, arm_t/2]) cube([arm_len - hub_od/2 + 1, arm_w*2, hub_od]);
        translate([-arm_len-1, -arm_w, -arm_t/2 - hub_od]) cube([arm_len - hub_od/2 + 1, arm_w*2, hub_od]);

        // hub clearance (hub is a separate part pressed/bolted in)
        rotate([90, 0, 0]) cylinder(d=hub_od - 0.01, h=arm_w+2, center=true);

        // sliding slot
        hull() {
            translate([-slot_start, 0, -arm_t]) cylinder(d=slot_w, h=arm_t*2);
            translate([-slot_end,   0, -arm_t]) cylinder(d=slot_w, h=arm_t*2);
        }

        // engraved graduation scale (top face, one side of slot)
        for (x=[slot_start:scale_pitch:slot_end]) {
            major = ((x - slot_start) % (scale_pitch*4) == 0);
            translate([-x - 0.4, slot_w/2 + 1.5, arm_t/2 - 0.6])
                cube([0.8, major ? 6 : 3.5, 1]);
        }
    }
    // hub mounting collar
    difference() {
        rotate([90, 0, 0]) cylinder(d=hub_od + 8, h=arm_w, center=true);
        rotate([90, 0, 0]) cylinder(d=hub_od + 0.2, h=arm_w+2, center=true);
    }
}

/* ---------------- Sliding carriage ---------------- */
module carriage() {
    // saddle straddles the arm: top plate plus side skirts
    difference() {
        translate([-car_len/2, -car_w/2, -arm_t/2 - 3])
            rounded_box([car_len, car_w, arm_t + 3 + car_t], 2);
        // channel for arm (0.3 mm sliding clearance)
        translate([-car_len, -(arm_w+0.6)/2, -arm_t/2 - 0.3])
            cube([car_len*2, arm_w + 0.6, arm_t + 0.6]);
        // thumbscrew hole
        translate([0, 0, -arm_t]) cylinder(d=slot_w - 0.2, h=arm_t*3);
        // index pointer notch
        translate([-0.4, slot_w/2, arm_t/2 + car_t - 0.8]) cube([0.8, car_w, 1]);
    }
    // key that rides in the slot (keeps carriage square)
    translate([-6, -(slot_w-0.4)/2, -arm_t/2]) difference() {
        cube([12, slot_w - 0.4, arm_t]);
        translate([6, (slot_w-0.4)/2, -1]) cylinder(d=3, h=arm_t+2);
    }
    // weight stud
    translate([0, 0, arm_t/2 + car_t]) cylinder(d=post_d, h=post_h);
}

/* ---------------- Slotted lead weight plate ---------------- */
module lead_weight() {
    difference() {
        hull() {
            translate([-wt_len/2 + wt_chamfer, -wt_w/2 + wt_chamfer, 0])
                rounded_box([wt_len - 2*wt_chamfer, wt_w - 2*wt_chamfer, wt_t], 3);
            translate([-wt_len/2, -wt_w/2, wt_chamfer])
                rounded_box([wt_len, wt_w, wt_t - 2*wt_chamfer], 3);
        }
        // open-ended U-slot so plates can be swapped without removing the nut
        hull() {
            translate([0, 0, -1]) cylinder(d=wt_slot_w, h=wt_t+2);
            translate([wt_len, 0, -1]) cylinder(d=wt_slot_w, h=wt_t+2);
        }
        // anti-rotation lip groove
        translate([-wt_len/2 - 1, -wt_w/2 - 1, wt_t - 1]) cube([3, wt_w + 2, 2]);
    }
    // lip that keys into the plate above
    translate([-wt_len/2 + 0.2, -wt_w/2 + 2, -0.8]) cube([2.6, wt_w - 4, 0.8]);
}

/* ---------------- Fasteners ---------------- */
module thumbscrew() {
    color("silver") {
        translate([0, 0, -arm_t/2 - 3 - knob_h]) knurled_knob(knob_d, knob_h);
        translate([0, 0, -arm_t/2 - 3]) cylinder(d=5, h=arm_t + 3);
    }
}

module retaining_nut() {
    color("silver") difference() {
        knurled_knob(knob_d, knob_h);
        translate([0, 0, -1]) cylinder(d=post_d, h=knob_h + 2);
    }
}

/* ---------------- Top-level module ---------------- */
module pitch_counterweight_mechanism(pos = weight_pos, n = wt_count,