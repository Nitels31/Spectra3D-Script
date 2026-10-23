// the inlining of the calls: the behaviour of the scripts does not change
// the arguments are evaluated once and in order
var order_log = [];

def side(v) {
    order_log = order_log + [v];
    return v;
}

def add(a, b) {
    return a + b;
}

def call_with_side_effects() {
    return add(side(1), side(2));
}

print(call_with_side_effects());
print(order_log);

// the local variables of the function do not leak into the caller
def with_a_local() {
    var hidden = 42;
    return hidden;
}

print(with_a_local());

// a return inside the function returns from the function
def early(x) {
    if x > 0 {
        return 1;
    }
    return 2;
}

var total = 0;
for i in range(5) {
    total = total + early(i - 2);
}
print(total);

// a return inside a loop of the function
def find_it(x) {
    for i in range(10) {
        if i == x {
            return i;
        }
    }
    return -1;
}

print(find_it(3), find_it(20));

// the recursion still works and it is not inlined
def fib(n) {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

print(fib(15));

// the globals of the function are not hidden by the locals of the caller
var g = 1;

def read_global() {
    return g;
}

def caller() {
    var g = 100;
    return read_global() + g;
}

print(caller(), g);

// a struct that is given to a function is not copied
struct Vec2 {
    float x = 0;
    float y = 0;
}

def bump(v) {
    v.x = v.x + 1.0;
    return v;
}

var v = Vec2();
var w = bump(v);
print(v.x, w.x);

// the function is inlined in a loop of another function
def total_of(n) {
    var sum = 0;
    for i in range(n) {
        sum = sum + add(i, 1);
    }
    return sum;
}

print(total_of(10));

// the function is still callable
print(add(2, 3), with_a_local());
