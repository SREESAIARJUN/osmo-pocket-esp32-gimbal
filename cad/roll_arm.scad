// Roll Axis Arm connecting Roll Motor to Pitch Motor
$fn = 60;

module roll_arm() {
    difference() {
        union() {
            // Roll motor hub
            cylinder(d=24, h=6);
            
            // Cantilever curved arm
            translate([0, -10, 3]) cube([38, 20, 6]);
            translate([34, -10, 3]) cube([6, 20, 28]);
            
            // Pitch motor housing cup
            translate([37, 0, 31]) rotate([90, 0, 0]) cylinder(d=24, h=6, center=true);
        }
        
        // Center motor shaft bore
        cylinder(d=6.0, h=20, center=true);
        
        // Roll motor mounting holes (4x M2 on 16mm PCD)
        for (a = [0, 90, 180, 270]) {
            rotate([0, 0, a]) translate([8, 0, -2]) cylinder(d=2.2, h=10);
        }
        
        // Pitch motor shaft bore & mounting holes
        translate([37, 0, 31]) rotate([90, 0, 0]) {
            cylinder(d=6.0, h=20, center=true);
            for (a = [45, 135, 225, 315]) {
                rotate([0, 0, a]) translate([8, 0, -10]) cylinder(d=2.2, h=20);
            }
        }
        
        // Internal wire raceway
        translate([20, 0, 4]) cube([30, 3, 4], center=true);
        translate([36, 0, 18]) cube([4, 3, 20], center=true);
    }
}

roll_arm();
