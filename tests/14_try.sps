// error handling
def risky(value) {
    return 10 / value;
}

try {
    print(risky(2));
} catch err {
    print("never");
}

try {
    print(risky(0));
} catch err {
    print("caught:", err);
}

try {
    var list = [1, 2];
    print(list[10]);
} catch err {
    print("index:", err);
}

// nested try blocks
try {
    try {
        risky(0);
    } catch inner {
        print("inner:", inner);
        risky(0);
    }
} catch outer {
    print("outer:", outer);
}

// the program continues after the catch
print("done");
