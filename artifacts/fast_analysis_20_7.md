# Pitch Axis Counterweight Mechanism with Slotted Sliding Lead Weights (Cycle 20)

The module below is parametric. It also computes the balance position for the weights and prints it to the console with `echo()`, so you can check it against your camera payload.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Mechanism  —  Cycle 20
//  Slotted rail arm + sliding carriers with cast/poured lead inserts
//
//  Coordinate system:
//    Pitch axis = X axis, through origin
//    Camera side = +Y, counterweight arm extends toward -Y
//    Z = up (arm plate thickness)
//  Units: mm, grams
// =====================================================================

/* [Render] */
part          = "assembly";   // [assembly, arm, carrier, lid, lead, hardware]
show_lead     = true;
show_hardware = true;
explode       = 0;            // [0:1:30]
$fn           = 48;

/* [Pitch Axis Hub] */
shaft_d        = 8.0;     // pitch shaft / bearing ID
shaft_clear    = 0.15;
hub_od         = 26;
hub_len        = 22;      // along X
clamp_slit     = 1.2;
clamp_bolt_d   = 3.4;     // M3 clearance
clamp_nut_af   = 5.6;     // M3 nut across flats (+clearance)

/* [Counterweight Arm] */
arm_len        = 140;     // from axis center to arm tip (-Y)
arm_w          = 30;      // along X
arm_t          = 6;       // plate thickness (Z)
slot_w         = 5.4;     // M5 clearance slot
slot_start     = 30;      // slot begins this far from axis
slot_end_gap   = 8;       // solid material at the tip
rib_h          = 5;       // stiffening ribs under the arm
rib_t          = 3;
tick_pitch     = 5;       // engraved position scale
tick_depth     = 0.6;

/* [Sliding Weight Carrier] */
n_weights      = 2;
carrier_len    = 28;      // along Y
carrier_w      = 30;      // along X
carrier_h      = 18;
wall           = 2.0;
key_h          = 2.0;     // anti-rotation key that rides in the slot
key_clear      = 0.3;
bolt_d         = 5.3;     // M5 clamp bolt
pocket_cols    = 2;       // lead pockets per carrier
pocket_rows    = 1;
pocket_d       = 11;
lid_t          = 2.0;
carrier_gap    = 2;       // spacing between stacked carriers

/* [Mass / Balance] */
camera_mass        = 450;   // g, camera + lens on the cradle
camera_cg_offset   = 22;    // mm, camera CG forward of pitch axis (+Y)
arm_mass_est       = 38;    // g, printed arm (from slicer)
arm_cg_est         = 70;    // mm behind axis
carrier_shell_mass = 9;     // g each, printed shell + lid + bolt
rho_lead           = 11.34e-3;  // g/mm^3
lead_fill          = 0.92;      // fill factor for shot/pour voids

// ---------------------------------------------------------------------
//  Derived values
// ---------------------------------------------------------------------
pocket_depth  = carrier_h - wall - lid_t;
pocket_vol    = PI * pow(pocket_d/2, 2) * pocket_depth;
lead_per_carr = pocket_cols * pocket_rows * pocket_vol * rho_lead * lead_fill;
carrier_mass  = carrier_shell_mass + lead_per_carr;
weights_mass  = n_weights * carrier_mass;

slot_len      = arm_len - slot_start - slot_end_gap;
stack_len     = n_weights * carrier_len + (n_weights - 1) * carrier_gap;

// Moment balance about the pitch axis:
//   m_cam * d_cam = m_arm * d_arm + m_w * r   ->  solve for r
r_required    = (camera_mass * camera_cg_offset - arm_mass_est * arm_cg_est)
                / weights_mass;

// Travel limits for the centroid of the carrier stack
r_min         = slot_start + stack_len/2;
r_max         = arm_len - slot_end_gap - stack_len/2;
r_used        = min(max(r_required, r_min), r_max);

