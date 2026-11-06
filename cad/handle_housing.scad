// 3-Axis Pocket Gimbal Ergonomic Handle Housing (Revision 6.0 / 150)
$fn = 60;
fillet_radius = 3.30;
wall_thickness = 2.12;

module handle_body() {
    difference() {
        hull() {
            translate([-14, -15, 0]) cylinder(r=fillet_radius, h=128);
            translate([ 14, -15, 0]) cylinder(r=fillet_radius, h=128);
            translate([ 14,  15, 0]) cylinder(r=fillet_radius, h=128);
            translate([-14,  15, 0]) cylinder(r=fillet_radius, h=128);
        }
        hull() {
            translate([-12, -13, 2]) cylinder(r=fillet_radius - 0.5, h=124);
            translate([ 12, -13, 2]) cylinder(r=fillet_radius - 0.5, h=124);
            translate([ 12,  13, 2]) cylinder(r=fillet_radius - 0.5, h=124);
            translate([-12,  13, 2]) cylinder(r=fillet_radius - 0.5, h=124);
        }
        // 1.3" OLED Display bezel
        translate([0, 16, 95]) cube([28, 6, 28], center=true);
        // Analog Thumbstick aperture
        translate([0, 16, 60]) rotate([90, 0, 0]) cylinder(d=14, h=10, center=true);
        // Tactile buttons
        translate([-8, 16, 38]) rotate([90, 0, 0]) cylinder(d=7, h=10, center=true);
        translate([ 8, 16, 38]) rotate([90, 0, 0]) cylinder(d=7, h=10, center=true);
        // Bottom USB-C port cutout & tripod thread
        translate([0, 0, 0]) cube([10, 4, 10], center=true);
        translate([0, 0, 4]) cylinder(d=6.5, h=10, center=true);
        // Top Pan Motor mounting boss
        translate([0, 0, 126]) cylinder(d=22, h=10, center=true);
    }
}
handle_body();
