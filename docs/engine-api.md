# API рухія Spectra3D для мови SPScript

Повний перелік того, що рушій віддає скриптам. Реалізація наведена в
`examples/host/` (міні-рушій на C++ через C API) — це еталон того, як
підключати свої класи.

```sps
var player = Entity("player")
player.position = vec3(0, 1, 0)
player.move(1, 0, 0)
```

---

## Сцени та об'єкти

### `Entity` — об'єкт сцени

| Властивість | Тип | Опис |
|---|---|---|
| `name` | str | ім'я об'єкта |
| `position` | Vector3 | позиція у світі |
| `rotation` | Vector3 | обертання (кути) |
| `size` | Vector3 | розмір |
| `color` | Color | колір |
| `material` | str | матеріал (шлях) |
| `texture` | str | текстура (шлях) |
| `tag` | str | тег для пошуку |
| `active` | bool | чи активний об'єкт |

| Метод | Опис |
|---|---|
| `entity.move(x, y, z)` | зсув на дельту |
| `entity.rotate(x, y, z)` | обертання на дельту |
| `entity.scale(x, y, z)` | масштабування (множення) |
| `entity.lookAt(x, y, z)` | повернутися до точки |
| `entity.destroy()` | видалити об'єкт |

### `Scene` — сцена

| Властивість | Тип | Опис |
|---|---|---|
| `name` | str | назва сцени |
| `gravity` | float | гравітація |

| Метод | Опис |
|---|---|
| `scene.find(name)` | знайти об'єкт за іменем (або `nil`) |
| `scene.count()` | кількість об'єктів |

### `Camera` — камера

| Властивість | Тип | Опис |
|---|---|---|
| `fov` | float | кут огляду |
| `far` | float | дальність відсікання |

| Метод | Опис |
|---|---|
| `camera.lookAt(x, y, z)` | навести на точку |

### `Light` — світло

| Властивість | Тип | Опис |
|---|---|---|
| `type` | str | `directional`, `point`, `spot` |
| `intensity` | float | сила світла |
| `range` | float | радіус дії |

### `Vector3` та `Color`

Створюються функціями `vec3(x, y, z)` та `color(r, g, b, a)`.
Властивості: `x`/`y`/`z` та `r`/`g`/`b`/`a` (читання і запис).

---

## Фізика

### `Rigidbody` — тіло

| Властивість | Тип | Опис |
|---|---|---|
| `velocity` | Vector3 | швидкість |
| `mass` | float | маса |
| `gravity` | float | гравітація тіла |
| `kinematic` | bool | кінематичне тіло (рух скриптом) |

| Метод | Опис |
|---|---|
| `body.applyForce(x, y, z)` | додати силу |
| `body.setVelocity(v)` | задати швидкість |

### `Collider` — колайдер

| Властивість | Тип | Опис |
|---|---|---|
| `shape` | str | `box`, `sphere`, `capsule` |
| `size` | Vector3 | розмір коробки |
| `radius` | float | радіус сфери |
| `isTrigger` | bool | чи є це тригером |

---

## Тригери

### `Trigger` — зона тригера

| Властивість | Тип | Опис |
|---|---|---|
| `size` | Vector3 | розмір зони |
| `onEnter` | str | назва функції скрипта, яка викликається при вході |
| `onExit` | str | назва функції скрипта, яка викликається при виході |

```sps
var zone = Trigger(coin)
zone.size = vec3(3, 2, 3)
zone.onEnter = "on_coin"

def on_coin(entity) {
    log("the coin is taken:", entity.name)
}
```

---

## Звук

### Функції

| Функція | Опис |
|---|---|
| `playSound(name, volume, pitch)` | одноразовий звук |
| `stopSound(name)` | зупинити звук |
| `soundPlaying(name)` | чи грає (bool) |
| `setVolume(name, volume)` | громкість |
| `playMusic(name, volume, loop)` | музика (зациклена) |
| `stopMusic()` | зупинити музику |

### `AudioSource` — джерело звуку

| Властивість | Тип | Опис |
|---|---|---|
| `sound` | str | назва файлу |
| `volume` | float | громкість |
| `pitch` | float | висота |
| `loop` | bool | зациклити |

