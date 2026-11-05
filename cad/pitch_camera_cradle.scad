// Pitch Axis Camera Cradle with Lens Mount & IMU Pocket
$fn = 60;

module camera_cradle() {
    difference() {
        union() {
            // Main camera enclosure cube (26mm x 26mm x 28mm)
            cube([26, 26, 28], center=true);
            
            // Lens barrel bezel
            translate([0, 13, 0]) rotate([90, 0, 0]) cylinder(d=18, h=5, center=true);
            
            // Counterweight balance wing
            translate([0, -14, -6]) cube([20, 4, 8], center=true);
        }
        
        // Internal sensor pocket (holds 1/1.3" CMOS sensor board)
        translate([0, 0, 0]) cube([22, 22, 24], center=true);
        
        // Lens optical aperture (14mm diameter)
        translate([0, 14, 0]) rotate([90, 0, 0]) cylinder(d=14, h=10, center=true);
        
        // Direct IMU (ICM-42688-P) pocket on rear of sensor PCB for zero-lag attitude sensing
        translate([0, -11, 0]) cube([10, 3, 10], center=true);
        
        // Side motor coupling plate (M2 screw holes)
        translate([-13, 0, 0]) rotate([0, 90, 0]) {
            cylinder(d=5, h=5, center=true);
            for (a = [0, 90, 180, 270]) {
                rotate([0, 0, a]) translate([6, 0, 0]) cylinder(d=2.0, h=6, center=true);
            }
        }
    }
}

camera_cradle();
