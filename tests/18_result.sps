// the Result - the errors without the exceptions
def divide(a, b) {
    if b == 0 {
        return resultError("the division by zero");
    }
    return resultOk(a / b);
}

def load(name) {
    if name == "" {
        return resultError("the name is empty");
    }
    return resultOk("the file " + name);
}

var good = divide(10, 2);
print(isOk(good), isError(good), unwrap(good));

var bad = divide(1, 0);
print(isOk(bad), isError(bad));
print(bad["error"]);
print(unwrapOr(bad, 0.0));

// the chain of the results
var result = load("level.spl");
if isOk(result) {
    print("loaded:", unwrap(result));
}
else {
    print("failed:", result["error"]);
}

var missing = load("");
if isError(missing) {
    print("the error is:", missing["error"]);
}