| Метод | Опис |
|---|---|
| `source.play()` | грати |
| `source.stop()` | зупинити |
| `source.isPlaying()` | чи грає (bool) |

---

## Ввід

| Функція | Опис |
|---|---|
| `downKey(name)` | клавіша натиснута зараз (bool) |
| `justPressed(name)` | клавіша натиснута в цьому кадрі (bool) |
| `justReleased(name)` | клавіша відпущена в цьому кадрі (bool) |
| `mouseX()` | положення миші по X |
| `mouseY()` | положення миші по Y |
| `mouseDown(button)` | кнопка миші натиснута (bool) |
| `gamepadDown(pad, button)` | кнопка геймпада (bool) |
| `gamepadAxis(pad, axis)` | вісь геймпада |

Імена клавіш: `"Space"`, `"A"`, `"Escape"`, `"ShiftLeft"`, ...

---

## Час

| Функція | Опис |
|---|---|
| `deltaTime()` | час кадру у секундах |
| `time()` | час від запуску |
| `frame()` | номер кадру |
| `wait(seconds)` | пауза |

---

## Ресурси

| Функція | Опис |
|---|---|
| `loadModel(path)` | завантажити модель |
| `loadTexture(path)` | завантажити текстуру |
| `loadMaterial(path)` | завантажити матеріал |
| `loadScene(path)` | завантажити сцену (SPL) |
| `loadFont(path)` | завантажити шрифт |

---

## Математика

| Функція | Опис |
|---|---|
| `vec3.add(a, b)` | сума двох векторів |
| `vec3.sub(a, b)` | різниця |
| `vec3.mul(a, b)` | множення (на вектор або на число) |
| `vec3.dot(a, b)` | скалярний добуток |
| `vec3.cross(a, b)` | векторний добуток |
| `vec3.length(a)` | довжина |
| `vec3.normalize(a)` | одиничний вектор |
| `vec3.lerp(a, b, t)` | інтерполяція |
| `distance(a, b)` | відстань |
| `clamp(value, min, max)` | обмежити значення |
| `random()` | випадкове число 0..1 |
| `randomRange(a, b)` | випадкове число у діапазоні |

---

## Інтерфейс

| Клас | Властивості |
|---|---|
| `Label` | `text` `position` `color` `size` |
| `Button` | `text` `position` `color` `onClick` `enabled` |

| Функція | Опис |
|---|---|
| `showMessage(text)` | показати повідомлення |

---

## Шари зіткнення та фільтри

Кожен об'єкт має шар — бітова маска, що визначає, з ким він стикається.

| Властивість | Тип | Опис |
|---|---|---|
| `entity.layer` | int | шар об'єкта |
| `collider.layer` | int | шар колайдера |
| `collider.mask` | int | з ким він стикається (бітова маска) |

```sps
player.layer = 1
enemy.layer = 2

collider.layer = 1
collider.mask = 3          // 1 | 2 - the player and the enemy

// the raycast with the filter of the layers
var hit = raycast(vec3(0, 1, 0), vec3(0, 0, 1), 100.0, 3)
```

---

## Інтерфейс

### `Panel` — панель

| Властивість | Тип | Опис |
|---|---|---|
| `position` | Vector3 | положення |
| `size` | Vector3 | розмір |
| `color` | Color | колір |
| `onHover` | str | функція скрипта, яка викликається під курсором |

### `Image` — зображення

| Властивість | Тип | Опис |
|---|---|---|
| `texture` | str | текстура |
| `position` | Vector3 | положення |
| `size` | Vector3 | розмір |
| `color` | Color | колір |

```sps
var panel = Panel()
panel.position = vec3(0, 0, 0)
panel.size = vec3(200, 40, 0)
panel.color = color(0, 0, 0, 0.5)
panel.onHover = "on_panel_hover"
```

---

## Шейдери та матеріали

### `Shader` — шейдер і його уніформи

| Властивість | Тип | Опис |
|---|---|---|
| `name` | str | назва шейдера |

| Метод | Опис |
|---|---|
| `shader.setFloat(name, value)` | уніформа `float` |
| `shader.setVec3(name, vector)` | уніформа `vec3` |
| `shader.setInt(name, value)` | уніформа `int` |
| `shader.setBool(name, value)` | уніформа `bool` |
| `shader.setTexture(name, path)` | уніформа-текстура |

