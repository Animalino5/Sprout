' rooms.bas: Rooms: Menu / Play / Game Over
'
' A "room" is a self-contained scene with two parts:
'
'   ROOM "name"
'       ' ... init code — runs ONCE when you switch to this room ...
'   UPDATE
'       ' ... update code — runs every frame while this room is active ...
'   END UPDATE
'   END ROOM
'
' Switch rooms with: GOTOROOM "name"
'
' When you call GOTOROOM, the new room's init runs immediately, then
' its update runs automatically every WAITFRAME. You do NOT write a
' WHILE TRUE loop inside a room, the runtime drives it for you.
'
' This example shows three rooms:
'   "menu" = title screen, press A to start
'   "play" = move a dot with the D-pad, avoid the edges
'   "gameover" = shows your time, press A to retry
'
' Shared state (visible to all rooms, since rooms are global)
px = 200
py = 120
play_start = 0
play_time = 0

IMPORT "menu.bas"
IMPORT "play.bas"
IMPORT "gameover.bas"

' Boot into the menu room
GOTOROOM "menu"

' Main loop, all rooms share this one frame pump
WHILE TRUE
    WAITFRAME
WEND
'
' Go on and explore the other room files!
'