// Spectra3D Script - feature demo

def fib(n) {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

def make_counter(start) {
    var count = start;
    def increment() {
        count = count + 1;
        return count;
    }
    return increment;
}

def greet(name) {
    return "Hello, " + name + "!";
}

print("--- basics ---");
print("Hello World!");
print(1 + 2, 10 / 4, int(7 / 2), 7 % 3, 2 * 3.5);
print(true and false, true or false, not true);

print("--- functions ---");
print("fib(15) =", fib(15));
print(greet("Spectra3D"));

print("--- closures ---");
var counter = make_counter(10);
print(counter());
print(counter());
print(counter());

print("--- lists ---");
var numbers = [1, 2, 3, 4, 5];
var total = 0;
for n in numbers {
    total = total + n;
}
print("numbers =", numbers);
print("total =", total);
numbers[0] = 100;
print("numbers[0] =", numbers[0], "len =", len(numbers));

print("--- loops ---");
var i = 0;
while i < 3 {
    print("while i =", i);
    i = i + 1;
}

for i in range(5) {
    if i == 2 {
        continue;
    }
    if i == 4 {
        break;
    }
    print("for i =", i);
}

print("--- strings ---");
var text = "Spectra3D";
print(text, len(text), text[0], text + " Script");
print(type(1), type(1.5), type("s"), type(true), type(nil));
