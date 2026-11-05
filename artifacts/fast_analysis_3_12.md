# Pitch-Axis Counterweight Balancer with Slotted Sliding Lead Weights (Cycle 3)

I don't have the earlier cycles, so this revision makes some assumptions about the design:

- A **T-slot rail** hangs off the pitch-axis hub.
- **Saddle carriers** slide along the rail and lock with a T-nut and clamp knob.
- **Open-slotted lead plates** slip sideways onto the carrier, so you can add or remove weight without taking the knob off.
- A **key on each carrier** fits the plate slot, which stops the plates from rotating.
- The rail has **engraved graduations** so you can repeat a balance setting.
- An **echo-based balance calculator** prints mass and moment.

Tell me what changed in Cycles 1–2 and I'll reconcile the details.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer — Cycle 3
//  Slotted sliding lead weights on a T-slot rail
//  Units: mm, g
// =====================================================================

/* ---------------- Global parameters ---------------- */
$fn = 48;
clr          = 0.25;    // general sliding clearance

// Pitch-axis hub
hub_od       = 34;
hub_len      = 22;
pitch_bore   = 12.0;    // pitch shaft diameter
clamp_screw  = 3.2;     // M3 clamp screw through split hub
hub_offset   = 0;       // rail X offset from pitch axis (hub centre at x=0)

// Rail (extends along +X from hub, below pitch axis by rail_drop)
rail_len     = 140;
rail_w       = 22;
rail_h       = 12;
rail_drop    = 0;       // Z offset of rail top relative to axis (negative = lower)
slot_w       = 5.5;     // M5 clearance through top
tchan_w      = 11;      // T-nut channel width (bottom, open)
tchan_h      = 5;       // T-nut channel height
slot_start   = 30;      // usable slot begins (from hub centre)
slot_end     = rail_len - 8;
grad_step    = 5;       // graduation pitch
grad_depth   = 0.6;

// Clamp hardware (M5)
bolt_d       = 5;
tnut_w       = tchan_w - 2*clr;
tnut_l       = 16;
tnut_t       = tchan_h - 0.5;

// Carrier (saddle)
car_l        = 28;
car_w        = rail_w + 2*3 + 2*clr;   // wraps rail sides
car_t        = 6;                       // plate above rail
car_lip      = 5;                       // side lips hanging down
key_w        = bolt_d + 2*clr + 3;      // anti-rotation key width (= plate slot)
key_h        = 2.5;

// Slotted lead plates
plate_l      = 40;      // along rail
plate_w      = 46;      // across rail
plate_t      = 6;
plate_r      = 4;
plate_slot_w = key_w + 2*clr;
lead_rho     = 11.34e-3; // g/mm^3

// Knob
knob_d       = 24;
knob_h       = 10;

/* ---------------- Utility ---------------- */
module rrect(l, w, h, r) {
    linear_extrude(h)
        offset(r) offset(-r) square([l, w], center = true);
}

function plate_mass() =
    let(v_box  = plate_l*plate_w*plate_t,
        v_slot = plate_slot_w*(plate_w/2)*plate_t)   // slot to centre
    (v_box - v_slot) * lead_rho;

/* ---------------- Components ---------------- */

// Split clamp hub on the pitch shaft
module pitch_hub() {
    difference() {
        union() {
            rotate([90,0,0]) cylinder(d = hub_od, h = hub_len, center = true);
            // web to rail
            translate([hub_od/4, 0, rail_drop - rail_h/2])
                cube([hub_od/2, hub_len, rail_h], center = true);
            // clamp ears
            translate([0, 0, hub_od/2])
                cube([10, hub_len, 10], center = true);
        }
        rotate([90,0,0]) cylinder(d = pitch_bore + clr, h = hub_len + 2, center = true);
        // split
        translate([0, 0, hub_od/2]) cube([1.5, hub_len + 2, hub_od], center = true);
        // clamp screw
        translate([0, 0, hub_od/2 + 1]) rotate([0,90,0])
            cylinder(d = clamp_screw, h = 20, center = true);
    }
}

// T-slot rail with graduations
module rail() {
    difference() {
        translate([0, -rail_w/2, rail_drop - rail_h]) cube([rail_len, rail_w, rail_h]);
        // top through-slot
        hull() for (x = [slot_start, slot_end])
            translate([x, 0, rail_drop - rail_h - 1]) cylinder(d = slot_w, h = rail_h + 2);
        // bottom T channel (open-ended at far end for nut insertion)
        translate([slot_start - tnut_l/2, -tchan_w/2, rail_drop - rail_h - 0.01])
            cube([rail_len, tchan_w, tchan_h]);
        // graduations on top face, one side
        for (x = [slot_start : grad_step : slot_end]) {
            major = ((x - slot_start) % (grad_step*2) == 0);
            translate([x - 0.4, rail_w/2 - (major ? 6 : 3.5), rail_drop - grad_depth])
                cube([0.8, 7, grad_depth + 0.1]);
        }
    }
    // end stop (prevents carrier sliding off)
    translate([rail_len - 4, -rail_w/2, rail_drop]) cube([4, rail_w, 4]);
}

module t_nut() {
    difference() {
        translate([-tnut_l/2, -tnut_w/2, 0]) cube([tnut_l, tnut_w, tnut_t]);
        translate([0,0,-1]) cylinder(d = bolt_d*0.85, h = tnut_t + 2); // tap drill
    }
}

module carrier() {
    difference() {
        union() {
            translate([0,0,0]) rrect(car_l, car_w, car_t, 2);
            // side lips
            for (s = [-1, 1])
                translate([-car_l/2, s > 0 ? rail_w/2 + clr : -car_w/2, -car_lip])
                    cube([car_l, (car_w - rail_w)/2 - clr, car_lip]);
            // anti-rotation key (plates slip over this)
            translate([-plate_l/2 + 1, -key_w/2, car_t])
                cube([plate_l - 2, key_w, key_h]);
        }
        translate([0,0,-car_lip-1]) cylinder(d = bolt_d + 2*clr, h = car_t + car_lip + key_h + 2);
        // index window for reading graduations
        translate([0, rail_w/2 - 3, -1]) cube([1, 6, car_t + 2], center = false);
    }
}

// Open-slotted lead plate: slot runs from -Y edge to centre
module lead_plate() {
    difference() {
        rrect(plate_l, plate_w, plate_t, plate_r);
        translate([-plate_slot_w/2, -plate_w/2 - 1, -1])
            cube([plate_slot_w, plate_w/2 + 1, plate_t + 2]);
        translate([0,0,-1]) cylinder(d = plate_slot_w, h = plate_t + 2);
        // lead-in chamfer at slot mouth
        for (s = [-1,1])
            translate([s*plate_slot_w/2, -plate_w/2, -1])
                rotate([0,0,45]) cube([2, 2, plate_t + 2], center = false);
    }
}

module clamp_knob() {
    difference() {
        union() {