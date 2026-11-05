# Pitch-Axis Counterweight Balancer with Slotted Sliding Lead Weights (Cycle 15)

This is a parametric OpenSCAD design. It includes five parts:

- **Pitch hub:** a bearing hub on the pitch axis.
- **Counterweight arm:** has a graduated slide slot.
- **Keyed carriage:** rides in the slot so it slides without rotating, and is locked in place with a thumb nut.
- **Slotted lead weights:** U-slotted slabs, like a laboratory slotted-mass set. They can be added or removed without taking the bolt out. Slot directions alternate so the stack cannot slip off.
- **Casting mold:** a two-part mold for pouring the lead weights.

The script also estimates the weight mass and the balancing moment in the console.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle - Counterweight Balancing Mechanism
//  Slotted sliding lead weights  |  Design Cycle 15
//  Units: mm.  Pitch axis = X axis through origin.
// =====================================================================

/* [Render] */
part          = "assembly"; // [assembly, arm, carriage, weight, thumbnut, mold_base, mold_lid]
weight_pos    = 40;    // slide position along slot (0 = nearest pivot)
n_weights     = 3;     // number of lead weights fitted
alt_slots     = true;  // alternate slot direction for retention
explode       = 0;     // exploded-view spacing
show_cradle   = true;

/* [Pitch hub] */
hub_od        = 32;
hub_len       = 14;
bearing_od    = 22.05; // 608 bearing + clearance
bearing_w     = 7;
axle_d        = 8.2;

/* [Arm] */
arm_len       = 120;   // pivot centre to arm tip
arm_w         = 24;
arm_t         = 8;
slot_w        = 6.4;   // M6 clearance
slot_start    = 28;    // slot begins this far below pivot
slot_end_mgn  = 10;
grad_step     = 5;     // graduation spacing

/* [Carriage] */
car_w         = 30;
car_h         = 30;
car_t         = 6;
key_depth     = 3;     // tongue engaging slot
bolt_d        = 6;
bolt_len      = 50;

/* [Lead weights] */
wt_w          = 40;
wt_h          = 40;
wt_t          = 8;
wt_slot_w     = 6.8;   // slot width in weight
wt_chamfer    = 1;
wt_gap        = 0.4;
lead_density  = 11.34; // g/cm^3

/* [Thumb nut] */
nut_od        = 22;
nut_t         = 10;
knurls        = 18;

/* [Mold] */
mold_wall     = 8;
mold_draft    = 1.5;   // degrees

$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------------
// Derived values
// ---------------------------------------------------------------------
slot_len   = arm_len - slot_start - slot_end_mgn;
pos_c      = max(0, min(weight_pos, slot_len));
bolt_z     = -(slot_start + pos_c);

wt_area    = wt_w*wt_h - (wt_slot_w*(wt_h/2) + PI*pow(wt_slot_w/2,2)/2);
wt_mass_g  = wt_area * wt_t / 1000 * lead_density;
stack_g    = wt_mass_g * n_weights;
moment     = stack_g * abs(bolt_z) / 1000; // g*m

echo(str("Cycle 15 | single weight = ", round(wt_mass_g*10)/10, " g"));
echo(str("Stack (", n_weights, ") = ", round(stack_g), " g @ ",
         abs(bolt_z), " mm  -> moment = ", round(moment*100)/100, " g*m"));

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
module y_cyl(d, h, center=false) { rotate([-90,0,0]) cylinder(d=d, h=h, center=center); }
module x_cyl(d, h, center=false) { rotate([0,90,0])  cylinder(d=d, h=h, center=center); }

module slot_y(len, w, depth) {          // slot running along -Z, cut along Y
    hull() {
        translate([0,0,0])    y_cyl(w, depth, center=true);
        translate([0,0,-len]) y_cyl(w, depth, center=true);
    }
}

// ---------------------------------------------------------------------
// Pitch hub + counterweight arm
// ---------------------------------------------------------------------
module counterweight_arm() {
    difference() {
        union() {
            x_cyl(hub_od, hub_len, center=true);
            hull() {
                translate([-arm_w/2,-arm_t/2,-hub_od/2]) cube([arm_w, arm_t, hub_od/2]);
                rotate([0,90,0]) cylinder(d=hub_od*0.8, h=arm_w, center=true);
            }
            translate([-arm_w/2,-arm_t/2,-arm_len]) cube([arm_w, arm_t, arm_len]);
        }
        // bearing pockets both sides + axle bore
        x_cyl(axle_d, hub_len+2, center=true);
        for (s=[-1,1])
            translate([s*(hub_len/2 - bearing_w/2 + eps),0,0])
                x_cyl(bearing_od, bearing_w, center=true);
        // slide slot
        translate([0,0,-slot_start]) slot_y(slot_len, slot_w, arm_t+2);
        // graduation notches (edge, +X side)
        for (i=[0:grad_step:slot_len]) {
            mj = (i % (grad_step*2) == 0);
            translate([arm_w/2 - (mj?3:1.8), arm_t/2-0.6, -(slot_start+i)-0.3])
                cube([5, 1, 0.6]);
        }
    }
}

// ---------------------------------------------------------------------
// Keyed carriage (rides on +Y face of arm)
// ---------------------------------------------------------------------
module carriage() {
    difference() {
        union() {
            translate([-car_w/2, 0, -car_h/2]) cube([car_w, car_t, car_h]);
            // key tongue engaging slot (prevents rotation)
            hull() for (z=[-car_h/2+slot_w/2+2, car_h/2-slot_w/2-2])
                translate([0,-key_depth,z]) y_cyl(slot_w-0.3, key_depth+eps);
            // pointer for graduations
            translate([car_w/2-eps,0,-0.5]) cube([3, car_t, 1]);
        }
        translate([0,-key_depth-1,0]) y_cyl(bolt_d+0.3, car_t+key_depth+2);
        // captive hex head pocket on arm side
        translate([0,-key_depth-1,0])
            rotate([-90,0,0]) cylinder(d=11.5, h=4.2, $fn=6);
    }
}

// ---------------------------------------------------------------------
// Slotted lead weight (U-slot from centre to one edge)
// ---------------------------------------------------------------------
module weight_2d() {
    difference() {
        offset(delta=wt_chamfer, chamfer=true)
            square([wt_w-2*wt_chamfer, wt_h-2*wt_chamfer], center=true);
        hull() {
            circle(d=wt_slot_w);
            translate([-wt_sl