echo(str("[Cycle20] lead per carrier = ", round(lead_per_carr), " g"));
echo(str("[Cycle20] total sliding mass = ", round(weights_mass), " g"));
echo(str("[Cycle20] required stack centroid = ", r_required, " mm behind axis"));
echo(str("[Cycle20] adjustable range = ", r_min, " .. ", r_max, " mm"));
if (r_required < r_min)
    echo("[Cycle20] WARNING: overbalanced. Remove lead or a carrier.");
if (r_required > r_max)
    echo("[Cycle20] WARNING: underbalanced. Add a carrier or lengthen the arm.");

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
module hex_prism(af, h) {
    cylinder(d = af / cos(30), h = h, $fn = 6);
}

module rounded_box(size, r = 2) {
    hull()
        for (x = [r, size[0]-r], y = [r, size[1]-r])
            translate([x, y, 0]) cylinder(r = r, h = size[2]);
}

// ---------------------------------------------------------------------
//  Pitch hub with clamp, plus slotted arm
// ---------------------------------------------------------------------
module pitch_hub() {
    difference() {
        union() {
            rotate([0, 90, 0]) cylinder(d = hub_od, h = hub_len, center = true);
            // clamp ears (above the shaft)
            translate([-hub_len/2, -6, 0]) cube([hub_len, 12, hub_od/2 + 6]);
        }
        // shaft bore
        rotate([0, 90, 0])
            cylinder(d = shaft_d + 2*shaft_clear, h = hub_len + 2, center = true);
        // clamp slit
        translate([-hub_len/2 - 1, -clamp_slit/2, 0])
            cube([hub_len + 2, clamp_slit, hub_od]);
        // clamp bolt across the slit (along Y)
        translate([0, 0, hub_od/2 + 1.5]) rotate([90, 0, 0]) {
            cylinder(d = clamp_bolt_d, h = 30, center = true);
            translate([0, 0, 4]) rotate([0, 0, 30]) hex_prism(clamp_nut_af, 10);
        }
    }
}

module counterweight_arm() {
    union() {
        pitch_hub();
        difference() {
            union() {
                // main plate, top surface flush with z = arm_t/2
                hull() {
                    translate([-arm_w/2, -hub_od/2, -arm_t/2])
                        cube([arm_w, 1, arm_t]);
                    translate([0, -arm_len + arm_w/2, -arm_t/2])
                        cylinder(d = arm_w, h = arm_t);
                }
                // underside stiffening ribs, kept clear of the slot and nuts
                for (sx = [-1, 1])
                    translate([sx*(arm_w/2 - rib_t/2) - rib_t/2,
                               -arm_len + arm_w/2, -arm_t/2 - rib_h])
                        cube([rib_t, arm_len - arm_w/2 - hub_od/2 + 2, rib_h]);
            }
            // through slot
            hull() for (y = [slot_start, arm_len - slot_end_gap])
                translate([0, -y, -arm_t])
                    cylinder(d = slot_w, h = arm_t*3);
            // engraved position scale (mm from the pitch axis)
            for (y = [slot_start : tick_pitch : arm_len - slot_end_gap]) {
                L = (y % (tick_pitch*2) == 0) ? 6 : 3.5;
                translate([slot_w/2 + 1.5, -y - 0.3, arm_t/2 - tick_depth])
                    cube([L, 0.6, tick_depth + 0.01]);
            }
            // balance-point witness mark
            translate([-slot_w/2 - 7, -r_required - 0.5, arm_t/2 - tick_depth])
                cube([5, 1, tick_depth + 0.01]);
        }
    }
}

// ---------------------------------------------------------------------
//  Sliding weight carrier (origin at center of the bottom face)
// ---------------------------------------------------------------------
function pocket_positions() = [
    for (c = [0:pocket_cols-1], r = [0:pocket_rows-1])
        [ (c - (pocket_cols-1)/2) * (carrier_w - 2*wall - pocket_d) / max(pocket_cols-1, 1),
          (r - (pocket_rows-1)/2) * (carrier_len - 2*wall - pocket_d) / max(pocket_rows-1, 1) ]
];

module weight_carrier() {
    difference() {
        union() {
            translate([-carrier_w/2, -carrier_len/2, 0])
                rounded_box([carrier_w, carr