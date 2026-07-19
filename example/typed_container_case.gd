extends RefCounted


func run() -> Dictionary:
    var values: Array[int] = [1, 2, 3]
    var weights: Dictionary[String, int] = {"left": 4, "right": 5}
    var objects: Array[RefCounted] = []
    var variants: Array[Variant] = []
    var variant_dictionary: Dictionary[Variant, Variant] = {}
    var result := roundtrip([6, 7])
    return {
        "values": values,
        "weights": weights,
        "array_typed": values.is_typed(),
        "array_builtin": values.get_typed_builtin(),
        "key_typed": weights.is_typed_key(),
        "value_typed": weights.is_typed_value(),
        "key_builtin": weights.get_typed_key_builtin(),
        "value_builtin": weights.get_typed_value_builtin(),
        "object_typed": objects.is_typed(),
        "object_class": objects.get_typed_class_name(),
        "variants_typed": variants.is_typed(),
        "variant_dictionary_typed": variant_dictionary.is_typed(),
        "roundtrip": result,
        "roundtrip_key_typed": result.is_typed_key(),
        "roundtrip_value_typed": result.is_typed_value(),
    }


func roundtrip(values: Array[int]) -> Dictionary[String, int]:
    var total := 0
    for value: int in values:
        total += value
    return {"sum": total}
