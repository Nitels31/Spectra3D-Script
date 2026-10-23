// Spectra3D Script - the micro benchmark of the arithmetic in the loops.
var total = 0;
for i in range(3000000) {
    total = total + 1;
}
print("total", total);
