// A game script of Spectra3D: the scene, the player, the triggers, the sound.

var scene = Scene("level_01")
var player = Entity("player")
var coin = Entity("coin")

// --- the appearance ---
player.position = vec3(0, 1, 0)
player.size = vec3(2, 2, 2)
player.rotation = vec3(0, 0, 0)
player.color = color(0.2, 0.7, 1.0, 1.0)
player.material = loadMaterial("assets/player.mat")
player.texture = loadTexture("assets/player.png")
player.tag = "hero"

coin.position = vec3(4, 0.5, 0)
coin.color = color(1.0, 0.8, 0.0, 1.0)
coin.tag = "pickup"

// --- the physics ---
var body = Rigidbody(player)
body.gravity = 9.81
body.mass = 70.0
body.velocity = vec3(0, 0, 0)

var collider = Collider(player)
collider.shape = "box"
collider.size = vec3(2, 2, 2)
collider.isTrigger = false

// --- the trigger of the coins ---
var zone = Trigger(coin)
zone.size = vec3(3, 2, 3)
zone.onEnter = "on_coin"

// --- the sound ---
var music = AudioSource()
music.sound = "theme.ogg"
music.volume = 0.5
music.loop = true
music.play()

// --- the shaders ---
var water = Shader("assets/water.shader")
water.setFloat("uTime", 0.0)
water.setVec3("uColor", vec3(0.1, 0.4, 0.9))
water.setInt("uSteps", 4)
water.setTexture("uNoise", "assets/noise.png")

var surface = Material("assets/water.mat")
surface.shader = water
surface.color = color(0.1, 0.4, 0.9, 1.0)
surface.shininess = 64.0
surface.setFloat("uRipple", 0.25)

// --- the camera and the light ---
var camera = Camera()
camera.fov = 70.0
camera.far = 500.0

var sun = Light()
sun.type = "directional"
sun.intensity = 0.9
sun.range = 50.0

// --- the rotation with the quaternions ---
var look = quatFromEuler(0, 90, 0)
player.rotation = vec3(0, 90, 0)
log("the direction of the look:", look.rotate(vec3(1, 0, 0)))

// --- the interface ---
var label = Label()
label.text = "Coins: 0"
label.position = vec3(10, 10, 0)

var button = Button()
button.text = "Play"
button.onClick = "on_play"

var panel = Panel()
panel.position = vec3(0, 0, 0)
panel.size = vec3(200, 40, 0)
panel.color = color(0, 0, 0, 0.5)
panel.onHover = "on_panel_hover"

var icon = Image()
icon.texture = "assets/hud.png"
icon.position = vec3(10, 10, 0)
icon.size = vec3(32, 32, 0)

// --- the layers of the collision ---
player.layer = 1
coin.layer = 2
collider.layer = 1
collider.mask = 3

// --- the 2D render ---
var heroTexture = loadTexture("assets/hero.png", 64, 64)

var hero = Sprite2D()
hero.setTexture(heroTexture.name)
hero.setPosition(120, 80)
hero.setSize(64, 64)
hero.zIndex = 10
hero.tint = color(1.0, 0.9, 0.2, 1.0)
hero.flipX = true

var coinSprite = Sprite2D()
coinSprite.setTexture("assets/coin.png")
coinSprite.setPosition(220, 80)
coinSprite.setSize(32, 32)
coinSprite.zIndex = 5

var camera2d = Camera2D()
camera2d.zoom = 2.0
camera2d.follow(player)

// --- the buses of the audio ---
var musicBus = createBus("music")
musicBus.setVolume(0.5)
music.bus = "music"

var effectsBus = createBus("effects")
effectsBus.setVolume(0.75)

var effect = AudioSource()
effect.sound = "coin.wav"
effect.volume = 0.8
effect.bus = "effects"

// --- the events of the game ---
on("coin_taken", "on_coin_taken")
on("enemy_spawned", "on_enemy")

// --- the input actions ---
bindAction("jump", "space")
bindAction("left", "a")
bindAction("right", "d")

// --- the start ---
def on_start() {
    log("the scene", scene.name, "is ready")
    log("the player is at", player.position)
    log("the action jump is bound to", actionKey("jump"))

    // the timers of the script
    after(0.5, "on_timer")
    every(1.0, "on_tick")

    // the ray: the coin is straight ahead
    var hit = raycast(vec3(0, 1, 0), vec3(0, 0, 1), 100.0, 3)
    if hit != nil {
        log("the ray found:", hit[0].name, hit[3])
    }
}

// --- the frame ---
def on_update(delta) {
    if downAction("jump") {
        player.move(0, 1, 0)
        playSound("jump.wav", 0.8, 1.0)
    }
    if downAction("left") {
        player.move(-1, 0, 0)
    }
    if downAction("right") {
        player.move(1, 0, 0)
    }
    body.velocity = vec3(0, body.velocity.y - body.gravity * delta, 0)
    if body.velocity.y < 0 {
        body.velocity = vec3(0, 0, 0)
    }
}

// --- the timers ---
def on_timer() {
    log("the timer is fired")
}

def on_tick() {
    log("the tick", frame())
}

// --- the triggers ---
def on_coin(entity) {
    log("the coin is taken:", entity.name)
    label.text = "Coins: 1"
    stopSound("jump.wav")
    emit("coin_taken", entity)
}

// --- the events ---
def on_coin_taken(entity) {
    log("the event is fired:", entity.name)
}

def on_enemy(value) {
    log("an enemy is spawned", value)
}

// --- the interface ---
def on_panel_hover() {
    log("the panel is hovered")
}

def on_play() {
    log("the play button is pressed")
    showMessage("the game is started")
}

// --- the helpers ---
def helper(value) {
    return value * 2
}

// --- the 2D render and the buses of the audio ---
def on_render_2d() {
    // the camera follows the player
    var point = camera2d.worldToScreen(10, 20)
    log("the point on the screen:", point.x, point.y)
    var back = camera2d.screenToWorld(point.x, point.y)
    log("the point in the world:", back.x, back.y)

    // the sprites are drawn in the order of their zIndex
    hero.draw()
    renderBegin()
    renderClear(color(0.1, 0.1, 0.2, 1.0))
    renderSetViewport(0, 0, 800, 600)
    renderDrawSprite(hero)
    renderDrawSprite(coinSprite)
    renderDrawRect(20, 20, 120, 60, color(0.2, 0.6, 1.0, 0.5), true)
    renderDrawCircle(400, 300, 48, color(1.0, 0.5, 0.0, 1.0), false)
    renderDrawLine(0, 0, 800, 600, color(0.3, 0.3, 0.3, 1.0))
    renderDrawText("coins: 1", 16, 24, 18.0, color(1.0, 1.0, 1.0, 1.0))
    renderEnd()

    // the volume of the source is the volume of the bus times the master
    setMasterVolume(0.75)
    setBusVolume("music", 0.5)
    log("the master volume is", getMasterVolume())
    for name in listBuses() {
        log("the bus", name)
    }
    music.play()
    effect.play()
    // the muted bus is silent
    muteBus("effects", true)
    effect.play()
    musicBus.pause()
    musicBus.resume()
}

def start() {
    log("the script is ready")
    stopSound("theme.ogg")
    log("the helper gives", callFunction("helper", 21))
    log("the math:", lerp(0, 10, 0.5), wrap(370, 0, 360), sign(-4))
    log("the screen:", screenToWorld(400, 300))
    on_render_2d()
    return scene.count()
}