### `Material` — матеріал

| Властивість | Тип | Опис |
|---|---|---|
| `name` | str | назва матеріалу |
| `color` | Color | базовий колір |
| `shininess` | float | блиск |
| `shader` | Shader | шейдер матеріалу |

| Метод | Опис |
|---|---|
| `material.setFloat(name, value)` | уніформа матеріалу (йде в шейдер) |
| `material.setVec3(name, vector)` | векторна уніформа |
| `material.setTexture(name, path)` | текстурна уніформа |

```sps
var water = Shader("assets/water.shader")
water.setFloat("uTime", 0.0)
water.setVec3("uColor", vec3(0.1, 0.4, 0.9))
water.setTexture("uNoise", "assets/noise.png")

var surface = Material("assets/water.mat")
surface.shader = water
surface.color = color(0.1, 0.4, 0.9, 1.0)
surface.setFloat("uRipple", 0.25)
```

---

## Математика 3D: кватерніони та матриці

### `Quaternion` — обертання

| Властивість | Тип | Опис |
|---|---|---|
| `x` `y` `z` `w` | float | компоненти |

| Метод | Опис |
|---|---|
| `quat.multiply(other)` | добуток кватерніонів |
| `quat.normalize()` | одинична одинична нормалізована |
| `quat.rotate(vector)` | повернути вектор кватерніоном |
| `quat.slerp(other, t)` | плавна інтерполяція |

### Функції

| Функція | Опис |
|---|---|
| `quatFromEuler(pitch, yaw, roll)` | кватерніон з кутів Ейлера |
| `quatMultiply(a, b)` | добуток двох кватерніонів |
| `quatRotate(q, vector)` | повернути вектор |
| `quatSlerp(a, b, t)` | інтерполяція |
| `mat4Translation(v)` | матриця переміщення |
| `mat4Scaling(v)` | матриця масштабу |
| `mat4Rotation(q)` | матриця обертання |
| `mat4Perspective(fov, aspect, near, far)` | перспективна проєкція |
| `mat4Multiply(a, b)` | добуток матриць |
| `mat4Transform(m, v)` | перетворити точку матрицею |

```sps
var q = quatFromEuler(0, 90, 0)
print(q.rotate(vec3(1, 0, 0)))

var m = mat4Translation(vec3(1, 2, 3))
print(m.transform(vec3(0, 0, 0)))
```

---

## Дії керування

Клавіші прив'язуються до імен дій — так їх можна переназначити в налаштуваннях.

| Функція | Опис |
|---|---|
| `bindAction(action, key)` | прив'язати дію до клавіші |
| `actionKey(action)` | яка клавіша у дії |
| `downAction(action)` | дія натиснута зараз (bool) |
| `justPressedAction(action)` | дія натиснута в цьому кадрі (bool) |

```sps
bindAction("jump", "space")
bindAction("fire", "mouse0")

if downAction("jump") { player.jump() }
```

---

## Події (шина подій)

Скрипт підписується на подію рухія або надсилає свою.

| Функція | Опис |
|---|---|
| `on(event, function)` | підписати функцію скрипта на подію |
| `off(event, function)` | відписати |
| `emit(event, value)` | надіслати подію (викликає всіх підписників) |

```sps
on("coin_taken", "on_coin_taken")

def on_coin_taken(entity) {
    log("the coin:", entity.name)
}

emit("coin_taken", player)
```

---

## Таймери

| Функція | Опис |
|---|---|
| `after(seconds, function)` | викликати функцію один раз через N секунд |
| `every(seconds, function)` | викликати функцію кожні N секунд |
| `cancelTimer(id)` | скасувати таймер |

```sps
after(2.0, "show_the_menu")
var id = every(0.5, "regenerate")
```

---

## Фізика: запити до світу

| Функція | Опис |
|---|---|
| `raycast(origin, direction, distance)` | промінь; повертає список `[entity, point, normal, distance]` або `nil` |
| `applyImpulse(entity, impulse)` | додати імпульс тілу |
| `setGravity(value)` | гравітація сцени |
| `getGravity()` | поточна гравітація |

