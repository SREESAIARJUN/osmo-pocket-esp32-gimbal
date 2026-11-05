# Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 11)

The design consists of the following parts:

- **Pitch hub:** a bearing boss on the pitch (X) axis, with a clamp-split arm that drops to the counterweight rail.
- **Rail:** a slotted rail running fore/aft (Y), with engraved scale ticks so you can repeat a balance setting.
- **Saddle carrier:** a carrier with a guide tongue that rides in the rail slot to stop the stack from rotating.
- **Lead plates:** slotted (U-slot) lead plates that slide sideways onto the clamp bolt. You can add or remove them without disassembling the stack.
- **Clamp:** a captive M5 bolt and a knurled thumb nut that lock the stack anywhere along the slot.

An `echo()` reports the lead mass and the balancing moment about the pitch axis.

```openscad
// =====================================================================
//  Pitch Axis Camera Cradle – Counterweight Balancing Mechanism
//  Slotted sliding lead weights  –  Cycle 11
//  Units: mm.  Pitch axis = X axis through origin.  +Z = up (camera side)
// =====================================================================

/* ---------------- user parameters ---------------- */
weight_pos      = 70;     // carrier position along rail slot (mm from hub)
n_plates        = 4;      // number of lead plates fitted
explode         = 0;      // >0 to explode stack for inspection

/* pitch hub */
shaft_d         = 8;      // pitch shaft
brg_od          = 22;     // 608 bearing
brg_w           = 7;
hub_od          = 32;
hub_len         = 24;

/* rail */
rail_drop       = 28;     // rail centreline below pitch axis
rail_len        = 130;
rail_w          = 22;
rail_t          = 6;
slot_w          = 5.5;    // M5 clearance
slot_start      = 25;
slot_end_margin = 10;
tick_pitch      = 5;

/* saddle carrier */
sad_len         = 18;     // along Y
sad_wall        = 3;
sad_h           = 8;      // below rail
tongue_h        = 3;      // key that rides in slot

/* lead plates */
plate_x         = 44;
plate_y         = 30;
plate_t         = 6;
plate_gap       = 0.4;    // clearance between plates
uslot_w         = 5.6;
chamfer         = 0.8;

/* hardware */
bolt_d          = 5;
nut_d           = 18;
nut_h           = 7;

lead_density    = 11.34e-3;   // g / mm^3
$fn             = 64;
eps             = 0.01;

/* ---------------- derived ---------------- */
slot_len   = rail_len - slot_start - slot_end_margin;
pos        = min(max(weight_pos, 0), slot_len);   // clamp to slot travel
carrier_y  = slot_start + pos;

// =====================================================================
//  Components
// =====================================================================

module pitch_hub() {
    difference() {
        union() {
            rotate([0,90,0]) cylinder(d=hub_od, h=hub_len, center=true);
            // drop arm to rail
            translate([-rail_w/2, -hub_od/4, -rail_drop - rail_t/2])
                cube([rail_w, hub_od/2, rail_drop + rail_t/2]);
        }
        // shaft + bearing seats both sides
        rotate([0,90,0]) {
            cylinder(d=shaft_d+1, h=hub_len+2, center=true);
            for (s=[-1,1]) translate([0,0,s*(hub_len/2 - brg_w/2 + eps)])
                cylinder(d=brg_od, h=brg_w, center=true);
        }
        // clamp split + pinch screw (locks arm angle on shaft collar)
        translate([-0.75, -hub_od, -hub_od/2 - 1]) cube([1.5, hub_od, hub_od/2]);
        translate([0, -hub_od/2 + 2, -hub_od/2 + 3]) rotate([0,90,0])
            cylinder(d=3.4, h=hub_od, center=true);
    }
}

module rail() {
    translate([0,0,-rail_drop])
    difference() {
        translate([-rail_w/2, -hub_od/4, -rail_t/2])
            cube([rail_w, rail_len + hub_od/4, rail_t]);
        // sliding slot
        hull() for (y=[slot_start, slot_start+slot_len])
            translate([0,y,0]) cylinder(d=slot_w, h=rail_t+2, center=true);
        // engraved scale ticks (top face, one side)
        for (i=[0:floor(slot_len/tick_pitch)]) {
            L = (i%2==0) ? 5 : 2.5;
            translate([rail_w/2 - L, slot_start + i*tick_pitch - 0.3, rail_t/2 - 0.5])
                cube([L+eps, 0.6, 1]);
        }
        // end-stop lightening hole
        translate([0, rail_len - slot_end_margin/2 + 2, 0])
            cylinder(d=3, h=rail_t+2, center=true);
    }
}

module saddle() {
    // U-channel that straddles the rail; tongue keys into slot
    iw = rail_w + 0.4;
    ow = iw + 2*sad_wall;
    difference() {
        union() {
            translate([-ow/2, -sad_len/2, -rail_t/2 - sad_h])
                cube([ow, sad_len, sad_h + rail_t + 1.5]);
        }
        // rail channel
        translate([-iw/2, -sad_len/2 - 1, -rail_t/2 - 0.2])
            cube([iw, sad_len + 2, rail_t + 0.4]);
        // bolt hole
        cylinder(d=bolt_d + 0.3, h=100, center=true);
        // captive hex for bolt head on top
        translate([0,0,rail_t/2 + 0.2]) cylinder(d=9.2, h=5, $fn=6);
        // index window to read scale
        translate([rail_w/2 - 6, -0.5, rail_t/2 - 1]) cube([6, 1, 5]);
    }
    // anti-rotation tongue (sits in rail slot, below top face)
    difference() {
        translate([0,0,-rail_t/2]) hull() for (y=[-sad_len/2+slot_w/2, sad_len/2-slot_w/2])
            translate([0,y,0]) cylinder(d=slot_w - 0.4, h=tongue_h);
        cylinder(d=bolt_d + 0.3, h=100, center=true);
    }
}

module lead_plate() {
    // slotted weight: U-slot opens toward -X so plates slide on/off bolt
    difference() {
        minkowski() {
            translate([-plate_x/2 + chamfer, -plate_y/2 + chamfer, chamfer])
                cube([plate_x - 2*chamfer, plate_y - 2*chamfer, plate_t - 2*chamfer]);
            sphere(r=chamfer, $fn=12);
        }
        // U-slot
        hull() {
            cylinder(d=uslot_w, h=3*plate_t, center=true);
            translate([-plate_x, 0, 0]) cylinder(d=uslot_w, h=3*plate_t, center=true);
        }
        // detent lip: slight neck retains plate against loose bolt
        // (lead deforms over bolt thread on install)
        // locating dimple on top / pip on bottom for stack registration
        for (x=[-plate_x/2 + 6, plate_x/2 - 6])
            translate([x, plate_y/4, plate_t - 0.6]) cylinder(d=3, h=1);
    }
    for (x=[-plate_x/2 + 6, plate_x/2 - 6])
        translate([x, plate_y/4, -0.5]) cylinder(d=2.6, h=0.5 + eps);
}

module thumb_nut() {
    difference() {
        union() {
            cylinder(d=nut_d - 2, h=nut_h);
            // knurl
            for (a=[0:30:359]) rotate(a) translate([nut_d/2 - 1.5, 0, 0])
                cylinder(d=3, h=nut_h, $fn=16);
        }
        cylinder(d=bolt_d - 0.8