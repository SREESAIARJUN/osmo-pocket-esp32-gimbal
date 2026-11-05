// DJI Osmo Pocket Style Ergonomic Handle Housing
// Holds ESP32-S3 mainboard, 2S 18350 Li-ion battery, 1.3" OLED, thumbstick, and bottom USB-C
$fn = 60;

module handle_body() {
    difference() {
        // Outer ergonomic rounded casing (32mm x 35mm x 128mm)
        hull() {
            translate([-14, -15, 0]) cylinder(r=3, h=128);
            translate([ 14, -15, 0]) cylinder(r=3, h=128);
            translate([ 14,  15, 0]) cylinder(r=3, h=128);
            translate([-14,  15, 0]) cylinder(r=3, h=128);
        }
        
        // Internal hollow cavity for battery, PCB, and wiring
        hull() {
            translate([-12, -13, 2]) cylinder(r=2, h=124);
            translate([ 12, -13, 2]) cylinder(r=2, h=124);
            translate([ 12,  13, 2]) cylinder(r=2, h=124);
            translate([-12,  13, 2]) cylinder(r=2, h=124);
        }
        
        // Front 1.3" OLED Display window (28mm x 28mm)
        translate([0, 16, 95]) cube([28, 6, 28], center=true);
        
        // Thumb Joystick aperture (14mm diameter)
        translate([0, 16, 60]) rotate([90, 0, 0]) cylinder(d=14, h=10, center=true);
        
        // Power and Record tactile buttons
        translate([-8, 16, 38]) rotate([90, 0, 0]) cylinder(d=7, h=10, center=true);
        translate([ 8, 16, 38]) rotate([90, 0, 0]) cylinder(d=7, h=10, center=true);
        
        // Bottom USB-C port cutout
        translate([0, 0, 0]) cube([10, 4, 10], center=true);
        
        // Bottom 1/4"-20 tripod thread insert socket
        translate([0, 0, 4]) cylinder(d=6.5, h=10, center=true);
        
        // Top Pan Motor mounting boss aperture
        translate([0, 0, 126]) cylinder(d=22, h=10, center=true);
    }
}

handle_body();
