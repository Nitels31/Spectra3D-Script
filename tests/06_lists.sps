// lists
var numbers = [1, 2, 3];
print(numbers, len(numbers));
numbers[0] = 100;
print(numbers[0], numbers[-1]);
numbers = numbers + [4, 5];
print(numbers);
print([0] * 3);

var total = 0;
for n in numbers {
    total = total + n;
}
print("total", total);

var empty = [];
print(empty, len(empty));
print(numbers == [100, 2, 3, 4, 5]);
