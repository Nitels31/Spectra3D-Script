// the SPL format, the membership check and the anonymous functions

// --- the SPL format of Spectra3D ---
var level = """
{
    // the name of the level
    name = "level_01"
    version = 3
    ratio = 0.5
    enabled = true
    tags = ["engine", "script"]
    player = { hp = 100, name = 'hero' }
    matrix = [1, 0, 0, 0, 1, 0, 0, 0, 1]
}
"""

var data = spl_parse(level)
print(data["name"], data["version"], data["ratio"], data["enabled"])
print(data["tags"][1], data["player"]["hp"], data["player"]["name"])
print(len(data["matrix"]))

// the compact form is shorter than the json
var compact = spl_stringify(data)
print(len(compact) < len(level))

// the pretty form
print(spl_pretty({"a": 1, "b": [1, 2]}))

// the json library
var json = json_stringify({"name": "SPScript", "values": [1, 2, 3]})
print(json)
var parsed = json_parse(json)
print(parsed["name"], parsed["values"][2])
print(json_pretty({"a": 1}))

// --- the membership check ---
print(2 in [1, 2, 3])
print("key" in {"key": 1})
print("ell" in "hello")
print(9 in [1, 2, 3])

// --- the anonymous functions ---
var double = def(x) { return x * 2 }
print(double(21))

var numbers = [1, 2, 3, 4, 5]
print(map(numbers, def(x) { return x * x }))
print(filter(numbers, def(x) { return x > 2 }))
print(reduce(numbers, def(a, b) { return a + b }, 0))

// --- the multi line strings ---
var text = """
the first line
the second line
"""
print(len(text) > 20)
