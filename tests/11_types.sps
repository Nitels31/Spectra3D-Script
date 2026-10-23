// typed declarations, C++ style
int suma = 13;
int zero;
float ratio = 0.5;
double precise = 0.1;
str name = "Spectra3D";
bool flag = true;
list items = [1, 2, 3];
dict table = {"a": 1};

print(suma, zero, ratio, precise, name, flag, items, table);
print(type(suma), type(ratio), type(precise), type(name), type(flag));

// int is promoted to float when needed
float from_int = suma;
double from_float = ratio;
print(from_int, from_float);

// float is 32 bit, double is 64 bit
print(ratio + 0.1);
print(precise + 0.1);
print(ratio * 3);

// the builtins still work
print(int("42"), float("1.5"), double("2.5"), str(7));

// the old dynamic style still works
var dynamic = 5;
let other = "text";
print(dynamic, other);
print(int(3.9), double(3));
