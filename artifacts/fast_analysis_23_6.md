# OpenSCAD Pitch-Axis Counterweight Module (Cycle 23)

The design has two parts:

- **Counterweight arm.** It has a hub with bearing pockets and an anti-rotation flat. A slotted rail extends from the hub, with a 5 mm graduated scale and an end stop that keeps the weights from sliding off.
- **Sliding lead weights.** Each weight is a U-sleeve that rides on the rail and holds a hollow pocket for cast lead or lead shot. It is locked in place by an M4 thumbscrew and a captive nut that run through the rail slot.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle — Counterweight Balancing Mechanism
//  Slotted rail + sliding lead-filled weights           (Cycle 23)
// =====================================================================
//  Coordinate frame (local):
//    Pitch axis  = Y axis through origin
//    Arm extends = +X (away from camera side)
//    Up          = +Z
// =====================================================================

$fn = 48;

// ---------------- Global parameters ----------------
/* [Pitch hub] */
hub_d          = 34;     // hub outer diameter
hub_w          = 16;     // hub width along pitch axis
bearing_od     = 22.1;   // 608 bearing OD (+clearance)
bearing_w      = 7;      // bearing width
shaft_d        = 8.4;    // pitch shaft clearance
flat_depth     = 0.8;    // D-flat for anti-rotation key

/* [Rail] */
arm_len        = 130;    // hub centre -> rail tip
rail_w         = 20;
rail_t         = 8;
slot_w         = 4.4;    // M4 clearance
slot_start     = 30;     // from pitch axis
slot_end       = 120;
tick_pitch     = 5;      // graduation spacing (mm)
stop_h         = 5;      // end-stop lip height

/* [Sliding weight] */
w_len          = 22;     // length along rail
w_wall         = 3;      // sleeve wall
w_clear        = 0.4;    // sliding clearance
lead_h         = 18;     // lead pocket box height below sleeve
lead_wall      = 2;
fill_port_d    = 6;
bolt_d         = 4.4;
nut_af         = 7.2;    // M4 nut across flats (+clearance)
nut_h          = 3.4;
knob_d         = 16;
knob_h         = 7;

/* [Assembly] */
weight_positions = [55, 95];   // rail stations (mm from pitch axis)
explode          = 0;          // >0 lifts weights for inspection
show_lead        = true;

// ---------------- Derived ----------------
lead_density = 11.34e-3;  // g/mm^3
function lead_cavity_vol() =
    (w_len - 2*lead_wall) *
    (rail_w + 2*(w_wall+w_clear) - 2*lead_wall) *
    (lead_h - 2*lead_wall);
function lead_mass_g() = lead_cavity_vol() * lead_density;
function total_moment(pos) =               // g·mm about pitch axis
    len(pos) == 0 ? 0 :
    lead_mass_g()*pos[0] + total_moment([for (i=[1:1:len(pos)-1]) pos[i]]);

// =====================================================================
//  Top-level module
// =====================================================================
module pitch_counterweight(positions = weight_positions,
                           explode   = explode,
                           show_lead = show_lead) {
    color("SlateGray") counterweight_arm();

    for (p = positions) {
        assert(p - w_len/2 >= slot_start && p + w_len/2 <= slot_end,
               str("Weight at ", p, " mm is outside slot range"));
        translate([p, 0, explode])
            sliding_weight(show_lead);
    }

    echo(str("Cycle23: lead per weight = ", lead_mass_g(), " g"));
    echo(str("Cycle23: total counter-moment = ",
             total_moment(positions)/1000, " g·m"));
}

