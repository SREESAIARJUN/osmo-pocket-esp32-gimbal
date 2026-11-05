# OpenSCAD: Pitch-Axis Counterweight Balancing Mechanism (Cycle 21)

This is a parametric counterweight assembly for a gimbal or camera cradle pitch axis. A clamping hub mounts on the pitch shaft. A slotted rail extends behind the axis, and slotted lead weights slide along that rail. Each weight locks in place with a bolt through the rail slot and a T-nut clamp plate underneath.

When you render it, the console reports each weight's mass, its arm length and the total balancing moment.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancing Mechanism
//  Revision: Cycle 21
//  - Clamping hub on pitch shaft
//  - Slotted counterweight rail with engraved graduation scale
//  - Slotted sliding lead weights (cast or pocket-filled) with
//    bolt + T-plate clamp for infinite positional adjustment
//  Units: mm
// =====================================================================

/* [Render] */
render_mode     = "assembly";   // ["assembly","rail","weight","clamp_plate","print_layout","weight_mold"]
show_hardware   = true;
$fn             = 64;

/* [Pitch Axis Hub] */
shaft_d         = 8.0;          // pitch shaft diameter
shaft_clear     = 0.15;
hub_d           = 26;
hub_len         = 14;
hub_split_gap   = 1.2;          // clamp slit width
clamp_screw_d   = 3.3;          // M3 clearance
clamp_nut_af    = 5.6;          // M3 nut across flats
key_flat        = true;         // D-flat engagement on shaft
key_flat_depth  = 0.6;

/* [Counterweight Rail] */
rail_len        = 130;          // from axis centre to rail tip
rail_w          = 18;
rail_t          = 6;
slot_w          = 5.3;          // M5 clearance slot
slot_start      = 28;           // slot begins this far from axis
slot_end_margin = 8;
tick_pitch      = 5;            // graduation spacing
tick_depth      = 0.6;
end_stop_h      = 4;            // lip at rail tip so weights can't slip off

/* [Lead Weights] */
weight_len      = 18;           // along rail
weight_w        = 34;
weight_h        = 22;           // total height above rail underside
channel_clear   = 0.4;          // sliding fit clearance on rail
bolt_d          = 5.3;          // M5 clearance through weight
bolt_head_d     = 9.5;
bolt_head_h     = 5.2;
lead_density    = 11.34;        // g/cm^3
// positions (mm from pitch axis to weight centre)
weight_positions = [55, 75, 98];

/* [Clamp Plate (T-nut)] */
plate_len       = 16;
plate_w         = 26;
plate_t         = 4;
plate_thread_d  = 4.2;          // M5 tap drill (or use embedded nut)

/* [Mold] */
mold_wall       = 6;

eps = 0.01;

// ---------------------------------------------------------------------
// Derived values
// ---------------------------------------------------------------------
slot_len   = rail_len - slot_start - slot_end_margin;
chan_w     = rail_w + 2*channel_clear;
chan_h     = rail_t + channel_clear;

function weight_volume_mm3() =
      weight_len * weight_w * weight_h
    - weight_len * chan_w   * chan_h
    - PI * pow(bolt_d/2,2) * (weight_h - chan_h)
    - PI * pow(bolt_head_d/2,2) * bolt_head_h;

function weight_mass_g() = weight_volume_mm3() / 1000 * lead_density;

function sum(v, i=0) = i >= len(v) ? 0 : v[i] + sum(v, i+1);

// =====================================================================
//  HUB
// =====================================================================
module pitch_hub() {
    difference() {
        union() {
            rotate([0,90,0]) cylinder(d=hub_d, h=hub_len, center=true);
            // clamp ears
            translate([0, 0, -hub_d/2 - 4])
                cube([hub_len, 12, 10], center=true);
        }
        // shaft bore with optional D-flat
        rotate([0,90,0])
        difference() {
            cylinder(d=shaft_d + 2*shaft_clear, h=hub_len+2, center=true);
            if (key_flat)
                translate([shaft_d/2 - key_flat_depth + shaft_clear, -shaft_d, -hub_len])
                    cube([shaft_d, 2*shaft_d, 2*hub_len]);
        }
        // split slit
        translate([0, 0, -hub_d/2 - 5])
            cube([hub_len+2, hub_split_gap, hub_d], center=true);
        // clamp screw
        translate([0, 0, -hub_d/2 - 4])
            rotate([90,0,0]) cylinder(d=clamp_screw_d, h=30, center=true);
        // nut trap
        translate([0, -6 - eps, -hub_d/2 - 4])
            rotate([90,0,0]) cylinder(d=clamp_nut_af/cos(30), h=3, $fn=6);
        // head counterbore
        translate([0, 6 + eps, -hub_d/2 - 4])
            rotate([-90,0,0]) cylinder(d=6, h=3);
    }
}