```sps
var hit = raycast(vec3(0, 1, 0), vec3(0, 0, 1), 50.0)
if hit != nil {
    log("the ray found:", hit[0].name, "at", hit[3])
}
```

---

## Об'єкти

| Функція | Опис |
|---|---|
| `createEntity(name)` | створити об'єкт |
| `clone(entity, name)` | копію об'єкта |
| `setParent(child, parent)` | вкласти об'єкт в інший |
| `findByTag(tag)` | знайти за тегом |
| `findAllByTag(tag)` | усі за тегом (список) |

---

## Екран

| Функція | Опис |
|---|---|
| `screenToWorld(x, y)` | координата екрана у світі |
| `worldToScreen(vec3)` | координата світу на екрані |
| `setCursorVisible(bool)` | показ курсора |

---

## Анімації та ефекти

| Функція | Опис |
|---|---|
| `playAnimation(entity, name, loop)` | запустити анімацію |
| `stopAnimation(entity, name)` | зупинити анімацію |
| `emitParticles(effect, position)` | випустити частинки |
| `setTimeScale(scale)` | швидкість часу |
| `pauseGame()` / `resumeGame()` | пауза |

---

## Система

| Функція | Опис |
|---|---|
| `getFPS()` | кадрів на секунду |
| `frame()` | номер кадру |
| `platform()` | назва платформи (`"windows"`) |
| `logWarning(text)` / `logError(text)` | журнал із рівнем |
| `callFunction(name, ...)` | викликати іншу функцію скрипта |
| `quit()` | закрити гру |

---

## Утиліти

| Функція | Опис |
|---|---|
| `lerp(a, b, t)` | інтерполяція числа |
| `moveTowards(value, target, step)` | рух до значення |
| `wrap(value, min, max)` | зациклити значення в діапазоні |
| `sign(value)` | знак числа |
| `repeat(value, length)` | зациклити число |
| `shuffle(list)` | перемішати список |
| `randomPoint(radius)` | випадкова точка в колі |

---

## Хуки скрипта

Рушій шукає ці функції у скрипті та викликає їх, коли вони є:

| Функція | Коли викликається |
|---|---|
| `on_start()` | перед першим кадром |
| `on_update(delta)` | кожен кадр |
| `on_collision(a, b)` | зіткнення двох об'єктів |
| `on_trigger_enter(entity)` | вхід у зону тригера |
| `on_trigger_exit(entity)` | вихід із зони тригера |
| `on_click(entity)` | натискання кнопки |
| `on_input(action)` | ввід ( назва клавіші або дія) |
| `on_destroy()` | знищення об'єкта |

---

## Як додати свій клас

```cpp
#include "sps/sps.h"

static SpsHostClass* g_my_class = nullptr;

static SpsValue my_constructor(SpsContext* ctx, int argc, const SpsValue* args, void* user) {
    (void)ctx; (void)argc; (void)args; (void)user;
    return sps_host_class_new_object(g_my_class, new MyData());
}

static SpsValue my_method(SpsContext* ctx, int argc, const SpsValue* args, void* user) {
    (void)ctx; (void)argc; (void)user;
    MyData* data = (MyData*)sps_value_to_userdata(args[0]);
    // args[0] — це об'єкт, args[1] — перший аргумент
    return sps_int(data ? data->value : 0);
}

static SpsValue my_getter(SpsContext* ctx, SpsValue object, void* user) {
    (void)ctx; (void)user;
    MyData* data = (MyData*)sps_value_to_userdata(object);
    return sps_int(data ? data->value : 0);
}

static SpsValue my_setter(SpsContext* ctx, SpsValue object, SpsValue value, void* user) {
    (void)ctx; (void)user;
    MyData* data = (MyData*)sps_value_to_userdata(object);
    if (data) data->value = (int)sps_value_to_int(value);
    return sps_nil();
}

// в main():
g_my_class = sps_host_class_new(context, "MyClass");
sps_host_class_constructor(g_my_class, my_constructor, NULL);
sps_host_class_method(g_my_class, "doIt", my_method, NULL);
sps_host_class_property(g_my_class, "value", my_getter, my_setter, NULL);
```

Правило життя рядків: текст, який повертає нативна функція, має залишатися
дійсним до наступного виклику API — використовуйте статичний буфер.
