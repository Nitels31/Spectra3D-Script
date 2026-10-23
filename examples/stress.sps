// Spectra3D Script - memory / GC stress test
//
// Creates a lot of short lived strings and lists. The garbage collector must
// reclaim them, so the process memory stays flat.

def build_list(count) {
    var result = [];
    for i in range(count) {
        result = result + [i * 2];
    }
    return result;
}

def build_text(count) {
    var text = "";
    for i in range(count) {
        text = text + "x";
    }
    return text;
}

var total = 0;
for round in range(500) {
    var numbers = build_list(50);
    var text = build_text(50);
    total = total + len(numbers) + len(text);
}

print("total =", total);
print("still alive:", len(build_list(10)));
