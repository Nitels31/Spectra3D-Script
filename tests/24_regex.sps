// the regular expressions
var digits = regex("\\d+");
print(regexMatch(digits, "12345"));
print(regexMatch(digits, "12a45"));
print(regexTest(digits, "abc123"));
print(regexTest(digits, "abc"));

// the character classes
print(regexMatch(regex("[a-z]+"), "spectra"));
print(regexMatch(regex("[^a-z]+"), "SPECTRA"));
print(regexMatch(regex("[0-9a-f]+"), "1a2f"));
print(regexMatch(regex("\\w+"), "abc_123"));
print(regexMatch(regex("\\s+"), " \t"));
print(regexMatch(regex("[.]"), "."));
print(regexMatch(regex("a.c"), "abc"));
print(regexMatch(regex("a.c"), "a\nc"));

// the quantifiers
print(regexMatch(regex("a{3}"), "aaa"));
print(regexMatch(regex("a{3}"), "aa"));
print(regexMatch(regex("a{2,}"), "aaaa"));
print(regexMatch(regex("a{2,3}"), "aa"));
print(regexMatch(regex("a{2,3}"), "aaaa"));
print(regexMatch(regex("a*"), ""));
print(regexMatch(regex("a+"), "aaa"));
print(regexMatch(regex("a+"), ""));
print(regexMatch(regex("colou?r"), "color"));
print(regexMatch(regex("colou?r"), "colour"));

// the lazy quantifiers
print(regexSearch(regex("<.+>"), "<a><b>"));
print(regexSearch(regex("<.+?>"), "<a><b>"));

// the groups and the back references
var pair = regex("(\\w+)\\s(\\w+)");
print(regexGroupCount(pair));
print(regexSearch(pair, "hello world"));
print(regexMatch(regex("(\\w)\\1"), "aa"));
print(regexMatch(regex("(\\w)\\1"), "ab"));
print(regexMatch(regex("(a|b)+"), "abab"));

// the groups without the capture and the nested groups
print(regexGroupCount(regex("(?:ab)+")));
print(regexMatch(regex("(?:ab)+"), "abab"));
var nested = regex("((a)(b))");
print(regexGroupCount(nested));
print(regexSearch(nested, "ab"));
print(regexSearch(regex("(a)|(b)"), "b"));

// the hexadecimal escape
print(regexMatch(regex("\\x41\\x42"), "AB"));

// the alternation and the anchors
print(regexMatch(regex("cat|dog"), "dog"));
print(regexMatch(regex("^(cat|dog)$"), "dog"));
print(regexMatch(regex("^(cat|dog)$"), "dogs"));
print(regexTest(regex("^abc"), "abcdef"));
print(regexTest(regex("def$"), "abcdef"));
print(regexTest(regex("^abc$"), "abcdef"));

// the search
print(regexSearch(regex("\\d+"), "a1b22c333"));
print(regexSearch(regex("z"), "abc"));

// the replace
print(regexReplace(regex("\\s+"), "a   b  c", "_"));
print(regexReplace(regex("(\\w+)@(\\w+)"), "mail: user@host!", "$2:$1"));
print(regexReplace(regex("\\d"), "a1b2", "[$0]"));
print(regexReplace(regex("x"), "abc", "-"));
print(regexReplace(regex("(\\w+)"), "abc", "${1}!"));
print(regexReplace(regex("a"), "a", "$$"));

// the split
print(regexSplit(regex(",\\s*"), "a, b,c ,  d"));
print(regexSplit(regex("\\d+"), "a1b22c"));

// the empty repetitions do not hang
print(regexMatch(regex("(a*)*"), ""));
print(regexMatch(regex("(a*)*"), "aaa"));

// a broken pattern is a normal error
try {
    regex("(");
} catch err {
    print("error:", err);
}

try {
    regex("a{2,1}");
} catch err {
    print("error:", err);
}
