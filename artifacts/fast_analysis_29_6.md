# Pitch-Axis Camera Cradle Counterweight Mechanism (Cycle 29)

This is a parametric OpenSCAD model of a counterweight balancing mechanism for a camera cradle's pitch axis. It has four main parts:

- **Pitch hub clamp.** A split collar clamps onto the pitch shaft.
- **Slotted counterweight rail.** A plate with a long through-slot, with a T-track guide on the top face.
- **Sliding carriage.** It has a key that rides in the rail slot, a fixed M5 stud, and a locking screw.
- **Lead weights.** These are slabs with U-slots, so you can add or remove them on the stud without taking the carriage apart.

The script also reports the total mass of the lead and the balancing torque at the chosen position in the console. These are rough estimates, so confirm the final balance on the real rig.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle — Counterweight Balancing Mechanism
//  Slotted sliding lead weights   |   Design Cycle 29
//  Units: mm
// =====================================================================

$fn = 64;

// ---------------- Global parameters ----------------
// Pitch axis
pitch_shaft_d      = 12;     // pitch shaft diameter
hub_od             = 30;
hub_len            = 18;
hub_clamp_gap      = 1.2;    // split-clamp slot width
hub_screw_d        = 3.4;    // M3 clearance

// Rail (counterweight arm)
rail_len           = 140;    // from axis centre to rail tip
rail_w             = 24;
rail_t             = 6;
rail_slot_w        = 5.5;    // M5 clearance slot
rail_slot_start    = 30;     // slot begins this far from the axis
rail_slot_end      = 130;
rail_drop          = 0;      // vertical offset of the rail below the axis (CoG tuning)

// Carriage
carr_len           = 30;
carr_w             = 30;
carr_t             = 5;
carr_key_w         = 5.2;    // key that rides in the rail slot
carr_key_h         = 3;
stud_d             = 5;      // M5 stud for the weights
stud_len           = 40;
lock_screw_d       = 3;      // M3 grub/thumb screw locking onto the rail

// Lead weights
lead_len           = 28;
lead_w             = 28;
lead_t             = 6;
lead_slot_w        = 5.6;    // U-slot for the M5 stud
lead_slot_depth    = 16;     // depth from the open edge
lead_chamfer       = 0.8;
lead_density       = 0.01134; // g/mm^3 (11.34 g/cm^3)

// Thumb nut
nut_od             = 18;
nut_h              = 7;

// ---------------- Assembly controls ----------------
weight_position    = 85;     // carriage centre distance from the pitch axis (mm)
n_weights          = 3;      // number of lead slabs fitted
explode            = 0;      // 0 = assembled, e.g. 10 = exploded view
show_shaft         = true;

// =====================================================================
//  Mass / moment reporting
// =====================================================================
function lead_volume() =
    lead_len*lead_w*lead_t
    - (lead_slot_w*lead_slot_depth + PI*pow(lead_slot_w/2,2)/2)*lead_t;

function lead_mass_g() = lead_volume()*lead_density;

module report() {
    m  = lead_mass_g();
    mt = m*n_weights;
    echo(str("Cycle 29 | Lead slab mass: ", round(m*10)/10, " g"));
    echo(str("Cycle 29 | Total lead mass: ", round(mt*10)/10, " g (", n_weights, " slabs)"));
    echo(str("Cycle 29 | Counter-torque at ", weight_position, " mm: ",
             round(mt*weight_position/10)/100, " kg·mm  (",
             round(mt/1000*9.81*weight_position)/1000, " N·m)"));
}

// =====================================================================
//  Components
// =====================================================================

// Split clamp hub on the pitch shaft
module pitch_hub() {
    difference() {
        union() {
            rotate([0,90,0]) cylinder(d=hub_od, h=hub_len, center=true);
            // Clamp ears
            translate([0, 0, hub_od/2 - 2])
                cube([hub_len, 14, 10], center=true);
        }
        // Shaft bore
        rotate([0,90,0]) cylinder(d=pitch_shaft_d+0.2, h=hub_len+2, center=true);
        // Clamp slot
        translate([0, 0, hub_od/2])
            cube([hub_len+2, hub_clamp_gap, hub_od], center=true);
        // Clamp screw
        translate([0, 0, hub_od/2 - 1])
            rotate([90,0,0]) cylinder(d=hub_screw_d, h=20, center=true);
        // Nut trap
        translate([0, -7, hub_od/2 - 1])
            rotate([90,0,0]) cylinder(d=6.4, h=3, $fn=6, center=true);
    }
}

