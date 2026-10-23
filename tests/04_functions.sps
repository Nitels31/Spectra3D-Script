// functions, recursion, default return value
def add(a, b) {
    return a + b;
}

def nothing() {
}

def fib(n) {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

print(add(2, 3));
print(nothing());
print(fib(12));
print(type(add));
