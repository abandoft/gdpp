extends Node

var received_value: int = -1


@rpc
func default_rpc() -> void:
    pass


@rpc("any_peer", "call_local", "reliable", 3)
func configured_rpc(value: int) -> void:
    received_value = value
