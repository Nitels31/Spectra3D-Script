// classes and structures
class Point {
    float x;
    float y;

    def init(x, y) {
        self.x = x;
        self.y = y;
    }

    def sum() {
        return self.x + self.y;
    }

    def move(dx, dy) {
        self.x = self.x + dx;
        self.y = self.y + dy;
        return self;
    }

    def scaled(factor) {
        self.x = self.x * factor;
        self.y = self.y * factor;
        return self;
    }
}

struct Vec2 {
    float x = 0;
    float y = 0;
}

class Counter {
    var count = 0;

    def bump() {
        self.count = self.count + 1;
        return self.count;
    }
}

var p = Point(3, 4);
print(p.x, p.y, p.sum());
p.move(1, 1);
print(p.x, p.y, p.sum());
p.scaled(2);
print(p.x, p.y);

// a class is a reference type
var a = Point(1, 2);
var b = a;
b.x = 100;
print(a.x, b.x);

// a struct is copied on assignment
var v1 = Vec2();
var v2 = v1;
v2.x = 50;
print(v1.x, v2.x);

var c = Counter();
print(c.bump(), c.bump(), c.count);
print(p, c);
print(type(p), type(Vec2));
