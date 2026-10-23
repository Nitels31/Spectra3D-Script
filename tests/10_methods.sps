// methods of the built-in types
var text = "  Hello World  ";
print(text.strip().upper());
print(text.strip().lower());
print("a,b,c".split(","));
print("hello".replace("l", "L"));
print("hello".find("ll"), "hello".contains("ell"), "hello".startswith("he"));
print("hello".endswith("lo"), "hello".count("l"));
print("123".isdigit(), "abc".isalpha(), "  ".isspace());

var list = [3, 1, 2];
list.append(4);
print(list);
list.sort();
print(list);
list.reverse();
print(list);
print(list.index(3), list.contains(9), list.count(2));
list.insert(0, 0);
print(list);
list.remove(2);
print(list);
print(list.pop());
print(["a", "b"].join("-"));

var d = {"a": 1};
print(d.get("a"), d.get("b", 0), d.keys(), d.values());

for word in "abc" {
    print(word);
}
