// ebounds.swift - RUNTIME ERROR demo:
// array index out of bounds. Compiles
// cleanly; the Runner errors at the
// bad subscript. Expected: runtime
// error
var a = [10, 20, 30]
print(a[9])
