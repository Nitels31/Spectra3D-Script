// if / elif / else, while, for, break, continue
var i = 0;
while i < 3 {
    print("while", i);
    i = i + 1;
}

for n in range(5) {
    if n == 1 {
        continue;
    }
    if n == 3 {
        break;
    }
    print("for", n);
}

if 1 > 2 {
    print("no");
} elif 2 > 1 {
    print("yes");
} else {
    print("nope");
}

var k = 0;
while true {
    k = k + 1;
    if k > 2 {
        break;
    }
}
print("k =", k);
