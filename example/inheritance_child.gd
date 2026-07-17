extends InheritanceBase
class_name InheritanceChild

@export var shared_mode: InheritanceBase.SharedMode = InheritanceBase.SharedMode.BOOST
const LoadedInheritanceBase = preload("res://inheritance_base.gd")
const RequiredFactory = preload("res://required_initializer.gd")


func child_answer() -> int:
    if InheritanceBase.static_answer() != 42:
        return 0
    return read_base(self)


func typed_identity(value: InheritanceBase) -> InheritanceBase:
    return value


func read_base(value: InheritanceBase) -> int:
    return value.inherited_value + 2


func cross_constants() -> int:
    match shared_mode:
        InheritanceBase.SharedMode.BOOST:
            return (
                InheritanceBase.BASE_BONUS
                + InheritanceBase.ANON_LIMIT
                + InheritanceBase.SharedMode.BOOST
            )
        _:
            return 0


func enum_roundtrip(value: InheritanceBase.SharedMode) -> InheritanceBase.SharedMode:
    return value


func resource_factory_answer() -> int:
    var preloaded: InheritanceBase = LoadedInheritanceBase.new(40)
    var loaded: InheritanceBase = load("res://inheritance_base.gd").new(1)
    var answer := preloaded.inherited_answer() + loaded.inherited_answer()
    preloaded.free()
    loaded.free()
    return answer


func required_init_answer() -> int:
    var created: RequiredInitializer = RequiredFactory.new(7)
    var answer := created.answer()
    created.free()
    return answer
