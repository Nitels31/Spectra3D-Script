// f-strings and format()
var name = "Spectra3D";
var version = 3;
var ratio = 3.14159;

print(f"Hello, {name}!");
print(f"Version {version}, ratio {ratio}");
print(f"{name} v{version} in {2026}");
print(f"sum = {1 + 2 * 3}, call = {name.upper()}");

// the format specs
print(f"{ratio:.2f}");
print(f"{version:5d}");
print(f"{name:>12}");
print(f"{name:<12}|");
print(f"{ratio:.1f}");

// the braces can be escaped
print(f"{{literal}}");

// format() as a function and as a method
print(format("Hello, {}!", name));
print(format("{0} and {1}", "first", "second"));
print("{} + {} = {}".format(1, 2, 3));
print("{name} is {age}".format({"name": "SPScript", "age": 1}));
print("{:>8}|".format("right"));
