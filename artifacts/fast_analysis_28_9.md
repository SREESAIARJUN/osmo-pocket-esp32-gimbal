# Pitch-Axis Counterweight Balancer with Sliding Lead Weights (Cycle 28)

This OpenSCAD design is a counterweight arm for the pitch axis of a camera cradle. Lead-filled saddle weights slide along a slot in the arm and lock in place with a bolt and a captive-nut clamp plate. The script also prints the estimated weight masses and balancing moment to the console.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer – Cycle 28
//  Slotted arm + sliding lead-filled saddle weights + captive-nut clamps
//  Pitch axis = Y axis through origin. Arm extends toward -X
//  (opposite the camera payload).
// =====================================================================

$fn = 64;

// ---------------- Global parameters ----------------
// Hub / pivot
hub_d          = 34;     // hub outer diameter
hub_w          = 14;     // hub width along pitch axis (Y)
bearing_d      = 22.1;   // 608 bearing pocket
bearing_w      = 7.2;
shaft_d        = 8.4;    // through-bore for 8 mm pitch shaft
hub_bolt_pcd   = 26;     // bolt circle to cradle side plate
hub_bolt_d     = 3.4;    // M3 clearance

// Arm
arm_len        = 160;    // pivot centre to arm tip
arm_w          = 22;     // width (Y)
arm_t          = 8;      // thickness (Z)
slot_w         = 4.4;    // M4 clearance slot
slot_start     = 30;     // slot begins this far from pivot
slot_end_margin= 10;     // solid material left at the tip
tick_pitch     = 10;     // scale graduation spacing
tick_depth     = 0.6;

// Sliding weight (saddle)
w_len          = 28;     // length along arm (X)
w_wall         = 3;      // shell wall
w_top_h        = 18;     // height of lead body above arm
w_skirt        = arm_t;  // side skirts hug arm sides
w_clear        = 0.35;   // sliding clearance
key_depth      = 1.6;    // locating rib into slot
fill_hole_d    = 6;      // lead pour / shot fill hole
lead_density   = 11.34;  // g/cm^3
pla_density    = 1.24;   // g/cm^3 (shell estimate)

// Clamp plate (under arm)
cp_t           = 5;
nut_af         = 7.2;    // M4 nut across flats (+clearance)
nut_h          = 3.4;

// Assembly: weight positions measured from pivot (mm, positive = along arm)
weight_positions = [70, 110];

// ---------------- Derived values ----------------
saddle_in_w  = arm_w + 2*w_clear;
saddle_out_w = saddle_in_w + 2*w_wall;
cav_len      = w_len - 2*w_wall;
cav_w        = saddle_out_w - 2*w_wall;
cav_h        = w_top_h - 2*w_wall;
cav_vol_cm3  = cav_len*cav_w*cav_h/1000;
lead_mass_g  = cav_vol_cm3*lead_density;
shell_vol_cm3= (w_len*saddle_out_w*(w_top_h+w_skirt)
               - w_len*saddle_in_w*w_skirt)/1000 - cav_vol_cm3;
weight_mass_g= lead_mass_g + shell_vol_cm3*pla_density;

