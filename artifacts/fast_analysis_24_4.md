# Pitch-Axis Camera Cradle with Sliding Counterweights (OpenSCAD)

The model below is parametric. You can render a full assembly, print individual parts, or animate the weights sliding along the rail.

## How It Works

- **Pitch axis:** Two side cheeks carry Ø22 mm bearing bosses on a common axis.
- **Camera plate:** A slotted plate takes a 1/4"-20 camera screw, so you can set the camera's fore/aft position.
- **Counterweight rail:** A flat bar with a long centre slot hangs below the axis and sweeps rearward. Lowering the system's centre of gravity toward the pitch axis is what balances the cradle.
- **Lead weights:** Each weight is a lead slab with an open U-slot, so you can add or remove it without taking the rail off. The weights stack on the rail. A clamp plate and an M5 thumbscrew pass through the rail slot to lock the stack at any position.
- **Balance estimate:** An `echo` reports the total weight mass (lead ≈ 11.34 g/cm³) and the moment arm about the pitch axis.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights  |  Design Cycle 24
//  Units: mm
// =====================================================================

/* [Render] */
part          = "assembly"; // [assembly, cradle, rail, weight, clamp, knob]
weight_pos    = 0.55;       // 0..1 position of weight stack along rail slot
num_weights   = 3;          // number of stacked lead slabs
explode       = 0;          // exploded-view gap (mm)

/* [Cradle] */
cradle_w      = 90;    // inside width between cheeks (X)
cradle_d      = 60;    // depth (Y)
cheek_h       = 70;    // cheek height
wall          = 6;     // general wall thickness
plate_t       = 8;     // camera plate thickness
axis_z        = 45;    // pitch axis height above cradle floor
bearing_od    = 22;    // 608 bearing OD
bearing_w     = 7;
shaft_d       = 8;
boss_d        = 34;

/* [Camera mount] */
cam_slot_len  = 40;
cam_screw_d   = 6.6;   // 1/4"-20 clearance

/* [Counterweight rail] */
rail_drop     = 25;    // how far below floor the rail starts
rail_len      = 130;   // rail length (rearward, -Y)
rail_w        = 30;
rail_t        = 6;
rail_slot_w   = 5.5;   // M5 clearance
rail_slot_margin = 12;
rail_angle    = 20;    // downward sweep angle (deg)

/* [Lead weights] */
wt_len        = 28;    // along rail
wt_w          = 50;    // across rail
wt_t          = 6;     // thickness of each slab
wt_slot_w     = 5.8;   // U-slot width (passes the M5 screw)
wt_slot_depth = 32;    // open slot depth from the front edge
wt_chamfer    = 1.0;
lead_density  = 11.34; // g/cm^3

/* [Clamp] */
clamp_t       = 4;
knob_d        = 22;
knob_h        = 10;
screw_d       = 5;

$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------------
//  Derived values
// ---------------------------------------------------------------------
slot_travel = rail_len - 2*rail_slot_margin - wt_len;
wt_center_s = rail_slot_margin + wt_len/2 + weight_pos*slot_travel; // dist along rail

// Mass estimate (U-slot removed)
wt_vol_cm3  = (wt_len*wt_w*wt_t - wt_slot_w*wt_slot_depth*wt_t)/1000;
wt_mass_g   = wt_vol_cm3 * lead_density;
echo(str("Cycle 24 | per-weight mass ≈ ", round(wt_mass_g), " g, stack ≈ ",
         round(wt_mass_g*num_weights), " g"));

// Moment arm about pitch axis (rail in Y-Z plane)
rail_origin = [0, -cradle_d/2, -rail_drop];
arm_y = rail_origin[1] - wt_center_s*cos(rail_angle);
arm_z = rail_origin[2] - wt_center_s*sin(rail_angle) - axis_z;
echo(str("Weight CG offset from pitch axis: Y=", round(arm_y), " Z=", round(arm_z),
         "  | moment ≈ ", round(wt_mass_g*num_weights*abs(arm_y)/1000), " g·m"));

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
module slot2d(len, w) {
    hull() { translate([-len/2,0]) circle(d=w); translate([len/2,0]) circle(d=w); }
}

module rounded_box(size, r=2) {
    hull() for (x=[r,size[0]-r], y=[r,size[1]-r])
        translate([x,y,0]) cylinder(r=r, h=size[2]);
}

