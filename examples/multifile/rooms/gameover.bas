' This is the gameover room! Oh no!
'
ROOM "gameover"
    ' Nothing to set up. play_time was already stored by the play room.
UPDATE
    CLS RGB(30, 10, 10)

    COLOR RGB(255, 100, 100)
    TEXTSIZE 2
    TEXT 130, 60, "GAME OVER"
    TEXTSIZE 1

    COLOR RGB(255, 255, 255)
    TEXT 110, 110, "You survived " + STR$(play_time / 1000) + " seconds"

    COLOR RGB(255, 255, 100)
    TEXT 110, 160, "Press A to retry"
    TEXT 110, 180, "Press START to quit"

    IF BUTTONDOWN(A) THEN
        GOTOROOM "play"
    ENDIF
    IF BUTTONDOWN(START) THEN END
END UPDATE
END ROOM
