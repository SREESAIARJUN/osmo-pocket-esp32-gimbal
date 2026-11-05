# Pitch Axis Counterweight Balancer (Cycle 1)

The counterweight is a slotted arm that clamps onto the pitch shaft. Printed carriers slide along the arm. Each carrier holds two lead slugs and locks in place with an M4 bolt through the slot into a captive nut. Graduations along the arm let you record and repeat balance positions. The script also prints the estimated lead mass and balancing moment to the console.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer  —  CYCLE 1
//  Slotted arm + sliding lead-weight carriers
//  Units: mm, grams
// =====================================================================

$fn = 64;

// ---------------- Global parameters ----------------
// Pitch shaft / hub
shaft_d        = 8.0;     // pitch axis shaft diameter
shaft_clear    = 0.2;     // print clearance on bore
hub_od         = 22;
hub_len        = 16;      // along pitch axis (Y)
clamp_gap      = 1.5;     // split width for pinch clamp
clamp_bolt_d   = 3.4;     // M3 pinch bolt
clamp_nut_af   = 5.7;     // M3 nut across flats (+clearance)

// Arm
arm_len        = 120;     // from axis centre to arm tip
arm_w          = 16;      // width (Y)
arm_t          = 6;       // thickness (Z)
slot_w         = 4.4;     // M4 clearance slot
slot_start     = 25;      // slot begins this far from axis
slot_end_margin= 8;
guide_w        = 9;       // shallow alignment channel on top face
guide_depth    = 1.2;
tick_pitch     = 5;       // graduation spacing
tick_major     = 25;

// Sliding weight carrier
w_len          = 24;      // along arm (X)
w_wid          = 20;      // across arm (Y)
w_h            = 14;      // height (Z)
key_clear      = 0.25;    // clearance of key in guide channel
bolt_d         = 4.4;     // M4 clearance
nut_af         = 7.2;     // M4 nut across flats (+clearance)
nut_h          = 3.4;

// Lead slug pockets (2 per carrier, either side of bolt)
slug_d         = 8.5;     // pocket diameter
slug_depth     = 10;      // pocket depth
slug_pitch     = 7.5;     // pocket offset from carrier centre (X)
slug_floor     = 1.6;     // material under pockets

lead_density   = 11.34e-3; // g/mm^3

// Assembly: carrier positions (distance from pitch axis, mm)
weight_positions = [55, 90];

// ---------------- Derived values ----------------
slot_len   = arm_len - slot_start - slot_end_margin;
slug_vol   = PI * pow(slug_d/2, 2) * slug_depth;
slug_mass  = slug_vol * lead_density;
carrier_lead_mass = 2 * slug_mass;

// ---------------- Helpers ----------------
module hex_prism(af, h) {
    cylinder(d = af / cos(30), h = h, $fn = 6);
}

// =====================================================================
//  ARM WITH CLAMP HUB
//  Pitch axis along Y through origin; arm extends along +X.
// =====================================================================
module counterweight_arm() {
    difference() {
        union() {
            // Hub
            rotate([90, 0, 0])
                cylinder(d = hub_od, h = hub_len, center = true);
            // Arm bar
            translate([0, -arm_w/2, -arm_t/2])
                cube([arm_len, arm_w, arm_t]);
            // Rounded tip
            translate([arm_len, 0, -arm_t/2])
                cylinder(d = arm_w, h = arm_t);
            // Clamp ears (opposite side from arm, -X)
            translate([-hub_od/2 - 6, -hub_len/2, -5])
                cube([10, hub_len, 10]);
        }

        // Shaft bore
        rotate([90, 0, 0])
            cylinder(d = shaft_d + shaft_clear, h = hub_len + 2, center = true);

        // Clamp split (through ears toward -X)
        translate([-hub_od/2 - 8, -hub_len/2 - 1, -clamp_gap/2])
            cube([hub_od/2 + 8, hub_len + 2, clamp_gap]);

        // Pinch bolt (vertical through ears) + nut trap
        translate([-hub_od/2 - 1.5, 0, -10]) {
            cylinder(d = clamp_bolt_d, h = 20);
            hex_prism(clamp_nut_af, 2.5 + 2.5);
        }

        // Through slot for M4 locking bolt
        hull() {
            translate([slot_start, 0, -arm_t]) cylinder(d = slot_w, h = arm_t*2);
            translate([slot_start + slot_len, 0, -arm_t]) cylinder(d = slot_w, h = arm_t*2);
        }

        // Shallow guide channel on top face (keys the carriers)
        translate([slot_start - guide_w/2, -guide_w/2, arm_t/2 - guide_depth])
            cube([slot_len + guide_w, guide_w, guide_depth + 1]);

        // Graduation ticks on +Y side face
        for (x = [slot_start : tick_pitch : slot_start + slot_len]) {
            major = ((x % tick_major) == 0);
            translate([x - 0.3, arm_w/2 - 0.6, major ? -arm_t/2 + 0.8 : 0])
                cube([0.6, 1, major ? arm_t : arm_t/2]);
        }
    }
}

