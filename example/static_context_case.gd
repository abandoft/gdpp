extends Resource

class Parent:
    static func base_score() -> int:
        return 2

class Child extends Parent:
    static var total: int = 1:
        get:
            return total
        set(value):
            total = value

    static func calculate() -> int:
        total = super.base_score()
        var adjust := func(value: int) -> int:
            return value + 1
        return adjust.call(total)


func run() -> Array[int]:
    Child.total = 1
    return [Child.calculate(), Child.total]
