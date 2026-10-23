// modules and imports
import "modules/util.sps" as u;
import "modules/util.sps";

print(u.square(5));
print(u.cube(3));
print(u.VERSION, u.LIMIT);
print(square(7));

// the module is loaded only once
print(u.square(2));
