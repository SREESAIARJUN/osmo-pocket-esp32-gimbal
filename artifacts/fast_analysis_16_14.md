# Pitch-Axis Counterweight Balancer: Slotted Sliding Lead Weights (Cycle 16)

This is a parametric OpenSCAD design. A slotted rail is clamped to the pitch shaft. A sliding carriage rides on the rail and carries a stack of U-slotted lead plates. One thumb-nut bolt locks the carriage to the rail and holds the plates at the same time. You can add or remove plates sideways without taking the bolt out.

```openscad
// =====================================================================
//  Pitch-Axis Camera Cradle Counterweight Balancer — Cycle 16
//  Slotted rail + sliding carriage + U-slotted stackable lead plates
// =====================================================================
//  Coordinate convention:
//    Pitch axis  = X axis through origin
//    Rail        = extends along +Y (away from camera side)
//    Up          = +Z
// =====================================================================

/* ---------------- Render selection ---------------- */
part      = "assembly";  // "assembly" | "rail" | "carriage" | "clamp" | "plate" | "plate_pattern" | "print_layout"
cw_pos    = 85;          // carriage distance from pitch axis [mm] (assembly view)
n_plates  = 3;           // number of lead plates installed (assembly view)

$fn = 64;
eps = 0.01;

/* ---------------- Pitch shaft / hub ---------------- */
shaft_d      = 8.0;      // pitch shaft diameter
shaft_flat   = 0.8;      // D-flat depth (0 = round)
hub_d        = 30;
hub_len      = 24;       // along X
clamp_slit   = 1.5;
m3_clear     = 3.4;
m3_head_d    = 6.0;

/* ---------------- Rail ---------------- */
rail_len     = 150;      // from axis to tip
rail_w       = 24;
rail_t       = 6;
slot_w       = 4.5;      // M4 clearance
slot_start   = hub_d/2 + 12;
slot_end     = rail_len - 8;
tick_pitch   = 5;        // graduation spacing [mm]
stop_h       = 4;        // tip end-stop height

/* ---------------- Carriage ---------------- */
car_l        = 34;       // along Y
car_w        = rail_w + 2*3;   // skirts hug rail
car_t        = 4;
skirt_drop   = rail_t - 0.6;   // skirt depth (leaves clamp gap)
fit_clear    = 0.3;
spig_s       = 10;       // square spigot that keys the plates
spig_h_max   = 6;        // max plates the spigot supports
m4_clear     = 4.4;

/* ---------------- Clamp plate (under rail) ---------------- */
clamp_t      = 4;
m4_nut_af    = 7.0;      // across flats
m4_nut_h     = 3.2;

/* ---------------- Lead plates ---------------- */
plate_l      = 30;       // along Y
plate_w      = 44;       // along X
plate_t      = 5;
plate_r      = 3;        // corner radius
uslot_w      = spig_s + 0.4;
lead_rho     = 11.34;    // g/cm^3

spig_h = spig_h_max * plate_t + 1;

/* ---------------- Mass / moment report ---------------- */
plate_vol_mm3 = (plate_l*plate_w - uslot_w*(plate_w/2)) * plate_t;
plate_mass_g  = plate_vol_mm3/1000 * lead_rho;
echo(str("Cycle16: lead plate mass ≈ ", round(plate_mass_g*10)/10, " g"));
echo(str("Cycle16: stack (", n_plates, ") = ", round(n_plates*plate_mass_g), " g, moment @ ",
         cw_pos, " mm = ", round(n_plates*plate_mass_g*cw_pos), " g·mm"));
echo(str("Cycle16: carriage travel ", slot_start + car_l/2, " … ", slot_end - car_l/2, " mm"));

// =====================================================================
//  Helpers
// =====================================================================
module rounded_rect(l, w, h, r) {
    // centered in XY, z from 0..h
    hull() for (sx=[-1,1], sy=[-1,1])
        translate([sx*(w/2-r), sy*(l/2-r), 0]) cylinder(r=r, h=h);
}

module hex_nut_trap(af, h) {
    cylinder(d=af/cos(30), h=h, $fn=6);
}

// =====================================================================
//  Rail + shaft-clamp hub
// =====================================================================
module cw_rail() {
    difference() {
        union() {
            // hub
            rotate([0,90,0]) cylinder(d=hub_d, h=hub_len, center=true);
            // clamp ears on -Y side
            translate([-hub_len/2, -hub_d/2-8, -8]) cube([hub_len, 10, 16]);
            // rail bar
            translate([-rail_w/2, 0, -rail_t/2]) cube([rail_w, rail_len, rail_t]);
            // tip end stop (carriage cannot slide off)
            translate([-rail_w/2, rail_len-4, rail_t/2-eps]) cube([rail_w, 4, stop_h]);
        }
        // shaft bore with D-flat
        rotate([0,90,0]) difference() {
            cylinder(d=shaft_d+0.2, h=hub_len+2, center=true);
            if (shaft_flat > 0)
                translate([shaft_d/2 - shaft_flat + 0.1, -shaft_d, -hub_len])
                    cube([shaft_d, 2*shaft_d, 2*hub_len]);
