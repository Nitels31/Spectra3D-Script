// closures capture variables by reference
def make_counter(start) {
    var count = start;
    def increment() {
        count = count + 1;
        return count;
    }
    return increment;
}

var counter = make_counter(10);
print(counter());
print(counter());
print(counter());

def make_pair() {
    var value = 1;
    def getter() {
        return value;
    }
    def setter(v) {
        value = v;
    }
    return [getter, setter];
}

var pair = make_pair();
pair[1](42);
print(pair[0]());
