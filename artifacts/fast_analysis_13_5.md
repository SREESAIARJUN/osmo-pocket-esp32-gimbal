# Pitch-Axis Counterweight Balancing Mechanism (Cycle 13)

This OpenSCAD file models a counterweight arm for a camera cradle's pitch axis. Slotted lead plates stack on a stud mounted to a sliding carriage, and you balance the cradle by moving the carriage along a graduated rail.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancing Mechanism
//  Slotted sliding lead weights  -  Cycle 13
//
//  Coordinate convention:
//    Pitch axis      = Y axis (through origin)
//    Counterweight arm extends along -X (opposite the camera)
//    Arm top surface = Z = arm_t
// =====================================================================

$fn = 64;

// ---------------- Top-level parameters ----------------
/* [Arm] */
arm_len      = 140;   // length from pitch axis to arm tip (mm)
arm_w        = 22;    // arm width (Y)
arm_t        = 6;     // arm thickness (Z)
slot_w       = 6.6;   // through-slot width (M6 clearance)
slot_margin  = 10;    // solid material at slot ends

/* [Hub / pitch axis] */
hub_r        = 16;
hub_len      = 30;    // along Y
bore_d       = 8;     // pitch shaft diameter
bearing_d    = 22;    // 608 bearing seat
bearing_w    = 7;
set_screw_d  = 3.2;

/* [Carriage] */
carr_len     = 30;    // along X
carr_wall    = 4;
carr_clr     = 0.4;   // sliding clearance
stud_d       = 6;     // M6 stud
stud_len     = 45;

/* [Slotted lead plates] */
plate_r      = 22;
plate_t      = 5;
plate_gap    = 0.3;   // visual gap between plates
n_plates     = 5;     // number of plates installed (0..)
plate_slot_clr = 0.6;

/* [Balancing] */
weight_pos   = 85;    // carriage centre distance from pitch axis (mm)
show_scale   = true;
explode      = 0;     // set >0 to explode plates upward

// Material
lead_density = 11.34e-3;  // g/mm^3

// ---------------- Derived ----------------
slot_x_min  = hub_r + slot_margin;          // nearest slot end (dist from axis)
slot_x_max  = arm_len - slot_margin;        // farthest slot end
pos_clamped = max(slot_x_min + carr_len/2,
                  min(weight_pos, slot_x_max - carr_len/2));

// =====================================================================
//  MAIN MODULE
// =====================================================================
module pitch_counterweight(pos = pos_clamped, plates = n_plates) {
    color("SteelBlue")   counterweight_arm();
    color("Silver")      translate([-pos, 0, 0]) carriage();
    color("DarkSlateGray")
        translate([-pos, 0, arm_t + carr_wall])
            plate_stack(plates);
    color("Gold")
        translate([-pos, 0, arm_t + carr_wall + plates*(plate_t+plate_gap) + explode*plates])
            thumb_nut();

    // ---- mass / moment report ----
    m_plate = plate_mass();
    m_tot   = plates * m_plate;
    echo(str("Cycle13 CW: plate mass = ", m_plate, " g, plates = ", plates,
             ", total lead = ", m_tot, " g"));
    echo(str("Cycle13 CW: arm position = ", pos, " mm, balancing moment = ",
             m_tot * pos / 1000, " g*m"));
}

// =====================================================================
//  ARM + HUB
// =====================================================================
module counterweight_arm() {
    difference() {
        union() {
            // hub on pitch axis
            translate([0, hub_len/2, arm_t/2])
                rotate([90, 0, 0]) cylinder(r = hub_r, h = hub_len);
            // arm bar
            translate([-arm_len, -arm_w/2, 0])
                cube([arm_len, arm_w, arm_t]);
            // rounded tip
            translate([-arm_len, 0, 0]) cylinder(d = arm_w, h = arm_t);
        }
        // shaft bore
        translate([0, hub_len/2 + 1, arm_t/2])
            rotate([90, 0, 0]) cylinder(d = bore_d, h = hub_len + 2);
        // bearing seats on both faces
        for (s = [-1, 1])
            translate([0, s*(hub_len/2 - bearing_w/2) + s*0.01, arm_t/2])
                rotate([90, 0, 0])
                    cylinder(d = bearing_d, h = bearing_w + 0.02, center = true);
        // radial set screw into hub (top)
        translate([0, 0, arm_t/2]) cylinder(d = set_screw_d, h = hub_r + 1);
        // long sliding slot
        hull() for (x = [slot_x_min, slot_x_max])
            translate([-x, 0, -1]) cylinder(d = slot_w, h = arm_t + 2);
        // graduation grooves
        if (show_scale) graduations();
    }
}

module graduations() {
    for (x = [ceil(slot_x_min/5)*5 : 5 : slot_x_max]) {
        major = (x % 10 == 0);
        len_g = major ? 6 : 3.5;
        translate([-x - 0.3, arm_w/2 - len_g, arm_t - 0.6])
            cube([0.6, len_g + 0.1, 1]);
    }
}

// =====================================================================
//  SLIDING CARRIAGE (saddle straddling the arm, M6 stud on top)
// =====================================================================
module carriage() {
    in_w = arm_w + 2*carr_clr;
    in_t = arm_t + 2*carr_clr;
    out_w = in_w + 2*carr_wall;
    difference() {
        union() {
            // inverted U saddle
            translate([-carr_len/2, -out_w/2, -carr_clr])
                cube([carr_len, out_w, in_t + carr_wall]);
            // stud
            translate([0, 0, arm_t + carr_wall - 0.01])
                cylinder(d = stud_d, h = stud_len);
            // index pointer
            translate([-0.5, out_w/2 - 0.01, arm_t])
                cube([1, 3, carr_wall]);
        }
        // channel for the arm (open bottom)
        translate([-carr_len/2 - 1, -in_w/2, -carr_clr - 0.01])
            cube([carr_len + 2, in_w, in_t]);
    }
    // bottom clamp plate + clamp bolt through slot
    translate([-carr_len/2, -out_w/2, -carr_clr - carr_wall])
        difference() {
            cube([carr_len, out_w, carr_wall]);
        }
    translate([0, 0, -carr_clr - carr_wall - 3])
        cylinder(d = 10, h = 3, $fn = 6);                 // bolt head
    translate([0, 0, -carr_clr - carr_wall])
        cylinder(d = slot_w - 0.6, h = in_t + carr_wall); // bolt shank in slot
}

// =====================================================================
//  SLOTTED LEAD PLATES (horseshoe, alternating slot direction)
// =====================================================================
module slotted_plate() {
    difference() {
        cylinder(r = plate_r, h = plate_t);
        // radial slot to stud
        sw = stud_d + plate_slot_clr;
        hull() {
            translate([0, 0, -1]) cylinder(d = sw, h = plate_t + 2);
            translate([plate_r + 1, -sw/2, -1]) cube([0.1, sw, plate_t + 2]);
        }
        // edge chamfer marking groove (orientation reference)
        translate([-plate_r, 0, plate_t - 0.8])
            rotate([0, 0, 45]) cube([2, 2, 1], center = true);
    }
}

module plate_stack(n) {
    for (i = [0 : 1 : n - 1])
        translate([0, 0, i*(plate_t + plate_gap + explode)])
            rotate([0, 0, (i % 2) * 180 + 90])   // alternate slots, never toward arm tip
                slotted_plate();
}

// Thumb nut retaining the stack
module thumb_nut() {
    difference() {
        union() {
            cylinder(d = 18, h = 6