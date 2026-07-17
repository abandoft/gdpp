extends Node
class_name CrossRefB

var peer: CrossRefA


func connect_peer(value: CrossRefA) -> CrossRefA:
    peer = value
    return peer
