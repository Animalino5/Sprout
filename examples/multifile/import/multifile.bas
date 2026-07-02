' multifile.bas: Multi-file Projects with IMPORT
' Demonstrates: IMPORT, splitting code across files
'
' File structure:
'   multifile.bas = main file (this one)
'   lib/helpers.bas = helper functions
'   lib/entities.bas = entity arrays and spawning
'
' IMPORT works like C's #include, the file's contents are pasted in.
' All variables and functions are global across files.

IMPORT "lib/helpers.bas"
IMPORT "lib/entities.bas"

' Game state
state = 0
frame = 0

WHILE TRUE
    frame = frame + 1
    CLS RGB(15, 15, 30)

    IF state = 0 THEN
        ' Menu
        COLOR RGB(100, 255, 200)
        TEXTSIZE 2
        TEXT 60, 60, "MULTI-FILE"
        TEXTSIZE 1
        COLOR RGB(200, 200, 200)
        TEXT 80, 100, "Code split across 3 files"
        TEXT 50, 130, "main.bas + lib/helpers.bas + lib/entities.bas"
        COLOR RGB(255, 255, 100)
        TEXT 100, 170, "Press A to start"

        IF BUTTONDOWN(A) THEN
            state = 1
            RESET_ENTITIES()
        ENDIF

    ELSEIF state = 1 THEN
        ' Playing: spawn dots and collect them
        IF frame MOD 30 = 0 THEN
            SPAWN_DOT(50 + RND(300), 50 + RND(180))
        ENDIF

        UPDATE_DOTS()

        ' Player
        COLOR RGB(100, 255, 100)
        CIRCLE 200, 120, 10, FILL

        ' Check collection
        collected = CHECK_COLLECTION(200, 120)
        IF collected > 0 THEN
            score = score + collected
        ENDIF

        ' Draw
        DRAW_DOTS()

        ' HUD
        COLOR RGB(255, 255, 255)
        TEXT 5, 225, "Dots: " + STR$(COUNT_DOTS())

        IF BUTTONDOWN(START) THEN END

    ENDIF

    WAITFRAME
WEND
