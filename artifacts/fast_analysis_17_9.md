# Pitch-Axis Counterweight Balancing Mechanism with Slotted Sliding Lead Weights

This OpenSCAD file models a camera cradle on a pitch axis with a slotted rail behind it. A carriage slides along the rail and carries a stack of slotted lead weights. One thumb nut both clamps the carriage to the rail and locks the weight stack.

The script also prints balance figures to the console:
- the mass of the weights
- the moments from the camera and the counterweight
- the ideal carriage position for balance

These numbers only include the camera and the weights, not the cradle's own mass, so treat them as a starting point for tuning.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle - Counterweight Balancing Mechanism
//  Slotted sliding lead weights on a slotted rail   (Design Cycle 17)
// ---------------------------------------------------------------------
//  Coordinate convention:
//    Pitch axis = X axis through origin
//    +Y = lens/forward direction, -Y = rearward (counterweight side)
//    +Z = up
//  All dimensions in mm, masses in g.
// =====================================================================

$fn = 48;

// ---------------- User parameters -----------------------------------
weight_pos      = 85;    // carriage distance behind pitch axis (mm, along -Y)
n_weights       = 4;     // number of slotted lead weights on the stack
explode         = 0;     // >0 lifts the weights apart for inspection
show_camera     = true;  // show a dummy camera body
show_axis       = true;  // show the pitch shaft

camera_mass     = 650;   // g
camera_com_y    = 22;    // camera COM forward of the axis (mm)

// ---------------- Cradle geometry -----------------------------------
cradle_w        = 110;   // inner width between side plates
plate_t         = 5;     // structural plate thickness
side_h          = 70;    // side plate height
side_d          = 90;    // side plate depth (Y)
base_z          = -20;   // top of camera base plate, relative to axis
boss_d          = 26;    // pivot bearing boss diameter
bearing_od      = 22;    // 608 bearing seat
bearing_w       = 7;
shaft_d         = 8;

// ---------------- Rail geometry -------------------------------------
rail_len        = 150;   // rail length behind the cradle
rail_w          = 30;
rail_t          = 6;
rail_z          = base_z - 12;  // top of rail
slot_w          = 6.4;   // fits an M6 T-bolt
slot_margin     = 15;    // solid material at each slot end
rail_y0         = -side_d/2 + 10;  // where the rail joins the cradle

// ---------------- Carriage & weights --------------------------------
carriage_l      = 36;
carriage_w      = 40;
carriage_h      = 8;
stud_d          = 6;
stud_len        = 70;

wt_l            = 44;    // weight footprint along Y
wt_w            = 56;    // weight footprint along X
wt_h            = 9;     // per-weight thickness
wt_slot_w       = 6.6;   // U-slot width (stud clearance)
wt_chamfer      = 1.5;

lead_density    = 0.01134; // g/mm^3

// ---------------- Derived values & balance calculation --------------
slot_len   = rail_len - 2*slot_margin;
pos_min    = -rail_y0 + slot_margin + carriage_l/2;
pos_max    = -rail_y0 + rail_len - slot_margin - carriage_l/2;
pos        = min(max(weight_pos, pos_min), pos_max);

// Approximate volume: solid block minus the U-slot
wt_vol     = wt_l*wt_w*wt_h - (wt_w/2)*wt_slot_w*wt_h;
wt_mass    = wt_vol * lead_density;
stack_mass = n_weights * wt_mass;

cam_moment = camera_mass * camera_com_y;
cw_moment  = stack_mass * pos;
ideal_pos  = stack_mass > 0 ? cam_moment / stack_mass : 0;