// =====================================================================
//  Arm: hub + slotted graduated rail + end stop
// =====================================================================
module counterweight_arm() {
    difference() {
        union() {
            // Pitch hub (axis along Y)
            rotate([90,0,0]) cylinder(d=hub_d, h=hub_w, center=true);

            // Gusseted transition to rail
            hull() {
                rotate([90,0,0]) cylinder(d=hub_d*0.8, h=rail_w, center=true);
                translate([hub_d/2+8, -rail_w/2, -rail_t/2])
                    cube([1, rail_w, rail_t]);
            }

            // Rail
            translate([0, -rail_w/2, -rail_t/2])
                cube([arm_len, rail_w, rail_t]);

            // End stop (prevents weights sliding off)
            translate([arm_len-4, -rail_w/2, -rail_t/2])
                cube([4, rail_w, rail_t + stop_h]);
        }

        // Shaft bore with D-flat
        rotate([90,0,0]) difference() {
            cylinder(d=shaft_d, h=hub_w+2, center=true);
            translate([shaft_d/2 - flat_depth, -shaft_d, -hub_w])
                cube([shaft_d, 2*shaft_d, 2*hub_w]);
        }
        // Bearing pockets both faces
        for (s=[-1,1])
            translate([0, s*(hub_w/2 - bearing_w/2 + 0.01), 0])
                rotate([90,0,0])
                    cylinder(d=bearing_od, h=bearing_w, center=true);

        // Adjustment slot
        hull() for (x=[slot_start, slot_end])
            translate([x,0,0]) cylinder(d=slot_w, h=rail_t+2, center=true);

        // Graduation ticks on top face (long tick every 25 mm)
        for (x=[slot_start : tick_pitch : slot_end])
            for (s=[-1,1])
                translate([x-0.3, s*(rail_w/2) - (s>0 ? (x%25==0?5:3) : 0),
                           rail_t/2-0.5])
                    cube([0.6, (x%25==0)?5:3, 1]);

        // Lightening pockets in hub-side web
        translate([hub_d/2+2, 0, 0])
            rotate([90,0,0]) cylinder(d=4, h=rail_w+2, center=true);
    }
}

// =====================================================================
//  Sliding lead weight (centred on its rail station)
// =====================================================================
module sliding_weight(show_lead=true) {
    in_w  = rail_w + 2*w_clear;
    in_t  = rail_t + 2*w_clear;
    out_w = in_w + 2*w_wall;
    out_t = in_t + 2*w_wall;

    color("DarkOrange") difference() {
        union() {
            // Sleeve around rail
            translate([-w_len/2, -out_w/2, -out_t/2])
                cube([w_len, out_w, out_t]);
            // Lead box underneath
            translate([-w_len/2, -out_w/2, -out_t/2 - lead_h])
                cube([w_len, out_w, lead_h]);
        }

        // Rail channel (open both ends)
        translate([-w_len/2-1, -in_w/2, -in_t/2])
            cube([w_len+2, in_w, in_t]);

        // Lead cavity
        translate([-w_len/2+lead_wall, -out_w/2+lead_wall,
                   -out_t/2 - lead_h + lead_wall])
            cube([w_len-2*lead_wall, out_w-2*lead_wall, lead_h-2*lead_wall]);

        // Fill port (side), plug with M6 set screw or epoxy
        translate([0, out_w/2+1, -out_t/2 - lead_h/2])
            rotate([90,0,0]) cylinder(d=fill_port_d, h=lead_wall+2);

        // Clamp bolt through sleeve top and bottom (passes through rail slot)
        cylinder(d=bolt_d, h=out_t+2, center=true);

        // Captive hex nut trap in sleeve floor
        translate([0,0,-in_t/2 - nut_h + 0.01])
            cylinder(d=nut_af/cos(30), h=nut_h, $fn=6);

        // Witness index notch (reads against rail graduations)
        translate([-0.4, -out_w/2-0.1, out_t/2-2]) cube([0.8, 1.2, 2.1]);
    }

    // Lead fill (visualisation only)
    if (show_lead)
        color("DimGray", 0.9)
        translate([-w_len/2+lead_wall+0.2, -out_w/2+lead_wall+0.2,
                   -out_t/2 - lead_h + lead_wall+0.2])
            cube([w_len-2*lead_wall-0.4, out_w-2*lead_wall-0.4,
                  lead_h-2*lead_wall-0.4]);

    // Thumbscrew knob
    translate([0,0,out_t/2]) thumbscrew();
}

module thumbscrew() {
    color("Black") {
        difference() {
            cylinder(d=knob_d, h=knob_h);
            for (a=[0: