/* ==================================================
  Configuration
================================================== */

hole_distance_horizontal = 31;
hole_distance_vertical   = 31;
hole_diameter            = 3.8;
plate_thickness          = 10;
plate_width              = 44;
plate_height             = 40;
edge_radius              = 2;

/* ==================================================
  Code
================================================== */

complete_assembly();

/* ==================================================
  Modules
================================================== */

/*
 * Complete assembly
 * 
 * Includes everything needed to 
 *
 */
module complete_assembly () {
  difference() {
    plate();

    translate(
      [
        hole_distance_horizontal * 0.5,
        -hole_distance_vertical * 0.5,
        0
      ]
    )
    screw();
    translate(
      [
        -hole_distance_horizontal * 0.5,
         -hole_distance_vertical * 0.5,
         0
      ]
    )
    screw();

    translate(
      [
        hole_distance_horizontal * 0.5,
        hole_distance_vertical * 0.5,
        0
      ]
    )
    screw();
    translate(
      [
        -hole_distance_horizontal * 0.5,
         hole_distance_vertical * 0.5,
         0
      ]
    )
    screw();
  }
}

/*
 *
 * Baseplate
 *
 * Baseplate without holes
 *
 */
module plate() {
  
  difference() {

    minkowski() {
      cube(
        [
          plate_width - edge_radius * 2,
          plate_height - edge_radius * 2,
          plate_thickness
        ],
        center = true
      );
      sphere(
        r = edge_radius,
        $fn = 32
      );
    }
    
    // cut 
    translate([0,0, plate_thickness])
    cube(
      [
        plate_width * 2,
        plate_height * 2,
        plate_thickness
      ],
      center = true
    );
    
    translate([0,0, -plate_thickness])
    cube(
      [
        plate_width * 2,
        plate_height * 2,
        plate_thickness
      ],
      center = true
    );
  
  }
}

module screw() {
  cylinder(
    h = plate_thickness * 2,
    d = hole_diameter,
    center = true,
    $fn = 32
  );
}