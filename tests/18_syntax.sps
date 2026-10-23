// the syntax that is easy for a beginner
// the semicolons are optional

var name = "Spectra3D"
var version = 3

print(name, version)
print("the semicolons are not needed")

// the colon in front of a block is optional too
if version > 2:
{
    print("the version is new")
}
else:
{
    print("the version is old")
}

if version > 5 {
    print("this is not printed")
} else if version > 2 {
    print("the second branch")
} else {
    print("the last branch")
}

// the ternary operator
var state = version > 2 ? "new" : "old"
print(state)

// the comparisons
print(1 == 1, 1 != 2, 2 < 3, 3 <= 3, 4 > 3, 4 >= 4)

// the loops
for i in range(3) {
    print("for", i)
}

var n = 0
while n < 2 {
    print("while", n)
    n = n + 1
}

// the functions
def add(a, b) {
    return a + b
}

print(add(2, 3))

// the classes
class Counter {
    var count = 0

    def bump() {
        self.count = self.count + 1
        return self.count
    }
}

var counter = Counter()
print(counter.bump(), counter.bump())
