extends RefCounted

signal selected(value: Variant)


func validate_release_isolation() -> void:
    assert(
        (await selected) == "GDPP_ASSERT_CONDITION_SENTINEL",
        "GDPP_ASSERT_MESSAGE_SENTINEL" + str(await selected)
    )
    print("GDPP_ASSERT_AFTER_SENTINEL")
