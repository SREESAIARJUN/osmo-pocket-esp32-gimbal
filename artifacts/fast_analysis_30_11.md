# Pitch-Axis Counterweight Balancer (Cycle 30)

This is a parametric OpenSCAD design for a pitch-axis counterweight on a camera gimbal cradle. It has four parts:

- A **hub** that mounts on the pitch shaft. It has a split clamp and a pinch bolt.
- A **slotted rail** with engraved scale ticks so you can repeat a balance setting.
- **U-channel sliding weights** that straddle the rail. Each weight locks with a bolt through the rail slot into a clamp plate that holds a captive nut. Each weight can be solid lead (machined or cast) or a printed shell with a cavity for lead shot.
- A **removable end stop**. Take it off to slide weights on or off the rail.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights  |  Cycle 30
//  Units: mm.  Pitch axis = Y axis.  Rail extends along +X.
// =====================================================================

$fn = 48;

// ---------------------------- PARAMETERS -----------------------------
/* [Hub / Pitch Axis] */
hub_d        = 30;     // hub outer diameter
hub_len      = 22;     // hub length along pitch axis (Y)
shaft_d      = 8;      // pitch shaft diameter
shaft_clr    = 0.15;   // radial clearance on shaft
clamp_gap    = 1.5;    // split-clamp kerf
clamp_bolt_d = 3.4;    // M3 pinch bolt clearance
clamp_nut_af = 5.6;    // M3 nut across flats (+clr)

/* [Rail] */
rail_len     = 130;    // from pitch axis to rail tip
rail_w       = 20;     // rail width (Y)
rail_t       = 6;      // rail thickness (Z)
slot_w       = 6.4;    // M6 clearance slot
slot_start   = 28;     // slot begins at this X
slot_end_mgn = 14;     // solid material left at tip
tick_pitch   = 5;      // scale engraving spacing
tick_depth   = 0.6;

/* [Sliding Weights] */
n_weights    = 2;
weight_pos   = [45, 85];      // X centre of each weight along rail
weight_L     = 26;            // length along rail (X)
weight_H     = 22;            // height above rail top
weight_wall  = 5;             // skirt wall thickness
fit_clr      = 0.35;          // sliding clearance per side
lock_bolt_d  = 6.4;           // M6 clearance
lock_head_d  = 11;            // M6 socket head counterbore
lock_head_h  = 6.5;
hollow_shell = false;         // true = printed shell for lead shot fill
shell_wall   = 2.0;
fill_port_d  = 6;

/* [Clamp Plate under rail] */
plate_t      = 5;
nut_af       = 10.2;          // M6 nut across flats (+clr)
nut_h        = 5.2;

/* [End Stop] */
stop_L       = 8;
stop_H       = 8;
stop_screw_d = 3.4;

/* [Display] */
explode      = 0;             // 0..1 exploded view
show_rail    = true;
show_weights = true;

LEAD_DENSITY = 11.34e-3;      // g / mm^3

// ---------------------------- DERIVED --------------------------------
slot_end    = rail_len - slot_end_mgn;
chan_w      = rail_w + 2*fit_clr;                 // inner channel width
weight_W    = chan_w + 2*weight_wall;             // outer width
skirt_depth = rail_t + fit_clr;                   // skirt reaches rail bottom

// ============================ MODULES ================================

// --- Pitch hub with split clamp, blended into rail root ---
module pitch_hub() {
    difference() {
        union() {
            rotate([90,0,0]) cylinder(d=hub_d, h=hub_len, center=true);
            // pinch boss
            translate([-hub_d/2 - 4, 0, 0])
                cube([10, hub_len, 12], center=true);
        }
        // shaft bore
        rotate([90,0,0])
            cylinder(d=shaft_d + 2*shaft_clr, h=hub_len+2, center=true);
        // split kerf (toward -X, away from rail)
        translate([-hub_d/2 - 5, 0, 0])
            cube([hub_d/2 + 6, hub_len+2, clamp_gap], center=true);
        // pinch bolt (vertical through boss)
        translate([-hub_d/2 - 4, 0, 0])
            cylinder(d=clamp_bolt_d, h=20, center=true);
        // captive nut on underside
        translate([-hub_d/2 - 4, 0, -6 - 0.01])
            cylinder(d=clamp_nut_af/cos(30), h=3, $fn=6);
    }
}

// --- Slotted rail with engraved scale ---
module rail_body() {
    difference() {
        union() {
            // root gusset blending hub to rail
            hull() {
                rotate([90,0,0]) cylinder(d=hub_d*0.8, h=rail_w, center=true);
                translate([slot_start - 6, -rail_w/2, -rail_t/2])
                    cube([1, rail_w, rail_t]);
            }
            // straight rail
            translate([0, -rail_w/2, -rail_t/2])
                cube([rail_len, rail_w, rail_t]);
        }
        // shaft clearance through gusset
        rotate([90,0,0])
            cylinder(d=shaft_d + 2*shaft_clr, h=hub_len+2, center=true);
        // adjustment slot (rounded ends)
        hull() {
            translate([slot_start, 0, 0]) cylinder(d=slot_w, h=rail_t+2, center=true);
            translate([slot_end,   0, 0]) cylinder(d=slot_w, h=rail_t+2, center=true);
        }
        // scale ticks on both top edges (long tick every 25 mm)
        for (x = [slot_start : tick_pitch : slot_end])
            for (s = [-1, 1]) {
                tl = ((x - slot_start) % 25 == 0) ? 5 : 2.5;
                translate([x - 0.3, s*(rail_w/2) - (s>0 ? tl : 0),
                           rail_t/2 - tick_depth])
                    cube([0.6, tl, tick_depth + 0.01]);
            }
        // end-stop screw holes (2x, vertical)
        for (dx = [-stop_L/4, stop_L/4])
            translate([rail_len - stop_L/2 + dx, 0, 0])
                cylinder(d=stop_screw_d - 0.6, h=rail_t+2, center=true); // tap-size
    }
}

// --- Removable end stop (screwed on tip, retains weights) ---
module end_stop() {
    translate([rail_len - stop_L, -rail_w/2, rail_t/2])
    difference() {
        cube([stop_L, rail_w, stop_H]);
        for (dx = [-stop_L/4, stop_L/4]) {
            translate([stop_L/2 + dx, rail_w/2, -1])
                cylinder(d=stop_screw_d, h=stop_H+2);
            translate([stop_L/2 + dx, rail_w/2, stop_H - 2.5])
                cylinder(d=6, h=3);
        }
    }
}

// --- One slotted sliding lead weight (U-channel straddling rail) ---
module sliding_weight() {
    z0 = -rail_t/2 - fit_clr;              // skirt bottom
    z1 = rail_t/2 + fit_clr + weight_H;    // weight top
    difference() {
        translate([-weight_L/2, -weight_W/2, z0])
            cube([weight_L, weight_W, z1 - z0]);
        // rail channel (slot) - open at bottom and both ends
        translate([-weight_L/