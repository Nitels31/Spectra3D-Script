// Spectra3D Script - benchmark
//
//   bin\spsc.exe examples\bench.sps --time

def fib(n) {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

def loop_sum(n) {
    var total = 0;
    for i in range(n) {
        total = total + i * 2;
    }
    return total;
}

var started = 0;
var t = 0;
for i in range(20) {
    t = t + fib(22);
}
print("fib(22) x20 =", t);
print("loop_sum(200000) =", loop_sum(200000));
print("done", started);
