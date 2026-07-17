extends Node
class_name CrossRefA

var peer: CrossRefB


func connect_peer(value: CrossRefB) -> CrossRefB:
    peer = value
    return peer
