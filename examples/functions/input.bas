' 03_input.bas: Input: Buttons, Touch, and Circle Pad
' Demonstrates: BUTTON, BUTTONDOWN, TOUCHDOWN, GETTOUCH, GETCIRCLEPAD

tx = 0
ty = 0
dx = 0
dy = 0

WHILE TRUE
    CLS RGB(20, 20, 50)

    ' Button held
    COLOR RGB(255, 255, 255)
    TEXT 10, 10, "Buttons held:"
    by = 25
    IF BUTTON(UP) THEN TEXT 10, by, "UP" : by = by + 12
    IF BUTTON(DOWN) THEN TEXT 10, by, "DOWN" : by = by + 12
    IF BUTTON(LEFT) THEN TEXT 10, by, "LEFT" : by = by + 12
    IF BUTTON(RIGHT) THEN TEXT 10, by, "RIGHT" : by = by + 12
    IF BUTTON(A) THEN TEXT 10, by, "A" : by = by + 12
    IF BUTTON(B) THEN TEXT 10, by, "B" : by = by + 12
    IF BUTTON(X) THEN TEXT 10, by, "X" : by = by + 12
    IF BUTTON(Y) THEN TEXT 10, by, "Y" : by = by + 12
    IF BUTTON(L) THEN TEXT 10, by, "L" : by = by + 12
    IF BUTTON(R) THEN TEXT 10, by, "R" : by = by + 12

    ' Button pressed
    IF BUTTONDOWN(START) THEN END
    IF BUTTONDOWN(SELECT) THEN
        COLOR RGB(255, 255, 0)
        TEXT 100, 10, "SELECT PRESSED!"
    ENDIF

    ' Touch
    IF TOUCHDOWN() THEN
        GETTOUCH tx, ty
        COLOR RGB(255, 100, 100)
        CIRCLE tx, ty, 10, FILL
        COLOR RGB(255, 255, 255)
        TEXT 100, 30, "Touch: " + STR$(tx) + "," + STR$(ty)
    ELSE
        COLOR RGB(100, 100, 100)
        TEXT 100, 30, "No touch"
    ENDIF

    ' Circle Pad
    GETCIRCLEPAD dx, dy
    COLOR RGB(100, 255, 100)
    TEXT 100, 50, "Circle: " + STR$(dx) + "," + STR$(dy)

    ' Draw circle pad as a visual
    cx = 200
    cy = 120
    COLOR RGB(60, 60, 60)
    CIRCLE cx, cy, 40, FILL
    COLOR RGB(100, 255, 100)
    CIRCLE cx + dx / 4, cy - dy / 4, 8, FILL

    WAITFRAME
WEND
