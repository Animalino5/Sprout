' drawing.bas: Shapes, Colors, and Text
' Demonstrates: CLS, COLOR, RGB, CIRCLE, RECT, LINE, TRIANGLE, ELLIPSE, TEXT, TEXTSIZE

WHILE TRUE
    CLS RGB(10, 10, 30)

    '  Filled shapes 
    COLOR RGB(255, 80, 80)
    CIRCLE 60, 60, 25, FILL

    COLOR RGB(80, 255, 80)
    RECT 110, 35, 50, 50, FILL

    COLOR RGB(80, 80, 255)
    TRIANGLE 220, 35, 270, 35, 245, 85, FILL

    COLOR RGB(255, 200, 0)
    ELLIPSE 320, 60, 50, 30, FILL

    '  Outlined shapes 
    COLOR RGB(255, 255, 255)
    CIRCLE 60, 140, 25
    RECT 110, 115, 50, 50
    TRIANGLE 220, 115, 270, 115, 245, 165

    '  Lines 
    COLOR RGB(255, 100, 255)
    LINE 10, 200, 380, 200
    LINE 10, 220, 380, 210

    '  Text sizes 
    TEXTSIZE 1
    COLOR RGB(200, 200, 200)
    TEXT 5, 5, "Small text"

    TEXTSIZE 2
    COLOR RGB(100, 255, 200)
    TEXT 100, 5, "Medium"

    TEXTSIZE 3
    COLOR RGB(255, 255, 100)
    TEXT 220, 5, "BIG"

    TEXTSIZE 1

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
