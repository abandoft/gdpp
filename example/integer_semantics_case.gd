extends RefCounted


func run(include_division_edges: bool = true) -> Dictionary:
    var maximum: int = 9223372036854775807
    var minimum: int = -9223372036854775808
    var one: int = 1
    var two: int = 2
    var sixty_three: int = 63
    var sixty_four: int = 64
    var negative_one: int = -1

    var compound: int = maximum
    compound += one
    var indexed: Array[int] = [maximum]
    indexed[0] += one

    var dynamic_maximum: Variant = maximum
    var dynamic_minimum: Variant = minimum
    var dynamic_one: Variant = one
    var dynamic_two: Variant = two
    var dynamic_sixty_four: Variant = sixty_four
    var dynamic_negative_one: Variant = negative_one

    var report: Dictionary = {
        "add": maximum + one,
        "subtract": minimum - one,
        "multiply": maximum * two,
        "negate": -minimum,
        "shift_left_sign": one << sixty_three,
        "shift_left_64": one << sixty_four,
        "shift_left_negative_count": one << negative_one,
        "shift_right_negative": minimum >> one,
        "shift_right_64": maximum >> sixty_four,
        "bit_not": ~0,
        "bit_and": negative_one & 0x55aa,
        "bit_or": 0x5500 | 0xaa,
        "bit_xor": 0x55ff ^ 0x55,
        "compound": compound,
        "indexed": indexed,
        "dynamic_add": dynamic_maximum + dynamic_one,
        "dynamic_subtract": dynamic_minimum - dynamic_one,
        "dynamic_multiply": dynamic_maximum * dynamic_two,
        "dynamic_shift_left_64": dynamic_one << dynamic_sixty_four,
        "dynamic_shift_left_negative": dynamic_one << dynamic_negative_one,
    }
    if include_division_edges:
        report["divide"] = minimum / negative_one
        report["modulo"] = minimum % negative_one
    return report