// Slotted rail extending along +Y from the axis
module counterweight_rail() {
    difference() {
        union() {
            // Arm plate
            translate([-rail_w/2, hub_od/2 - 4, -rail_t/2 - rail_drop])
                cube([rail_w, rail_len - hub_od/2 + 4, rail_t]);
            // Web joining the plate to the hub
            hull() {
                rotate([0,90,0]) cylinder(d=hub_od, h=rail_w*0.6, center=true);
                translate([-rail_w*0.3, hub_od/2, -rail_t/2 - rail_drop])
                    cube([rail_w*0.6, 10, rail_t]);
            }
        }
        // Main slot
        hull() {
            translate([0, rail_slot_start, -rail_drop])
                cylinder(d=rail_slot_w, h=rail_t+2, center=true);
            translate([0, rail_slot_end, -rail_drop])
                cylinder(d=rail_slot_w, h=rail_t+2, center=true);
        }
        // Index marks every 10 mm (engraved on the top face)
        for (y=[rail_slot_start:10:rail_slot_end])
            translate([rail_w/2 - 4, y - 0.4, rail_t/2 - rail_drop - 0.6])
                cube([5, 0.8, 1]);
        // Clear the shaft bore through the web
        rotate([0,90,0]) cylinder(d=pitch_shaft_d+0.2, h=rail_w+2, center=true);
    }
}

// Carriage that slides along the slot
module carriage() {
    difference() {
        union() {
            // Base plate (sits on the rail's top face)
            translate([-carr_w/2, -carr_len/2, 0])
                cube([carr_w, carr_len, carr_t]);
            // Guide key (rides in the rail slot)
            translate([-carr_key_w/2 + 0.15, -carr_len/2 + 3, -carr_key_h])
                cube([carr_key_w - 0.3, carr_len - 6, carr_key_h]);
            // Side cheeks straddle the rail
            for (s=[-1,1])
                translate([s*(rail_w/2 + 1.5) - 1.5, -carr_len/2, -rail_t])
                    cube([3, carr_len, rail_t]);
        }
        // Lock screw through the side cheek
        translate([rail_w/2, 0, -rail_t/2])
            rotate([0,90,0]) cylinder(d=lock_screw_d - 0.4, h=10, center=true);
    }
    // Stud (modelled; in practice an M5 bolt pressed through from below)
    translate([0,0,carr_t]) cylinder(d=stud_d, h=stud_len);
}

// Lead slab with a U-slot (cast in a mould made from the negative of this)
module lead_weight() {
    difference() {
        minkowski() {
            translate([-lead_w/2 + lead_chamfer, -lead_len/2 + lead_chamfer, lead_chamfer])
                cube([lead_w - 2*lead_chamfer, lead_len - 2*lead_chamfer, lead_t - 2*lead_chamfer]);
            sphere(r=lead_chamfer, $fn=12);
        }
        // U-slot open toward +Y (lets the slab slide on without removing the nut)
        hull() {
            translate([0, 0, -1]) cylinder(d=lead_slot_w, h=lead_t+2);
            translate([0, lead_len, -1]) cylinder(d=lead_slot_w, h=lead_t+2);
        }
        // Stack-alignment dimples
        for (s=[-1,1])
            translate([s*lead_w/3, -lead_len/4, lead_t - 0.8])
                sphere(d=3, $fn=16);
    }
}

// Knurled thumb nut
module thumb_nut() {
    difference() {
        union() {
            cylinder(d=nut_od*0.6, h=nut_h);
            for (a=[0:30:330])
                rotate([0,0,a]) translate([nut_od/2 - 1.5, 0, 0])
                    cylinder(d=3, h=nut_h*0.6, $fn=16);
            cylinder(d=nut_od - 3, h=nut_h*0.6);
        }
        translate([0,0,-1]) cylinder(d=stud_d - 0.6, h=nut_h + 2); // tap-drill size for M5
    }
}

// =====================================================================
//  Top-level assembly module
// =====================================================================
module pitch_counterweight_mechanism(pos = weight_position,
                                     n   = n_weights,
                                     ex  = explode) {
    pos_c = max(rail_slot_start + carr_len/2 - 5,
                min(pos, rail_slot_end - carr_len/2 + 5));
    if (pos_c != pos)
        echo(str("Cycle 29 WARNING: position clamped to ", pos_c, " mm"));

    color("SteelBlue") pitch_hub();
    color("Silver")    counterweight_rail();

    transl