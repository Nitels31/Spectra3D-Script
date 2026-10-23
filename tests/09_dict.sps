// dictionaries
var d = {"name": "Spectra3D", "version": 1};
print(d["name"], d["version"]);

d["extra"] = true;
print(len(d));
print(d.has("extra"), d.has("nope"));
print(d.get("missing", "default"));

d.remove("version");
print(len(d), d.has("version"));

for key in d {
    print("key:", key, "=", d[key]);
}

print(d.keys(), d.values());
print(d.items());

var empty = {};
print(empty, len(empty));
