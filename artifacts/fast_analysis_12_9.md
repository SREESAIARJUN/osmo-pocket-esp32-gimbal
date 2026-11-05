# Pitch-Axis Camera Cradle Counterweight Mechanism (Cycle 12)

I couldn't find any context for "Cycle 12," so I treated it as a revision label. This version provides:

- **Fore/aft (Y) balance:** a slotted counterweight rail lets the weight stack slide along it.
- **Vertical (Z) balance:** the rail hangs from slotted drop-brackets, so the center of gravity can be moved toward the pitch axis.
- **Removable weights:** each lead plate has an open-ended slot, so plates can be added or removed by loosening the thumb knob. You don't need to disassemble anything.
- **Anti-rotation:** a keyed carrier block rides in the rail slot so the stack can't spin.
- **Balance report:** the console prints the estimated lead mass and pitch moment.

```openscad
// ============================================================
//  Pitch-Axis Camera Cradle Counterweight Balancing Mechanism
//  Slotted sliding lead weights — Cycle 12
//  Units: mm.  Pitch axis = X axis through origin.
// ============================================================

/* [Cradle] */
cradle_w        = 90;    // inner width between cheeks (X)
cradle_d        = 70;    // depth (Y)
cradle_drop     = 35;    // pitch axis -> top of cradle base plate
base_t          = 6;
cheek_t         = 6;
cheek_h         = 60;
bearing_od      = 22;    // 608-style bearing
bearing_w       = 7;
axle_d          = 8;
camera_slot_w   = 6.5;   // 1/4"-20 slot in base plate
camera_slot_l   = 40;

/* [Counterweight Rail] */
rail_len        = 140;   // along Y
rail_w          = 24;
rail_t          = 6;
rail_slot_w     = 6.4;   // M6 clearance / key width
rail_slot_margin= 12;
rail_y_offset   = -20;   // rail centre relative to pitch axis (Y)
rail_drop_adj   = 10;    // 0..bracket_slot_len : vertical tuning
bracket_slot_len= 20;

/* [Lead Weights] */
n_plates        = 4;
plate_l         = 40;    // along Y
plate_w         = 36;    // along X
plate_t         = 8;     // each lead plate thickness
plate_slot_w    = 6.6;   // open slot for M6 bolt
plate_slot_depth= 26;    // how far the open slot runs in from the edge
weight_pos      = 35;    // slider position along rail (Y, from rail centre)

/* [Hardware] */
bolt_d          = 6;
knob_d          = 26;
knob_h          = 10;
lead_density    = 11.34e-3;  // g/mm^3
$fn = 48;

// ---------------- Derived ----------------
rail_z      = -cradle_drop - base_t - 25 - rail_drop_adj;
slide_limit = rail_len/2 - rail_slot_margin - plate_l/2;

assert(abs(weight_pos) <= slide_limit,
       str("weight_pos out of range ±", slide_limit));
assert(rail_drop_adj >= 0 && rail_drop_adj <= bracket_slot_len,
       "rail_drop_adj outside bracket slot");
assert(plate_slot_depth > plate_l/2,
       "plate slot must reach past bolt centre");

// ============================================================
//  Top-level module
// ============================================================
module pitch_counterweight_mechanism(pos = weight_pos,
                                     plates = n_plates,
                                     drop = rail_drop_adj,
                                     show_cradle = true) {
    if (show_cradle) color("SlateGray") cradle();
    color("DimGray")   drop_brackets(drop);
    color("Silver")    translate([0, rail_y_offset, rail_z]) cw_rail();
    translate([0, rail_y_offset + pos, rail_z])
        weight_stack(plates);
    color("Red", 0.4) pitch_axis_marker();
    balance_report(pos, plates);
}

// ============================================================
//  Cradle
// ============================================================
module cradle() {
    // Base plate with camera mounting slot
    difference() {
        translate([-cradle_w/2, -cradle_d/2, -cradle_drop - base_t])
            cube([cradle_w, cradle_d, base_t]);
        hull() for (y = [-1, 1] * (camera_slot_l - camera_slot_w)/2)
            translate([0, y, -cradle_drop - base_t - 1])
                cylinder(d = camera_slot_w, h = base_t + 2);
    }
    // Side cheeks with bearing pockets on the pitch axis
    for (s = [-1, 1]) mirror([s < 0 ? 1 : 0, 0, 0])
        difference() {
            hull() {
                translate([cradle_w/2, -cradle_d/2, -cradle_drop - base_t])
                    cube([cheek_t, cradle_d, 1]);
                translate([cradle_w/2, 0, 0]) rotate([0, 90, 0])
                    cylinder(d = bearing_od + 12, h = cheek_t);
            }
            translate([cradle_w/2 - 0.01, 0, 0]) rotate([0, 90, 0]) {
                cylinder(d = axle_d + 0.4, h = cheek_t + 1);
                translate([0, 0, cheek_t - bearing_w])
                    cylinder(d = bearing_od + 0.2, h = bearing_w + 1);
            }
        }
}

// ============================================================
//  Drop brackets: vertical slots allow Z tuning of the rail
// ============================================================
module drop_brackets(drop) {
    top_z = -cradle_drop - base_t;
    br_h  = top_z - rail_z + rail_t;   // span to rail
    for (x = [-1, 1] * (rail_w/2 + 2))
        translate([x - (x > 0 ? 0 : 4), rail_y_offset - 8, rail_z])
            difference() {
                cube([4, 16, top_z - rail_z]);
                // vertical adjustment slot (rail bolt rides here)
                hull() for (z = [rail_t/2, rail_t/2 + bracket_slot_len])
                    translate([-1, 8, z - drop + rail_drop_adj])
                        rotate([0, 90, 0]) cylinder(d = 4.4, h = 6);
            }
}

// ============================================================
//  Counterweight rail with long sliding slot
// ============================================================
module cw_rail() {
    sl = rail_len - 2*rail_slot_margin;
    difference() {
        translate([-rail_w/2, -rail_len/2, 0]) cube([rail_w, rail_len, rail_t]);
        hull() for (y = [-1, 1] * (sl/2 - rail_slot_w/2))
            translate([0, y, -1]) cylinder(d = rail_slot_w, h = rail_t + 2);
        // graduation notches every 10 mm for repeatable balance settings
        for (y = [-sl/2 : 10 : sl/2])
            translate([rail_w/2 - 1.2, y - 0.4, rail_t - 1]) cube([2, 0.8, 2]);
    }
}

// ============================================================
//  Weight stack: keyed carrier + slotted lead plates + knob
// ============================================================
module weight_stack(plates) {
    // Keyed carrier (sits under rail, key tongue rides in slot)
    color("DarkOrange") translate([0, 0, -4]) {
        difference() {
            translate([-plate_w/2, -plate_l/2, 0]) cube([plate_w, plate_l, 4]);
            translate([0, 0, -1]) cylinder(d = bolt_d + 0.4, h = 6);
        }
        difference() {   // anti-rotation key
            translate([-(rail_slot_w - 0.4)/2, -plate_l/2 + 4, 4])
                cube([rail_slot_w - 0.4, plate_l - 8, rail_t - 1]);
            translate([0, 0, 3]) cylinder(d = bolt_d + 0.4, h = rail_t + 2);
        }
    }
    // Lead plates, alternating slot direction so the stack can't walk off
    for (i = [0 : plates - 1])
        color(i % 2 ? "#6b6f75" : "#585c61")
        translate([0, 0, -4 - (i + 1) * plate_t])
            mirror([0, i % 2, 0]) lead_plate();
    // Bolt
    color("Gainsboro")
        translate([0, 0, -4 - plates*plate_t - knob_h])
            cylinder(d = bolt_d, h = plates*plate_t + knob_h + rail_t + 10);
    // Washer + thumb knob on top of rail
    color("Black") translate([0, 0, rail_t]) {
        cylinder(d = bolt_d * 2.2, h = 1.5);
        translate([0, 0, 1.5]) thumb_knob();
    }
    // Retaining head below stack
    color("Gainsboro")
        translate([0, 0, -4 - plates*plate_t - 4