// =====================================================================
//  RAIL
// =====================================================================
module counterweight_rail() {
    difference() {
        union() {
            // main bar (extends along +Y, opposite the camera)
            translate([-rail_w/2, 0, -rail_t/2])
                cube([rail_w, rail_len, rail_t]);
            // root gusset blending into hub
            hull() {
                rotate([0,90,0]) cylinder(d=hub_d, h=rail_w, center=true);
                translate([-rail_w/2, slot_start - 6, -rail_t/2])
                    cube([rail_w, 1, rail_t]);
            }
            // tip end stop
            translate([-rail_w/2, rail_len - 3, -rail_t/2])
                cube([rail_w, 3, rail_t + end_stop_h]);
        }
        // shaft bore through gusset
        rotate([0,90,0]) cylinder(d=hub_d - 2, h=rail_w + 2, center=true);
        // adjustment slot
        hull() {
            translate([0, slot_start, 0]) cylinder(d=slot_w, h=rail_t+2, center=true);
            translate([0, slot_start + slot_len, 0]) cylinder(d=slot_w, h=rail_t+2, center=true);
        }
        // graduations on top face (every tick_pitch mm, long tick every 10)
        for (y = [slot_start : tick_pitch : slot_start + slot_len]) {
            L = (round(y) % 10 == 0) ? 4 : 2.2;
            for (s = [-1, 1])
                translate([s*(rail_w/2 - L/2), y, rail_t/2 - tick_depth/2 + eps])
                    cube([L, 0.6, tick_depth], center=true);
        }
        // lightening pocket on underside near root
        translate([0, (slot_start)/2 + 6, -rail_t/2 - eps])
            linear_extrude(rail_t/2)
                offset(r=2) square([rail_w - 10, 8], center=true);
    }
    pitch_hub();
}

// =====================================================================
//  SLIDING LEAD WEIGHT
//  Origin: centre of rail-contact channel, channel along Y
// =====================================================================
module lead_weight() {
    difference() {
        // body with chamfered top edges
        translate([0, 0, weight_h/2 - chan_h/2 - eps])
        hull() {
            translate([0,0,-weight_h/2 + 1])
                cube([weight_w, weight_len, 2], center=true);
            translate([0,0, weight_h/2 - 1])
                cube([weight_w - 4, weight_len - 4, 2], center=true);
        }
        // sliding channel (open on underside)
        translate([0, 0, -chan_h/2 - 1 + eps])
            cube([chan_w, weight_len + 2, chan_h + 2], center=true);
        // bolt hole
        cylinder(d=bolt_d, h=weight_h*2, center=true);
        // counterbore for socket head
        translate([0, 0, weight_h - chan_h/2 - bolt_head_h + eps])
            cylinder(d=bolt_head_d, h=bolt_head_h + 1);
        // index notch aligning with rail ticks
        for (s = [-1, 1])
            translate([s*(chan_w/2), 0, 0])
                rotate([0,0,45]) cube([1.5,1.5,30], center=true);
        // grip grooves on sides
        for (s = [-1,1], z = [4 : 4 : weight_h - chan_h - 2])
            translate([s*weight_w/2, 0, z])
                rotate([90,0,0]) cylinder(d=1.6, h=weight_len+2, center=true, $fn=12);
    }
}

// =====================================================================
//  CLAMP PLATE (bears against rail underside)
// =====================================================================
module clamp_plate() {
    difference() {
        translate([0,0,-plate_t/2])
            minkowski() {
                cube([plate_w - 4, plate_len - 4, plate_t/2], center=true);
                cylinder(r=2, h=plate_t/2, center=true, $fn=24);
            }
        cylinder(d=plate_thread_d, h=plate_t*3, center=true);
        // anti-rotation key rides in slot
    }
    translate([0,0,0])
        difference() {
            c