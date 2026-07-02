' dualscreen.bas: Dual Screen Drawing
' Demonstrates: SETSCREEN 0 (top), SETSCREEN 1 (bottom)
'
' The 3DS has two screens:
'   Top screen (SETSCREEN 0): 400 x 240
'   Bottom screen (SETSCREEN 1): 320 x 240 (touch)

frame = 0
tx = 0.0
ty = 0.0

WHILE TRUE
    frame = frame + 1

    ' Top screen
    SETSCREEN 0
    CLS RGB(10, 10, 40)

    ' Animated title
    hue = (frame / 2) MOD 360
    r = 100 + SIN(frame * 0.05) * 100
    g = 100 + SIN(frame * 0.05 + 2) * 100
    b = 100 + SIN(frame * 0.05 + 4) * 100
    COLOR RGB(r, g, b)
    TEXTSIZE 3
    TEXT 100, 80, "TOP SCREEN"
    TEXTSIZE 1

    ' Bouncing ball on top screen
    bx = 200 + SIN(frame * 0.03) * 150
    by = 120 + COS(frame * 0.04) * 60
    COLOR RGB(255, 200, 0)
    CIRCLE bx, by, 15, FILL

    ' Stats
    COLOR RGB(150, 150, 200)
    TEXT 5, 5, "400x240 — Top"
    TEXT 5, 225, "Frame: " + STR$(frame)

    ' Bottom screen
    SETSCREEN 1
    CLS RGB(40, 10, 40)

    ' Touch-reactive area
    IF TOUCHDOWN() THEN
        GETTOUCH tx, ty
        COLOR RGB(255, 100, 255)
        CIRCLE tx, ty, 20, FILL
        COLOR RGB(255, 255, 255)
        CIRCLE tx, ty, 8, FILL
    ENDIF

    ' Draw a grid
    COLOR RGB(80, 30, 80)
    FOR gx = 0 TO 320 STEP 40
        LINE gx, 0, gx, 240
    NEXT
    FOR gy = 0 TO 240 STEP 40
        LINE 0, gy, 320, gy
    NEXT

    ' Label
    COLOR RGB(200, 150, 200)
    TEXTSIZE 2
    TEXT 60, 100, "BOTTOM"
    TEXTSIZE 1
    TEXT 70, 120, "Touch me!"
    TEXT 5, 5, "320x240 — Bottom (touch)"

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
