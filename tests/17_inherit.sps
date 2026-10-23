// the inheritance, the switch and the cases
class Entity {
    float x = 0;
    float y = 0;

    def init(x, y) {
        self.x = x;
        self.y = y;
    }

    def sum() {
        return self.x + self.y;
    }

    def describe() {
        return "Entity(" + str(self.x) + ", " + str(self.y) + ")";
    }
}

class Player extends Entity {
    var health = 100;

    def describe() {
        return "Player(" + str(self.x) + ", hp=" + str(self.health) + ")";
    }
}

class Enemy extends Player {
    var damage = 10;
}

var e = Entity(1, 2);
print(e.x, e.y, e.sum(), e.describe());

// the fields and the methods are inherited
var p = Player(3, 4);
print(p.x, p.y, p.health, p.sum());
print(p.describe());

// the inheritance of the inheritance
var enemy = Enemy(5, 6);
print(enemy.x, enemy.y, enemy.health, enemy.damage, enemy.sum());
print(enemy.describe());

// the switch
var state = 2;
switch state {
    case 1:
    {
        print("one");
    }
    case 2:
    {
        print("two");
    }
    case 3:
    {
        print("three");
    }
    default:
    {
        print("other");
    }
}
