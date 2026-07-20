extends Resource

@export_custom(
    PROPERTY_HINT_RANGE,
    "0,10",
    PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY,
)
var amount: int = 1


func run() -> Dictionary:
    for property: Dictionary in get_property_list():
        if property.get("name") == &"amount":
            return {
                "hint": property.get("hint"),
                "hint_string": property.get("hint_string"),
                "usage": property.get("usage"),
            }
    return {}
