# Pitch-Axis Counterweight Balancing Mechanism (Cycle 6)

The module below is a parametric OpenSCAD design. It builds a pitch-axis hub with a counterweight rail. Slotted lead saddle weights slide along the rail and lock in place with a thumbscrew-style bolt that runs through the rail's slot. Each weight has an open-ended slot, so it can be added or removed without fully unscrewing the bolt.

The file also prints a static balance estimate to the console (mass, moment arm and torque per weight).

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle – Counterweight Balancing Mechanism
//  Revision: Cycle 6
//  Features:
//    - Clamping pitch hub (shaft bore + split clamp)
//    - Slotted counterweight rail with graduated scale
//    - Slotted, sliding lead saddle weights (open-ended bolt slot,
//      so weights can be removed without fully unscrewing the bolt)
//    - Captive locking bolts + under-rail washers
//    - Adjustable end stop
//    - Console balance report (mass / moment per weight)
//  Units: mm, g
// =====================================================================

/* [Pitch Axis Hub] */
shaft_d        = 8;      // pitch shaft diameter
shaft_clr      = 0.2;    // bore clearance
hub_od         = 26;     // hub outer diameter
hub_len        = 20;     // hub length along pitch (X) axis
clamp_gap      = 1.5;    // split-clamp slot width
clamp_screw_d  = 3.2;    // M3 clamp screw clearance

/* [Counterweight Rail] */
rail_start     = 16;     // distance from pivot axis to rail start
rail_len       = 110;    // rail length (along -Y)
rail_w         = 16;     // rail width (X)
rail_t         = 5;      // rail thickness (Z)
rail_drop      = 0;      // Z offset of rail top relative to axis
slot_w         = 5.5;    // M5 sliding slot width
slot_margin    = 8;      // solid material at slot ends
tick_pitch     = 5;      // scale graduation spacing
tick_depth     = 0.5;

/* [Lead Weights] */
w_len          = 18;     // weight length along rail (Y)
w_wall         = 4;      // side wall thickness (wraps rail)
w_top          = 10;     // thickness above rail
w_clr          = 0.4;    // sliding clearance
bolt_d         = 5;      // M5 locking bolt
bolt_head_d    = 9;
bolt_head_h    = 5;
washer_d       = 12;
washer_t       = 1.5;
lead_density   = 11.34e-3;   // g/mm^3

// Positions of weights, measured from pivot axis (mm, along -Y)
weight_positions = [40, 62, 100];

/* [End Stop] */
stop_len       = 6;

/* [Display] */
explode        = 0;      // lifts weights for inspection
show_bolts     = true;
$fn            = 48;

// ---------------------------------------------------------------------
//  Derived values
// ---------------------------------------------------------------------
rail_y0  = -rail_start;              // near end (Y)
rail_y1  = -(rail_start + rail_len); // far end (Y)
rail_z   = rail_drop - rail_t;       // rail bottom Z
w_width  = rail_w + 2*w_clr + 2*w_wall;
w_height = rail_t + w_clr + w_top;

// =====================================================================
//  MAIN MODULE
// =====================================================================
module pitch_counterweight_mechanism(positions = weight_positions,
                                     expl = explode,
                                     bolts = show_bolts) {
    color("SlateGray") pitch_hub();
    color("LightSteelBlue") counterweight_rail();
    color("DimGray") end_stop();

    for (p = positions) {
        assert(p - w_len/2 >= rail_start + slot_margin,
               str("Weight at ", p, " mm collides with hub end"));
        assert(p + w_len/2 <= rail_start + rail_len - slot_margin - stop_len,
               str("Weight at ", p, " mm beyond end stop"));
        translate([0, -p, rail_z + expl]) {
            color([0.45,0.47,0.5]) slotted_lead_weight();
            if (bolts) locking_bolt();
        }
    }
    balance_report(positions);
}

// ---------------------------------------------------------------------
//  Pitch hub with split clamp; axis along X
// ---------------------------------------------------------------------
module pitch_hub() {
    difference() {
        union() {
            rotate([0,90,0]) cylinder(d = hub_od, h = hub_len, center = true);
            // web joining hub to rail
            translate([-rail_w/2, rail_y0 - 0.01, rail_z])
                cube([rail_w, rail_start + 0.01, rail_t]);
            // clamp ear
            translate([-hub_len/2, -4, hub_od/2 - 3])
                cube([hub_len, 8, 9]);
        }
        // shaft bore
        rotate([0,90,0])
            cylinder(d = shaft_d + shaft_clr, h = hub_len + 2, center = true);
        // split slot
        translate([-hub_len/2 - 1, -clamp_gap/2, 0])
            cube([hub_len + 2, clamp_gap, hub_od]);
        // clamp screw (across the split, along Y)
        translate([0, 0, hub_od/2 + 2])
            rotate([90,0,0]) cylinder(d = clamp_screw_d, h = 20, center = true);
    }
}

// ---------------------------------------------------------------------
//  Rail with longitudinal sliding slot and scale ticks
// ---------------------------------------------------------------------
module counterweight_rail() {
    difference() {
        translate([-rail_w/2, rail_y1, rail_z])
            cube([rail_w, rail_len, rail_t]);
        // sliding slot (rounded ends)
        hull() {
            translate([0, rail_y0 - slot_margin, rail_z - 1])
                cylinder(d = slot_w, h = rail_t + 2);
            translate([0, rail_y1 + slot_margin, rail_z - 1])
                cylinder(d = slot_w, h = rail_t + 2);
        }
        // graduation ticks along both edges (distance from pivot)
        for (d = [ceil(rail_start/tick_pitch)*tick_pitch :
                  tick_pitch : rail_start + rail_len - 1]) {
            major = (d % (2*tick_pitch) == 0);
            tl = major ? 4 : 2;
            for (s = [-1, 1])
                translate([s*(rail_w/2 - tl/2), -d, rail_drop - tick_depth/2])
                    cube([tl, 0.6, tick_depth + 0.01], center = true);
        }
        // end-stop screw hole
        translate([0, rail_y1 + stop_len/2, rail_z - 1])
            cylinder(d = 3.2, h = rail_t + 2);
    }
}

// ---------------------------------------------------------------------
//  Slotted lead saddle weight (local origin: rail bottom, weight centre)
//  - Channel underneath wraps the rail sides
//  - Bolt slot open toward the far (-Y) end for tool-less removal
//  - Counterbore keeps the bolt head flush
// ---------------------------------------------------------------------
module slotted_lead_weight() {
    difference() {
        // body with chamfered top edges
        hull() {
            translate([-w_width/2, -w_len/2, -0.01])
                cube([w_width, w_len, w_height - 1.5]);
            translate([-w_width/2 + 1.5, -w_len/2 + 1.5, 0])
                cube([w_width - 3, w_len - 3, w_height]);
        }
        // rail channel
        translate([-(rail_w/2 + w_clr), -w_len/2 - 1, -0.02])
            cube([rail_w + 2*w_clr, w_len + 2, rail_t + w_clr]);
        // open-ended bolt slot (centre -> far end)
        hull() {
            translate([0, 0, -1]) cylinder(d = bolt_d + 0.4, h = w_height + 2);
            translate([0, -w_len, -1]) cylinder(d = bolt_d + 0.4, h = w_