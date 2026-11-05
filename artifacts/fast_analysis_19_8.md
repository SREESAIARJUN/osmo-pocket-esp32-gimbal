# Pitch-Axis Counterweight Balancer with Slotted Sliding Lead Weights (Cycle 19)

This OpenSCAD design is a pitch-axis counterweight for a camera cradle. A slotted rail extends behind the pitch axis. A T-nut carriage slides in the rail slot and carries a threaded stud. Slotted lead plates slip sideways onto the stud and are locked by a knob.

The module also calculates the stack mass and the position that balances the camera. You can enable auto-balance so the weights move to that position automatically.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle — Counterweight Balancing Mechanism
//  Slotted sliding lead weights on a slotted rail   (Design Cycle 19)
//  Units: mm, g.   Pitch axis = X axis through origin.
//  Camera side = +Y, counterweight rail = -Y.
// =====================================================================

/* [Camera payload] */
cam_mass        = 650;   // g, camera + lens
cam_cg_offset   = 38;    // mm, CG distance forward of pitch axis (+Y)

/* [Pitch hub] */
hub_d           = 32;
hub_len         = 24;
bearing_od      = 22;    // 608 bearing
bearing_w       = 7;
shaft_d         = 8;

/* [Counterweight rail] */
rail_len        = 140;   // from axis to rail end
rail_w          = 22;
rail_t          = 6;
slot_w          = 6.5;   // M6 stud clearance
slot_start      = 30;    // first usable distance from axis
slot_end_margin = 10;
tslot_w         = 11;    // T-nut pocket width (under rail)
tslot_d         = 3;

/* [Slotted lead weights] */
plate_d         = 40;
plate_t         = 5;
plate_count     = 4;
plate_slot_w    = 6.6;   // open slot that slips over the M6 stud
lead_density    = 11.34e-3; // g/mm^3

/* [Carriage / clamp] */
stud_d          = 6;
stud_len        = 40;
carriage_len    = 26;
carriage_mass   = 35;    // g, steel T-nut + stud + knob (estimate)
knob_d          = 24;
knob_h          = 10;

/* [Positioning] */
auto_balance    = true;
manual_pos      = 80;    // used when auto_balance = false
explode         = 0;     // >0 to separate the weight plates
$fn             = 64;

// ---------------------------------------------------------------------
// Mass / balance helpers
// ---------------------------------------------------------------------
function plate_area(d, s) =
    let(r = d/2)
    PI*r*r - s*r - PI*(s/2)*(s/2)/2;          // disc minus U-slot (approx.)

function plate_mass(d, t, s) = plate_area(d, s) * t * lead_density;

function stack_mass(n) = n * plate_mass(plate_d, plate_t, plate_slot_w)
                         + carriage_mass;

function balance_pos(n) = (cam_mass * cam_cg_offset) / stack_mass(n);

function slot_limit() = rail_len - slot_end_margin - stud_d/2;

// ---------------------------------------------------------------------
// Parts
// ---------------------------------------------------------------------
module pitch_hub() {
    difference() {
        rotate([0,90,0]) cylinder(d=hub_d, h=hub_len, center=true);
        rotate([0,90,0]) cylinder(d=shaft_d+1, h=hub_len+2, center=true);
        // bearing seats both sides
        for (s=[-1,1])
            translate([s*(hub_len/2 - bearing_w/2 + 0.01),0,0])
                rotate([0,90,0])
                    cylinder(d=bearing_od, h=bearing_w, center=true);
    }
}