// ---------------- Arm with hub ----------------
module counterweight_arm() {
    difference() {
        union() {
            // hub (axis along Y)
            rotate([90,0,0]) cylinder(d=hub_d, h=hub_w, center=true);
            // arm beam
            translate([-arm_len, -arm_w/2, -arm_t/2])
                cube([arm_len, arm_w, arm_t]);
            // rounded tip
            translate([-arm_len, 0, -arm_t/2])
                cylinder(d=arm_w, h=arm_t);
        }
        // shaft bore
        rotate([90,0,0]) cylinder(d=shaft_d, h=hub_w+2, center=true);
        // bearing pockets both sides
        for (s=[-1,1])
            translate([0, s*(hub_w/2 - bearing_w/2 + 0.01), 0])
                rotate([90,0,0]) cylinder(d=bearing_d, h=bearing_w, center=true);
        // cradle mounting bolts (parallel to the pitch axis)
        for (a=[45:90:359])
            rotate([0,a,0]) translate([hub_bolt_pcd/2,0,0])
                rotate([90,0,0]) cylinder(d=hub_bolt_d, h=hub_w+2, center=true);
        // sliding slot
        hull() {
            translate([-slot_start,0,-arm_t]) cylinder(d=slot_w, h=arm_t*2);
            translate([-(arm_len-slot_end_margin),0,-arm_t])
                cylinder(d=slot_w, h=arm_t*2);
        }
        // graduation ticks on the top face (long tick every 5th)
        for (x=[slot_start:tick_pitch:arm_len-slot_end_margin]) {
            long = ((x - slot_start) % (tick_pitch*5)) == 0;
            for (s=[-1,1])
                translate([-x-0.4, s>0 ? slot_w/2+1 : -(slot_w/2+1)-(long?6:3.5),
                           arm_t/2-tick_depth])
                    cube([0.8, long?6:3.5, tick_depth+0.1]);
        }
    }
}

// ---------------- Sliding lead weight (saddle) ----------------
// Origin: centre of the weight's contact face with the arm top (z = 0).
module lead_weight(show_lead=false) {
    difference() {
        union() {
            // lead body above arm
            translate([-w_len/2, -saddle_out_w/2, 0])
                cube([w_len, saddle_out_w, w_top_h]);
            // side skirts
            for (s=[-1,1])
                translate([-w_len/2, s>0 ? saddle_in_w/2 : -saddle_out_w/2, -w_skirt])
                    cube([w_len, w_wall, w_skirt]);
            // locating key rib that rides in slot
            translate([-w_len/2+2, -(slot_w-w_clear*2)/2, -key_depth])
                cube([w_len-4, slot_w-w_clear*2, key_depth]);
        }
        // sealed lead cavity
        translate([-cav_len/2, -cav_w/2, w_wall])
            cube([cav_len, cav_w, cav_h]);
        // fill hole (offset from bolt), plugged after filling
        translate([w_len/4, 0, w_top_h - w_wall - 0.1])
            cylinder(d=fill_hole_d, h=w_wall+0.2);
        // bolt tube clearance through the whole weight
        translate([0,0,-key_depth-1]) cylinder(d=slot_w, h=w_top_h+key_depth+2);
        // counterbore for socket head
        translate([0,0,w_top_h-4]) cylinder(d=8, h=5);
    }
    // bolt sleeve isolating the bolt from the lead cavity
    difference() {
        translate([0,0,w_wall]) cylinder(d=slot_w+3, h=cav_h);
        translate([0,0,w_wall-1]) cylinder(d=slot_w, h=cav_h+2);
    }
    if (show_lead)
        %translate([-cav_len/2, -cav_w/2, w_wall]) cube([cav_len, cav_w, cav_h*0.95]);
}

// ---------------- Clamp plate with captive M4 nut ----------------
// Origin: centre of the plate's top face (contacts arm underside).
module clamp_plate() {
    difference() {
        translate([-w_len/2+2, -arm_w/2+1, -cp_t])
            cube([w_len-4, arm_w-2, cp_t]);
        translate([0,0,-cp_t-1]) cylinder(d=slot_w, h=cp_t+2);
        translate([0,0,-cp_t-0.01])
            cylinder(d=nut_af/cos(30), h=nut_h, $fn=6);
    }
}

// ---------------- Fill-hole plug ----------------
module fill_plug() {
    cylinder(d=fill_hole_d-0.2, h=w_wall);
    translate([0,0,w_wall]) cylinder(d=fill_hole_d+3, h=1);
}

// ---------------- Full mechanism ----------------
module pitch_counterweight_assembly(positions=weight_positions, show_lead=true) {
    color("SteelBlue") counterweight_arm();
    for (p = positions) {