// ---------------------------------------------------------------------
//  Cradle (U-bracket with bearing bosses + slotted camera plate)
// ---------------------------------------------------------------------
module cradle() {
    outer_w = cradle_w + 2*wall;
    difference() {
        union() {
            // floor / camera plate
            translate([-outer_w/2, -cradle_d/2, 0])
                rounded_box([outer_w, cradle_d, plate_t], 3);
            // cheeks
            for (s=[-1,1])
                translate([s*(cradle_w/2 + wall/2) - wall/2, -cradle_d/2, 0])
                    rounded_box([wall, cradle_d, cheek_h], 2);
            // bearing bosses
            for (s=[-1,1])
                translate([s*(cradle_w/2 + wall), 0, axis_z])
                    rotate([0, s*90, 0]) cylinder(d=boss_d, h=bearing_w+2);
            // rail hanger tabs (rear underside)
            for (s=[-1,1])
                translate([s*(rail_w/2 + wall/2) - wall/2, -cradle_d/2, -rail_drop])
                    cube([wall, 18, rail_drop + eps]);
        }
        // bearing pockets + shaft bore
        for (s=[-1,1])
            translate([s*(cradle_w/2 + wall + bearing_w + 2 + eps), 0, axis_z])
                rotate([0, -s*90, 0]) cylinder(d=bearing_od, h=bearing_w + eps);
        translate([0,0,axis_z]) rotate([0,90,0])
            cylinder(d=shaft_d+1, h=outer_w + 40, center=true);
        // camera 1/4" slot
        translate([0,0,-eps]) linear_extrude(plate_t + 2*eps)
            rotate(90) slot2d(cam_slot_len, cam_screw_d);
        // counterbore for screw head
        translate([0,0,-eps]) linear_extrude(3)
            rotate(90) slot2d(cam_slot_len, 12);
        // hanger pin hole
        translate([0, -cradle_d/2 + 9, -rail_drop + 6]) rotate([0,90,0])
            cylinder(d=screw_d+0.4, h=rail_w + 4*wall, center=true);
        // lightening pockets in cheeks
        for (s=[-1,1])
            translate([s*(cradle_w/2 + wall/2), 0, axis_z/2 + plate_t/2])
                rotate([0,90,0]) cylinder(d=14, h=wall+2, center=true);
    }
}

// ---------------------------------------------------------------------
//  Counterweight rail (slotted bar)
// ---------------------------------------------------------------------
// Local frame: rail runs along -Y from origin, top face at z=0
module rail() {
    difference() {
        union() {
            translate([-rail_w/2, -rail_len, -rail_t]) rounded_box([rail_w, rail_len, rail_t], 3);
            // pivot lug
            translate([0, -9, 6 - rail_t]) rotate([0,90,0])
                cylinder(d=14, h=rail_w, center=true);
            translate([-rail_w/2, -16, -rail_t]) cube([rail_w, 16, 12]);
            // end stop
            translate([-rail_w/2, -rail_len, 0]) cube([rail_w, 4, 4]);
        }
        // central adjustment slot
        translate([0, -rail_len/2 - (rail_slot_margin - 16)/2, -rail_t - eps])
            linear_extrude(rail_t + 2*eps)
                rotate(90) slot2d(rail_len - rail_slot_margin - 16 - rail_slot_w, rail_slot_w);
        // pivot hole
        translate([0, -9, 6 - rail_t]) rotate([0,90,0])
            cylinder(d=screw_d+0.4, h=rail_w+2, center=true);
        // graduation notches every 10 mm
        for (i=[0:10:rail_len-30])
            translate([rail_w/2 - 1, -20 - i, -1]) cube([1.5, 0.8, 1.5]);
    }
}

// ---------------------------------------------------------------------
//  Slotted lead weight (open U-slot for tool-less add/remove)
// ---------------------------------------------------------------------
module lead_weight() {
    difference() {
        hull() {
            translate([-wt_w/2 + wt_chamfer, -wt_len/2 + wt_chamfer, 0])
                cube([wt_w - 2*wt_chamfer, wt_len - 2*wt_chamfer, wt_t]);
            translate([-wt_w/2, -wt_len/2, wt_chamfer])
                cube([wt_w, wt_len, wt_t - 2*wt_chamfer]);
        }
        // open U-slot entering from +X edge, ending at centre
        translate([0, 0, -eps]) linear_extrude(wt_t + 2*eps) {
            translate([0, -wt_slot_w/2]) square([wt_w, wt_slot_w]);
            circle(d=wt_slot_w);
        }
        // registration dimples (top) for stacking alignment
        for (x=[-wt_w/2+8, wt_w/2-8 - 0], y=[-wt_len/2+7