module counterweight_rail() {
    difference() {
        union() {
            // main bar
            translate([-rail_w/2, -rail_len, -rail_t/2])
                cube([rail_w, rail_len - hub_d/2 + 2, rail_t]);
            // gusset blending into hub
            hull() {
                translate([-rail_w/2, -hub_d/2 - 12, -rail_t/2])
                    cube([rail_w, 12, rail_t]);
                rotate([0,90,0]) cylinder(d=hub_d*0.8, h=rail_w, center=true);
            }
            // end stop
            translate([-rail_w/2, -rail_len, -rail_t/2])
                cube([rail_w, 4, rail_t + 4]);
        }
        // through slot for stud
        hull() for (y=[slot_start, slot_limit()])
            translate([0,-y,0]) cylinder(d=slot_w, h=rail_t+2, center=true);
        // T-nut channel underneath
        hull() for (y=[slot_start, slot_limit()])
            translate([-tslot_w/2, -y - carriage_len/2, -rail_t/2 - 0.01])
                cube([tslot_w, carriage_len, tslot_d]);
        // position graduations every 10 mm (engraved top face)
        for (y=[slot_start : 10 : slot_limit()])
            translate([rail_w/2 - 4, -y - 0.4, rail_t/2 - 0.6])
                cube([4.1, 0.8, 1]);
        // hub bore clearance
        rotate([0,90,0]) cylinder(d=shaft_d+1, h=rail_w+2, center=true);
    }
}

module carriage() {
    // T-nut that rides in the underside channel
    color("silver") {
        translate([-tslot_w/2 + 0.3, -carriage_len/2, -rail_t/2])
            cube([tslot_w - 0.6, carriage_len, tslot_d - 0.3]);
        // stud
        translate([0,0,-rail_t/2])
            cylinder(d=stud_d, h=stud_len);
    }
}

module lead_plate() {
    color("DimGray")
    difference() {
        cylinder(d=plate_d, h=plate_t);
        // open slot: slides sideways onto the stud
        translate([0,0,-1]) {
            cylinder(d=plate_slot_w, h=plate_t+2);
            translate([0,-plate_slot_w/2,0])
                cube([plate_d, plate_slot_w, plate_t+2]);
        }
        // chamfer the slot mouth for easier loading
        translate([plate_d/2, 0, -1])
            rotate([0,0,45]) cube([4,4,plate_t+2], center=false);
    }
}

module clamp_knob() {
    color("orange")
    difference() {
        union() {
            cylinder(d=knob_d*0.55, h=3);
            translate([0,0,3])
            intersection() {
                cylinder(d=knob_d, h=knob_h);
                union() for (a=[0:60:359])
                    rotate([0,0,a]) translate([knob_d/2-2,0,0])
                        cylinder(d=6, h=knob_h);
                // central body
            }
            translate([0,0,3]) cylinder(d=knob_d*0.75, h=knob_h);
        }
        translate([0,0,-1]) cylinder(d=stud_d-1, h=knob_h+2); // tap M6
    }
}

module weight_stack(n) {
    for (i=[0:n-1])
        translate([0,0, rail_t/2 + i*(plate_t + explode)])
            // alternate slot orientation so the stack can't slip off
            rotate([0,0, (i%2)*180]) lead_plate();
    translate([0,0, rail_t/2 + n*(plate_t + explode) + 0.2])
        clamp_knob();
}

// ---------------------------------------------------------------------
// Top-level module
// ---------------------------------------------------------------------
module pitch_counterweight_cycle19(n = plate_count,
                                   pos = auto_balance ? balance_pos(plate_count)
                                                      : manual_pos) {
    p = min(max(pos, slot_start), slot_limit());

    // diagnostics
    echo(str("[C19] plate mass  = ", plate_mass(plate_d,plate_t,plate_slot_w), " g"));
    echo(str("[C19] stack mass  = ", stack_mass(n), " g"));
    echo(str("[C19] balance pos = ", balance_pos(n), " mm  (used ", p, " mm)"));
    if (balance_pos(n) > slot_limit())
        echo("[C19] WARNING: add plates — balance point beyond rail end");
    if (balance_pos(n) < slot_start)
        echo("[C19] WARNING: remove plates — balance point inside min slot position");
    echo(str("[C19] residual moment = ",
             cam_mass*cam_cg_offset - stack_mass(n)*p, " g·mm"));

    color("SteelBlue") pitch_hub();
    color("LightSteelBlue") counterweight_rail();

    translate([0,-p,0]) {
        carriage();
        weight_stack(n);
    }

    // payload CG marker (reference only)
    %translate([0, cam_c