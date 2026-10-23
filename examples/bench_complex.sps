// Spectra3D Script - a complex benchmark: the objects, the calls, the loops,
// the strings and the lists. The same work is done again and again.

class Particle {
    float x;
    float y;
    float life;

    def init(x, y) {
        self.x = x;
        self.y = y;
        self.life = 1.0;
    }

    def step(delta) {
        self.x = self.x + delta;
        self.y = self.y - delta * 0.5;
        self.life = self.life - delta;
        return self.life;
    }

    def alive() {
        return self.life > 0.0;
    }
}

def make_particles(count) {
    var particles = [];
    for i in range(count) {
        particles = particles + [Particle(i * 0.1, i * 0.2)];
    }
    return particles;
}

def simulate(particles, steps) {
    var total = 0.0;
    for step in range(steps) {
        for particle in particles {
            total = total + particle.step(0.01);
        }
    }
    return total;
}

def build_text(count) {
    var text = "";
    for i in range(count) {
        text = text + "x";
    }
    return len(text);
}

def sum_list(count) {
    var numbers = [];
    for i in range(count) {
        numbers = numbers + [i * 2];
    }
    var total = 0;
    for n in numbers {
        total = total + n;
    }
    return total;
}

var particles = make_particles(400);
var alive = 0;
for particle in particles {
    if particle.alive() {
        alive = alive + 1;
    }
}

print("alive", alive);
print("simulated", simulate(particles, 150));
print("text", build_text(5000));
print("sum", sum_list(1500));
