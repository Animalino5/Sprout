' This is the menu room!
'
ROOM "menu"
    ' Init runs once when GOTOROOM "menu" is called.
    ' Set up anything this room needs.
    bg = RGB(20, 20, 50)
UPDATE
    ' Update runs every frame while this room is active.
    CLS bg

    COLOR RGB(100, 255, 200)
    TEXTSIZE 2
    TEXT 120, 60, "ROOMS DEMO"
    TEXTSIZE 1

    COLOR RGB(200, 200, 200)
    TEXT 90, 110, "Three rooms stitched together"
    TEXT 100, 130, "with GOTOROOM and WAITFRAME."

    COLOR RGB(255, 255, 100)
    TEXT 120, 180, "Press A to start"
    TEXT 120, 200, "Press START to quit"

    IF BUTTONDOWN(A) THEN
        GOTOROOM "play"
    ENDIF
    IF BUTTONDOWN(START) THEN END
END UPDATE
END ROOM