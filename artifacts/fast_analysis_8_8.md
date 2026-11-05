# Pitch Axis Counterweight Balancing Mechanism (Cycle 8)

This is a parametric OpenSCAD design for a gimbal pitch-axis counterweight. It has three main parts:

- **Hub and rail.** A slotted rail extends behind the pitch axis and carries engraved graduation marks.
- **Sliding carrier.** A carrier straddles the rail and is locked in place by a T-bolt and a thumb-knob.
- **Slotted lead plates.** The plates stack on a vertical stud. Each plate has a U-slot, so you can add or remove plates without taking off the clamp nut.

The script also echoes the estimated mass and the balancing moment so you can tune the setup numerically.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle - Counterweight Balancing Mechanism
//  Revision: Cycle 8
//  Slotted sliding lead weights on a graduated rail
// =====================================================================
//  Coordinate convention:
//    Pitch axis  = Y axis (through origin)
//    Rail        = extends along -X (behind the camera)
//    Stack dir.  = +Z
//  All units in mm.
// =====================================================================

$fn = 64;

/* ---------------- Global parameters ---------------- */
// Pitch hub
hub_od            = 36;
hub_len           = 22;
bearing_od        = 22.05;   // 608 bearing + clearance
bearing_w         = 7;
shaft_d           = 8.2;

// Rail
rail_len          = 140;     // from hub centre to rail end
rail_w            = 24;      // Y
rail_t            = 8;       // Z
slot_w            = 6.4;     // M6 bolt
slot_start        = 30;      // distance from axis where slot starts
slot_end_margin   = 10;
tick_pitch        = 5;       // graduation spacing
tick_depth        = 0.6;

// Carrier
car_len           = 30;      // X
car_wall          = 3;
car_top_t         = 6;
car_clear         = 0.3;     // slide clearance
stud_d            = 6;       // M6 stud
stud_len          = 40;

// Lead plates
plate_od          = 44;
plate_t           = 5;
plate_slot_w      = 6.6;     // U-slot for M6 stud
lead_density      = 11.34;   // g/cm^3
plate_chamfer     = 0.8;

// Clamp knob
knob_d            = 22;
knob_h            = 10;
knob_lobes        = 6;

/* ---------------- Assembly control ---------------- */
cw_position       = 95;      // distance of carrier centre from pitch axis
n_plates          = 4;       // number of stacked lead plates
show_exploded     = false;

/* =====================================================================
   PITCH HUB
   ===================================================================== */
module pitch_hub() {
    difference() {
        union() {
            rotate([90,0,0]) cylinder(d=hub_od, h=hub_len, center=true);
            // web blending into rail
            translate([-hub_od/2 - 4, -rail_w/2, -rail_t/2])
                cube([hub_od/2 + 4, rail_w, rail_t]);
        }
        // shaft bore
        rotate([90,0,0]) cylinder(d=shaft_d, h=hub_len+2, center=true);
        // bearing seats both sides
        for (s=[-1,1])
            translate([0, s*(hub_len/2 - bearing_w/2 + 0.01), 0])
                rotate([90,0,0]) cylinder(d=bearing_od, h=bearing_w, center=true);
        // clamp split + clamp screw (M3)
        translate([hub_od/4, -hub_len/2-1, -0.75]) cube([hub_od, hub_len+2, 1.5]);
        translate([hub_od/2 - 5, 0, 0]) cylinder(d=3.2, h=hub_od, center=true);
    }
}

/* =====================================================================
   SLOTTED, GRADUATED RAIL
   ===================================================================== */
