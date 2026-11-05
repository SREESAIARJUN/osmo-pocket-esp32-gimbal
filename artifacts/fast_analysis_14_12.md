# Pitch-Axis Camera Cradle Counterweight: Slotted Sliding Lead Weights (Cycle 14)

This is a fully parametric OpenSCAD design. It includes:

- A pivot hub on the pitch axis.
- A graduated counterweight arm with a slide slot.
- A sliding carrier sled with a threaded stud.
- Lab-style slotted weights (printed shells filled with lead shot and epoxy) that slip on and off sideways without removing the knob.
- A locking thumb-knob.

The script also prints the lead mass and the balancing moment to the console.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight — Slotted Sliding Lead Weights
//  Revision: Cycle 14
//  Units: mm
//  Pitch axis = X axis through origin. Camera side = +Y, counterweight = -Y
// =====================================================================

/* [Render] */
part        = "assembly";   // [assembly, hub_arm, carrier, weight, weight_lid, knob, clamp_knob, print_plate]
explode     = 0;            // [0:1:40] exploded-view spacing
n_weights   = 3;            // [0:1:8] weights stacked in assembly
carrier_pos = 70;           // [20:1:115] carrier distance from pitch axis

/* [Pivot Hub] */
bearing_od   = 22.1;   // 608ZZ + clearance
bearing_w    = 7;
hub_od       = 34;
hub_len      = 16;
shaft_d      = 8.2;

/* [Arm] */
arm_len      = 130;    // from axis to tip
arm_w        = 24;
arm_t        = 8;
slot_w       = 4.5;    // M4 clamp bolt
slot_start   = 25;
slot_end     = 120;
tick_depth   = 0.6;

/* [Carrier Sled] */
sled_len     = 30;
sled_t       = 5;
guide_h      = 6;      // skirt wrapping arm sides
fit_clr      = 0.3;
stud_d       = 6;      // M6 stud / threaded rod
stud_len     = 75;

/* [Slotted Weight] */
weight_d     = 46;
weight_t     = 10;
wall         = 1.6;
lid_t        = 1.2;
wslot_clr    = 0.6;
key_d        = 4;      // registration peg between stacked weights
key_h        = 1.2;

/* [Material] */
lead_density = 11.34;  // g/cm^3
fill_factor  = 0.62;   // lead shot + epoxy packing fraction (1.0 = cast lead)

$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------------
//  Derived values / balance report
// ---------------------------------------------------------------------
wslot_w   = stud_d + wslot_clr;
cav_r     = weight_d/2 - wall;
cav_h     = weight_t - wall - lid_t;
cav_vol   = (PI*cav_r*cav_r - (wslot_w + 2*wall)*cav_r) * cav_h;  // mm^3, slot web removed
w_mass_g  = cav_vol/1000 * lead_density * fill_factor;
stack_m   = n_weights * w_mass_g;
moment    = stack_m * carrier_pos / 1000;   // g·m

echo(str("[Cycle14] lead per weight ≈ ", round(w_mass_g*10)/10, " g"));
echo(str("[Cycle14] stack (", n_weights, ") ≈ ", round(stack_m), " g @ ",
         carrier_pos, " mm -> ", round(moment*100)/100, " g·m"));
echo(str("[Cycle14] adjustable range: ",
         round(stack_m*slot_start/10)/100, " – ",
         round(stack_m*slot_end/10)/100, " g·m"));

// ---------------------------------------------------------------------
//  Pivot hub + graduated arm (single print)
// ---------------------------------------------------------------------
module pivot_hub() {
    difference() {
        rotate([0,90,0]) cylinder(d=hub_od, h=hub_len, center=true);
        // bearing pockets both sides
        for (s=[-1,1])
            translate([s*(hub_len/2 - bearing_w/2 + eps),0,0])
                rotate([0,90,0]) cylinder(d=bearing_od, h=bearing_w, center=true);
        rotate([0,90,0]) cylinder(d=shaft_d+4, h=hub_len+2, center=true);
    }
}

module scale_ticks() {
    for (y=[slot_start:5:slot_end]) {
        major = (y % 10 == 0);
        L = major ? 6 : 3.5;
        for (s=[-1,1])
            translate([s*(arm_w/2 - L/2 + eps), -y, arm_t/2 - tick_depth/2 + eps])
                cube([L, 0.8, tick_depth], center=true);
    }
}

module counterweight_arm() {
    difference() {
        hull() {
            translate([0,-hub_od/2+2,0]) cube([hub_len, 4, arm_t], center=true);
            translate([0,-arm_len+arm_w/2,0]) cylinder(d=arm_w, h=arm_t, center=true);
        }
        // clamp slot
        hull() for (y=[slot_start, slot_end])
            translate([0,-y,0]) cylinder(d=slot_w, h=arm_t+2, center=true);
        // underside relief channel for clamp-bolt head
        hull() for (y=[slot_start, slot_end])
            translate([0,-y,-arm_t/2]) cylinder(d=slot_w+4, h=2.4, center=true);
        scale_ticks();
        // keep hub bore clear
        rotate([0,90,0]) cylinder(d=bearing_od-1, h=hub_len+2, center=true);
    }
}

module hub_arm() {
    union() { pivot_hub(); counterweight_arm(); }
}

// ---------------------------------------------------------------------
//  Carrier sled: rides on arm, clamps via M4, carries M6 stud
// ---------------------------------------------------------------------
module carrier() {
    iw = arm_w + 2*fit_clr;
    difference() {
        union() {
            translate([0,0,sled_t/2]) cube([iw+2*3, sled_len, sled_t], center=true);
            // side guides
            for (s=[-1,1])
                translate([s*(iw/2+1.5), 0, -guide_h/2+eps])
                    cube([3, sled_len, guide_h], center=true);
            // stud boss
            cylinder(d=weight_d*0.45, h=sled_t+3);
            // index pointer
            for (s=[-1,1])
                translate([s*(iw/2+3), 0, sled_t/2])
                    rotate([0,0,s>0?0:180]) linear_extrude(sled_t, center=true)
                        polygon([[0,-2],[2.5,0],[0,2]]);
        }
        // M6 stud tapped/heat-set hole
        translate([0,0,1]) cylinder(d=stud_d-0.9, h=sled_t+5);
        // M4 clamp bolt hole (offset so stud & bolt are both on slot line)
        translate([0, sled_len/2-5, -1]) cylinder(d=4.3, h=sled_t+2);
        translate([0, sled_len/2-5, sled_t-2.5]) cylinder(d=8.2, h=3, $fn=6); // nut trap
        // registration socket matching weight key
        translate([0, -(weight_d/2 - 6), sled_t+3-key_h-0.2])
            cylinder(d=key_d+0.4, h=key_h+1);
    }
}

// ---------------------------------------------------------------------
//  Slotted weight shell (fill with lead shot + epoxy, close with lid)
// ---------------------------------------------------------------------
module slot_profile(h, w, extra=0) {
    // U-slot from center to +Y edge
    hull() {
        cylinder(d=w+extra, h=h);
        translate([0, weight_d, 0]) cylinder(d=w+extra, h=h);
    }
}

module slotted_weight() {
    difference() {
        union() {
            cylinder(d=weight_d, h=weight_t);
            // registration key (bottom-side socket mates on top of stack)
            translate([0, -(weight_d/2 - 6), weight_t-eps]) cylinder(d=key_d, h=key_h);
        }