// =====================================================================
//  SLIDING LEAD-WEIGHT CARRIER
//  Local origin: centre of carrier, bottom of key (sits in guide channel)
// =====================================================================
module weight_carrier() {
    key_w = guide_w - 2*key_clear;
    key_h = guide_depth - 0.2;            // small gap so the body seats on arm
    body_z = key_h;

    difference() {
        union() {
            // Alignment key
            translate([-w_len/2 + 2, -key_w/2, 0])
                cube([w_len - 4, key_w, key_h + 0.01]);
            // Main body with chamfered top edges
            translate([0, 0, body_z])
            hull() {
                translate([-w_len/2, -w_wid/2, 0]) cube([w_len, w_wid, w_h - 1.5]);
                translate([-w_len/2 + 1.5, -w_wid/2 + 1.5, 0])
                    cube([w_len - 3, w_wid - 3, w_h]);
            }
        }

        // Bolt hole (vertical, centre)
        translate([0, 0, -1]) cylinder(d = bolt_d, h = w_h + key_h + 2);

        // Captive nut trap from the top
        translate([0, 0, body_z + w_h - nut_h])
            hex_prism(nut_af, nut_h + 1);

        // Lead slug pockets (open top; retain with epoxy or poured lead)
        for (sx = [-slug_pitch, slug_pitch])
            translate([sx, 0, body_z + w_h - slug_depth])
                cylinder(d = slug_d, h = slug_depth + 1);

        // Index pointer notch aligned with carrier centre (+Y face)
        translate([0, w_wid/2, body_z])
            rotate([0, 0, 45]) cube([1.4, 1.4, w_h*2], center = true);
    }
}

// Lead slug reference model (for visualisation / cutting template)
module lead_slug() {
    cylinder(d = slug_d - 0.3, h = slug_depth - 0.5);
}

// =====================================================================
//  ASSEMBLY
// =====================================================================
module counterweight_assembly(positions = weight_positions, show_lead = true) {
    color("SteelBlue") counterweight_arm();

    for (p = positions) {
        // Clamp positions to the usable slot range
        px = min(max(p, slot_start), slot_start + slot_len);
        translate([px, 0, arm_t/2 - guide_depth]) {
            color("Orange") weight_carrier();
            if (show_lead)
                for (sx = [-slug_pitch, slug_pitch])
                    translate([sx, 0, guide_depth - 0.2 + w_h - slug_depth])
                        color("DimGray") lead_slug();
        }
        // M4 bolt visual
        translate([px, 0, -arm_t/2 - 3])
            color("Silver") cylinder(d = 4, h = arm_t + w_h + 2);
    }

    // Console balance report
    total_mass = len(positions) * carrier_lead_mass;
    moment     = sum_moments(positions);
    echo(str("Lead per carrier: ", carrier_lead_mass, " g"));
    echo(str("Total lead: ", total_mass, " g"));
    echo(str("Counter-moment (l