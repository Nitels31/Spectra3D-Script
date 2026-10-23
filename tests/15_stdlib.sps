// the standard library: files, time, json, math
var data = {"name": "Spectra3D", "version": 3, "tags": ["engine", "script"]};

var text = json_stringify(data);
print(text);

var parsed = json_parse(text);
print(parsed["name"], parsed["version"]);
print(parsed["tags"][1]);
print(type(parsed));

// files
var path = "out/sps_test_file.txt";
print(write_file(path, "hello from SPScript\n"));
print(file_exists(path));
print(read_file(path));
print(file_size(path));
append_file(path, "second line\n");
print(read_file(path));
print(delete_file(path));
print(file_exists(path));

// time
var stamp = time();
print(stamp > 1700000000.0);
print(len(now()) == 19);

// math
print(sqrt(16.0), floor(3.7), ceil(3.2), round(3.5));
print(pow(2.0, 10.0));
print(random() >= 0.0 and random() < 1.0);
print(random_range(1, 6) >= 1);