module counterweight_rail() {
    slot_len = rail_len - slot_start - slot_end_margin;
    difference() {
        // main bar
        translate([-rail_len, -rail_w/2, -rail_t/2])
            cube([rail_len - hub_od/2 + 2, rail_w, rail_t]);

        // through slot (stadium)
        hull() for (x=[slot_start, slot_start + slot_len])
            translate([-x, 0, 0]) cylinder(d=slot_w, h=rail_t+2, center=true);

        // T-bolt head recess on underside
        hull() for (x=[slot_start, slot_start + slot_len])
            translate([-x, 0, -rail_t/2 - 0.01]) cylinder(d=11, h=3.5);

        // graduation ticks on top surface, both sides of slot
        for (x=[slot_start : tick_pitch : slot_start + slot_len]) {
            major = (x % (tick_pitch*2) == 0);
            for (s=[-1,1])
                translate([-x - 0.3, s>0 ? slot_w/2+1.5 : -rail_w/2 + 1,
                           rail_t/2 - tick_depth])
                    cube([0.6, major ? 6 : 3.5, tick_depth + 0.1]);
        }
    }
    // end stop lip so the carrier can't slide off
    translate([-rail_len, -rail_w/2, rail_t/2]) cube([3, rail_w, 2]);
}

/* =====================================================================
   SLIDING CARRIER (straddles rail, holds stud)
   ===================================================================== */
module weight_carrier() {
    in_w = rail_w + 2*car_clear;
    out_w = in_w + 2*car_wall;
    skirt = rail_t * 0.6;
    difference() {
        union() {
            // top saddle
            translate([-car_len/2, -out_w/2, rail_t/2 + car_clear])
                cube([car_len, out_w, car_top_t]);
            // side skirts to guide on rail
            for (s=[-1,1])
                translate([-car_len/2, s>0 ? in_w/2 : -out_w/2,
                           rail_t/2 + car_clear - skirt])
                    cube([car_len, car_wall, skirt]);
            // plate seat boss
            translate([0,0, rail_t/2 + car_clear + car_top_t])
                cylinder(d=plate_od*0.55, h=2);
        }
        // clamp bolt through-hole
        cylinder(d=stud_d + 0.4, h=100, center=true);
        // index notch aligning with rail ticks
        translate([-0.4, -out_w/2 - 0.1, rail_t/2 + car_clear])
            cube([0.8, 1.2, car_top_t]);
    }
}

/* =====================================================================
   STUD / T-BOLT
   ===================================================================== */
module clamp_stud() {
    // head sits in rail underside recess
    translate([0,0,-rail_t/2]) cylinder(d=10, h=3, $fn=6);
    translate([0,0,-rail_t/2 + 3]) cylinder(d=stud_d, h=stud_len);
}

/* =====================================================================
   SLOTTED LEAD PLATE
   U-slot opens toward +Y so plates slide on/off the stud sideways
   ===================================================================== */
module slotted_weight() {
    difference() {
        // chamfered disc
        hull() {
            translate([0,0,plate_chamfer])
                cylinder(d=plate_od, h=plate_t - 2*plate_chamfer);
            cylinder(d=plate_od - 2*plate_chamfer, h=plate_t);
        }
        // U-slot
        hull() {
            translate([0,0,-1]) cylinder(d=plate_slot_w, h=plate_t+2);
            translate([0, plate_od, -1]) cylinder(d=plate_slot_w, h=plate_t+2);
        }
        // locating dimple (keys plates so they don't rotate out)
        translate([0, -plate_od/2 + 6, plate_t - 1]) sphere(d=3);
    }
    // mating bump on underside
    translate([0, -plate_od/2 + 6, 0]) sphere(d=2.6);
}

/* =====================================================================
   THUMB KNOB (captures M6 nut)
   ===================================================================== */
module thumb_knob() {
    difference() {
        union() {
            cylinder(d=knob_d*0.7, h=knob_h);
            for (i=[0:knob_lobes-1]) rotate(i*360/knob_lobes)
                translate([knob_d/2 - 3, 0, 0]) cylinder(d=6, h=knob_h);
        }
        cylinder(d=stud_d + 0.4, h=knob_h*3, center=true);
        translate([0,0,knob_h - 5]) cylinder(d=11.4, h=6, $fn=6); // M6 nut
    }
}

/* =====================================================================
   MASS / BALANCE CALCULATION
   ===================================================================== */
function plate_volume_mm3() =
    PI*pow(plate_od/2,2)*plate_t
    - (plate_slot_w * (plate_od/2) + PI*pow(plate_slot_w/2,2)/2) * plate_t;

function plate_mass_g() = plate_volume_mm3()/1000 * lead_density;

