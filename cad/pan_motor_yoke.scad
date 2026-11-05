// Pan Axis Brushless Motor Yoke & Slip Ring Housing
$fn = 60;

module pan_yoke() {
    difference() {
        union() {
            // Base cylinder mating to handle top
            cylinder(d=26, h=6);
            
            // Pan arm base
            translate([-12, -10, 6]) cube([24, 20, 14]);
            
            // Vertical riser arm to Roll motor
            translate([-12, 4, 20]) cube([24, 6, 28]);
            
            // Roll motor mounting ring
            translate([0, 7, 48]) rotate([0, 90, 0]) cylinder(d=24, h=6, center=true);
        }
        
        // Central cavity for 6.5mm capsule slip ring
        cylinder(d=7.0, h=30, center=true);
        
        // Pan motor mounting screw holes (4x M2 on 16mm PCD)
        for (a = [0, 90, 180, 270]) {
            rotate([0, 0, a]) translate([8, 0, -1]) cylinder(d=2.2, h=10);
        }
        
        // Roll motor shaft aperture and M2 mountings
        translate([0, 7, 48]) rotate([0, 90, 0]) {
            cylinder(d=6.0, h=20, center=true);
            for (a = [45, 135, 225, 315]) {
                rotate([0, 0, a]) translate([8, 0, -10]) cylinder(d=2.2, h=20);
            }
        }
        
        // Internal wiring channel for motor phases & encoder SPI
        translate([0, 6, 25]) cube([4, 4, 35], center=true);
    }
}

pan_yoke();
