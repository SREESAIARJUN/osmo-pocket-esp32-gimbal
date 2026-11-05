# Pitch-Axis Camera Cradle: Counterweight Balancing Mechanism (Cycle 10)

I don't know what your earlier cycles contained, so this is a self-contained, fully parametric design. If you share the Cycle 9 code or its interface (module names, axis conventions, rail size), I can adapt this to match.

**Design summary**
- The pitch axis runs along **X** through the origin.
- A U-shaped cradle carries the camera plate above the axis.
- A counterweight rail runs rearward (−Y) and below the axis. It has a long through slot.
- Slotted lead weights sit on the rail. Each weight has a U-slot that straddles the rail and a captive M4 clamp screw that passes through the rail slot. Weights can be added, removed or slid without taking anything apart.
- The script echoes the moment balance about the pitch axis.

```openscad
// ============================================================
//  Pitch-Axis Camera Cradle – Counterweight Balancing Mechanism
//  Cycle 10 : slotted sliding lead weights on a slotted rail
//  Units: mm, grams.  Pitch axis = X axis through origin.
// ============================================================

$fn = 48;

// ---------------- Global parameters -------------------------
/* [Cradle] */
cradle_width      = 90;    // inner width between side arms (X)
cradle_depth      = 70;    // camera plate depth (Y)
plate_thk         = 6;
arm_thk           = 6;
arm_height        = 45;    // plate top -> pitch axis
axle_d            = 8;     // pitch shaft diameter
bearing_od        = 22;    // 608 bearing
bearing_w         = 7;
boss_d            = 30;

/* [Counterweight rail] */
rail_len          = 140;   // rearward extension from axis
rail_w            = 20;    // rail width (X)
rail_h            = 10;    // rail thickness (Z)
rail_drop         = 25;    // rail centre below pitch axis
rail_slot_w       = 4.5;   // clearance for M4
rail_slot_margin  = 12;    // solid material at slot ends
rail_scale_pitch  = 10;    // engraved index marks

/* [Lead weights] */
wt_len            = 18;    // along rail (Y)
wt_w              = 44;    // across rail (X)
wt_h              = 30;    // total height (Z)
wt_clear          = 0.4;   // slot clearance around rail
wt_screw_d        = 4.2;   // M4 clamp screw
wt_nut_af         = 7.2;   // M4 nut across flats (+clearance)
wt_nut_h          = 3.4;
lead_density      = 11.34e-3; // g/mm^3

/* [Balance study] */
camera_mass       = 650;   // g
camera_cg_z       = 35;    // camera CG above plate top
camera_cg_y       = 10;    // camera CG forward of axis
weight_positions  = [60, 80, 100];  // distance of each weight from axis (along rail)

/* [Render] */
show              = "assembly"; // "assembly", "cradle", "weight", "print_weights"
explode           = 0;          // mm, lift weights off rail for clarity

// ---------------- Derived values ----------------------------
plate_top_z   = -arm_height;          // plate hangs below axis? no: camera above axis
// Camera sits ABOVE the axis on an under-slung plate? Choose over-slung:
plate_z       = arm_height - plate_thk;   // plate bottom Z (camera above axis)
rail_slot_len = rail_len - 2*rail_slot_margin;
slot_in_w     = rail_w + 2*wt_clear;
slot_in_h     = rail_h + wt_clear;

// ============================================================
//  Utility
// ============================================================
module slot2d(len, w) {
    hull() {
        translate([0, -len/2 + w/2]) circle(d = w);
        translate([0,  len/2 - w/2]) circle(d = w);
    }
}

// ============================================================
//  Cradle (camera plate + side arms + bearing bosses + rail)
// ============================================================
module cradle() {
    difference() {
        union() {
            // Camera plate
            translate([-cradle_width/2 - arm_thk, -cradle_depth/2, plate_z])
                cube([cradle_width + 2*arm_thk, cradle_depth, plate_thk]);

            // Side arms with bearing bosses
            for (s = [-1, 1])
                translate([s*(cradle_width/2 + arm_thk/2), 0, 0])
                    hull() {
                        rotate([0, 90, 0]) cylinder(d = boss_d, h = arm_thk, center = true);
                        translate([0, 0, plate_z + plate_thk/2])
                            cube([arm_thk, cradle_depth*0.6, plate_thk], center = true);
                    }

            // Rail drop web (centre, below axis)
            hull() {
                translate([-rail_w/2, -boss_d/2, -rail_drop - rail_h/2])
                    cube([rail_w, boss_d/2, rail_h]);
                translate([-rail_w/2, -boss_d/2 - 2, plate_z])
                    cube([rail_w, 6, plate_thk]);
            }

            // Counterweight rail
            translate([-rail_w/2, -rail_len, -rail_drop - rail_h/2])
                cube([rail_w, rail_len, rail_h]);
            // End stop (stops weights sliding off)
            translate([-rail_w/2 - 3, -rail_len, -rail_drop - rail_h/2 - 3])
                cube([rail_w + 6, 4, rail_h + 6]);
        }

        // Bearing pockets + shaft bore
        for (s = [-1, 1])
            translate([s*(cradle_width/2 + arm_thk/2), 0, 0])
                rotate([0, 90, 0]) {
                    cylinder(d = axle_d + 0.5, h = arm_thk + 2, center = true);
                    translate([0, 0, s*(arm_thk/2 - bearing_w/2 + 0.01)])
                        cylinder(d = bearing_od + 0.2, h = min(bearing_w, arm_thk), center = true);
                }

        // 1/4"-20 camera mounting slot (fore/aft balance)
        translate([0, 0, plate_z - 1])
            linear_extrude(plate_thk + 2) slot2d(cradle_depth*0.6, 6.6);

        // Rail through-slot for clamp screws
        translate([0, -rail_len/2 - rail_slot_margin/2 + rail_slot_margin/2,
                   -rail_drop - rail_h/2 - 1])
            translate([0, -(rail_len - rail_slot_len)/2 + rail_slot_margin, 0])
                linear_extrude(rail_h + 2)
                    translate([0, -rail_slot_len/2 + rail_slot_len/2 - rail_len/2 + rail_slot_len/2 + (rail_len-rail_slot_len)/2 - rail_slot_margin])
                        slot2d(rail_slot_len, rail_slot_w);

        // Index marks along rail top (every rail_scale_pitch from axis)
        for (d = [rail_scale_pitch : rail_scale_pitch : rail_len - 6])
            translate([rail_w/2 - 4, -d - 0.4, -rail_drop + rail_h/2 - 0.6])
                cube([5, 0.8, 1]);
    }
}

// ============================================================
//  Slotted sliding lead weight
//  U-slot opens downward → drops over the rail from above?
//  No: opens upward from bottom so it can be lifted on/off
//  the rail; clamp screw passes down through rail slot.
// ============================================================
module lead_weight() {
    difference() {
        // Body with chamfered edges
        hull() {
            translate([-wt_w/2 + 1, -wt_len/2, 0]) cube([wt_w - 2, wt_len, wt_h]);
            translate([-wt_w/2, -wt_len/2 + 1, 1]) cube([wt_w, wt_len - 2, wt_h - 2]);
        }

        // U-slot that straddles the rail (open at bottom)
        translate([-slot_in_w/2, -wt_len/2 - 1, -0.01])
            cube([slot_in_w, wt_len + 2, wt_h - (wt_h - slot_in_h)/2 - (wt_h - slot_in_h)/2 + slot_in_h - slot_in_h + (wt_h*0.5)]);

        // Clamp screw bore (vertical, through the roof of the slot)
        translate([0, 0, -1]) cylinder(d = wt_screw_d, h = wt_h + 2);

        // Captive nut trap in roof (screw threads down onto rail)
        translate([0, 0, wt_h*0.5 + 3])
            cylinder(d = wt_nut_af / cos(30), h = wt_nut_h, $fn = 6);

        // Thumb-screw counterbore on top
        translate([0, 0, wt_h - 4]) cylinder(d = 9, h = 5);

        // Side relief slot = "slotted" stacking feature:
        // lets adjacent weights nest a stacking pin / zip-tie
        for (s = [-1, 1])
            translate([s*(wt_w/2), 0, wt_h*0.75])
                cube([6, wt_len + 2, 3], center = true);
    }
}

// Volume / mass estimate (body minus slot, ignoring small holes)
function weight_volume() =
    wt_w*wt_len*wt_h - slot_in_w*wt_len*(wt_h*0.5);
function weight_mass() = weight_volume() * lead_density;

// ============================================================
//  Placement helper: weight on rail at distance d from axis
// ============================================================
module place_weight(d) {
    // Slot roof sits on rail top -> weight bottom at rail top - slot depth
    translate([0, -d, -rail_drop + rail_h/2 - wt_h*0.5 + explode])
        children();
}

// ============================================================
//  Main assembly module
// ============================================================
module counterweight_mechanism(positions = weight_positions) {
    color("SteelBlue") cradle();
    for (d = positions)
        place_weight(d) color("DimGray") lead_weight();

    // Pitch shaft (reference)
    %rotate([0, 90, 0])
        cylinder(d = axle_d, h = c