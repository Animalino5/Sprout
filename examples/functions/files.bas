' files.bas: Save and Load Data
' Demonstrates: OPENW, WRITE, OPENR, READNUM, READLINE$, CLOSE, EXISTS

CLS RGB(20, 20, 50)
COLOR RGB(255, 255, 255)
y = 10

' Try loading save data
IF EXISTS("demo_save.dat") THEN
    OPENR "demo_save.dat"
    saved_score = READNUM()
    saved_name$ = READLINE$()
    saved_level = READNUM()
    CLOSE
    TEXT 10, y, "Save found!" : y = y + 12
    TEXT 10, y, "  Score: " + STR$(saved_score) : y = y + 12
    TEXT 10, y, "  Name: " + saved_name$ : y = y + 12
    TEXT 10, y, "  Level: " + STR$(saved_level) : y = y + 16
ELSE
    TEXT 10, y, "No save file found." : y = y + 16
    saved_score = 0
    saved_name$ = "NewPlayer"
    saved_level = 1
ENDIF

TEXT 10, y, "Writing new save data..." : y = y + 12

' Write save data
OPENW "demo_save.dat"
WRITE 42
WRITE "Hero"
WRITE 5
CLOSE

' Read it back to verify
OPENR "demo_save.dat"
check_score = READNUM()
check_name$ = READLINE$()
check_level = READNUM()
CLOSE

COLOR RGB(100, 255, 100)
TEXT 10, y, "Verified:" : y = y + 12
TEXT 10, y, "  Score: " + STR$(check_score) : y = y + 12
TEXT 10, y, "  Name: " + check_name$ : y = y + 12
TEXT 10, y, "  Level: " + STR$(check_level) : y = y + 16

COLOR RGB(255, 255, 100)
TEXT 10, y, "Press START to exit."

WHILE TRUE
    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