echo(str("Per-weight mass: ", wt_mass, " g"));
echo(str("Stack mass: ", stack_mass, " g (", n_weights, " weights)"));
echo(str("Camera moment: ", cam_moment, " g*mm"));
echo(str("Counterweight moment: ", cw_moment, " g*mm"));
echo(str("Net imbalance: ", cam_moment - cw_moment, " g*mm (+ = nose heavy)"));
echo(str("Ideal carriage position: ", ideal_pos, " mm  [travel ", pos_min, " to ", pos_max, "]"));
if (ideal_pos > pos_max) echo("WARNING: add weights - balance point beyond rail travel");
if (ideal_pos < pos_min && n_weights > 0) echo("WARNING: remove weights - balance point inside minimum travel");
if (weight_pos != pos) echo("NOTE: weight_pos clamped to rail travel");

// =====================================================================
//  Top-level module
// =====================================================================
module pitch_counterweight_mechanism(pos = pos, n = n_weights, ex = explode) {
    color("SlateGray") cradle();
    color("Silver")    counterweight_rail();

    translate([0, -pos, rail_z]) {
        color("DimGray") carriage();

        // Weight stack
        for (i = [0 : n - 1])
            translate([0, 0, carriage_h + i*(wt_h + ex)])
                color([0.45, 0.47, 0.5]) slotted_weight();

        // Clamp washer and thumb nut sit on top of the stack
        translate([0, 0, carriage_h + n*wt_h + max(n - 1, 0)*ex]) {
            color("Gainsboro") washer();
            translate([0, 0, 1.6]) color("Black") thumb_nut();
        }
    }

    if (show_camera) color("DarkSlateGray", 0.6) dummy_camera();
    if (show_axis)   color("LightSteelBlue")
        rotate([0, 90, 0]) cylinder(d = shaft_d, h = cradle_w + 4*plate_t + 30, center = true);
}

// =====================================================================
//  Cradle: base plate + two side plates with bearing bosses
// =====================================================================
module cradle() {
    // Base plate
    translate([-cradle_w/2, -side_d/2, base_z - plate_t])
        difference() {
            cube([cradle_w, side_d, plate_t]);
            // 1/4"-20 camera screw slot
            hull() for (y = [side_d/2 - 15, side_d/2 + 15])
                translate([cradle_w/2, y, -1]) cylinder(d = 6.6, h = plate_t + 2);
        }

    // Side plates
    for (s = [-1, 1]) side_plate(s);

    // Rear bracket that carries the rail
    translate([-rail_w/2, -side_d/2, rail_z])
        cube([rail_w, 14, base_z - plate_t - rail_z + 0.01]);
}

module side_plate(s) {
    x0 = s > 0 ? cradle_w/2 : -cradle_w/2 - plate_t;
    difference() {
        union() {
            hull() {
                translate([x0, -side_d/2, base_z - plate_t]) cube([plate_t, side_d, 1]);
                translate([x0, 0, 0]) rotate([0, 90, 0]) cylinder(d = boss_d + 8, h = plate_t);
            }
            // Bearing boss on the outside face
            translate([s > 0 ? x0 + plate_t : x0 - bearing_w, 0, 0])
                rotate([0, 90, 0]) cylinder(d = boss_d + 6, h = bearing_w);
        }
        // Bearing seat
        translate([s > 0 ? x0 + plate_t - 0.01 : x0 - bearing_w - 0.01, 0, 0])
            rotate([0, 90, 0]) cylinder(d = bearing_od, h = bearing_w + 0.02);
        // Shaft clearance
        translate([x0 - 1, 0, 0]) rotate([0, 90, 0]) cylinder(d = shaft_d + 1, h = plate_t + 2);
        // Lightening hole
        translate([x0 - 1, -side_d/4, base_z/2])
            rotate([0, 90, 0]) cylinder(d = 14, h = plate_t + 2);
    }
}

// =====================================================================
//  Slotted rail with graduation marks every 10 mm
// =====================================================================
module counterweight_rail() {
    difference() {
        translate([-rail_w/2, rail_y0 - rail_len, rail_z - rail_t])
            cube([rail_w, rail_len, rail_t]);

        // Through slot for the T-bolt
        hull() for (y = [rail_y0 - slot_margin, rail_y0 - rail_len + slot_margin])
            translate([0, y, rail_z - r