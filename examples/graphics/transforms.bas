' transforms.bas: TRANSLATE, ROTATE, RESIZE
'
' These three statements change WHERE and HOW everything you draw
' afterwards appears on screen. They are GLOBAL state, once set,
' every draw call (RECT, CIRCLE, LINE, TEXT, DRAWIMAGE, ...) is
' affected until you change them again.
'
'   TRANSLATE x, y    Shift the origin by (x, y) pixels
'   ROTATE   angle    Spin by 'angle' degrees (clockwise, 0..360)
'   RESIZE   sx, sy   Scale by sx% and sy%  (100 = original size)
'
' Reset to default with:  TRANSLATE 0, 0 : ROTATE 0 : RESIZE 100, 100
'
' This example shows all three side by side. The dark hollow square
' in each card is the shape's "home" position. The colored solid
' square shows what the transform does to it.

t = 0

WHILE TRUE
    CLS RGB(10, 10, 30)
    t = t + 1

    ' Draw card frames and labels (no transform)
    COLOR RGB(90, 90, 90)
    RECT 10,  20, 120, 200      ' Card 1 frame
    RECT 140, 20, 120, 200      ' Card 2 frame
    RECT 270, 20, 120, 200      ' Card 3 frame

    COLOR RGB(255, 255, 255)
    TEXT 22, 28, "TRANSLATE"
    TEXT 178, 28, "ROTATE"
    TEXT 308, 28, "RESIZE"

    ' Reference squares (hollow), where the shape would be with NO transform applied.
    COLOR RGB(70, 70, 70)
    RECT 55, 105, 30, 30        ' Card 1 home
    RECT 185, 105, 30, 30       ' Card 2 home
    RECT 315, 105, 30, 30       ' Card 3 home

    ' Card 1: TRANSLATE moves the square in a circle
    ' The RECT is still drawn at (55, 105), but TRANSLATE adds
    ' an offset to everything that follows.
    COLOR RGB(255, 100, 100)
    off_x = INT(SIN(t * 0.05) * 30)
    off_y = INT(COS(t * 0.05) * 30)
    TRANSLATE off_x, off_y
    RECT 55, 105, 30, 30, FILL

    ' Reset before the next card!
    TRANSLATE 0, 0

    ' Card 2: ROTATE spins the square in place
    ' First TRANSLATE to the card's center (200, 120), then
    ' ROTATE, then draw the square centered on (0, 0) so it
    ' spins around its own middle.
    COLOR RGB(100, 255, 100)
    angle = (t * 2) MOD 360
    TRANSLATE 200, 120
    ROTATE angle
    RECT -15, -15, 30, 30, FILL

    ' Reset before the next card!
    TRANSLATE 0, 0
    ROTATE 0

    ' Card 3: RESIZE pulses the square bigger/smaller
    ' Same trick: TRANSLATE to the card center, RESIZE, then
    ' draw centered on (0, 0) so it scales around its middle.
    COLOR RGB(100, 100, 255)
    pulse = 80 + INT(SIN(t * 0.06) * 40)   ' 40% .. 120%
    TRANSLATE 330, 120
    RESIZE pulse, pulse
    RECT -15, -15, 30, 30, FILL

    ' Reset for next frame
    TRANSLATE 0, 0
    ROTATE 0
    RESIZE 100, 100

    ' Footer
    COLOR RGB(200, 200, 200)
    TEXT 10, 225, "Hollow = home position.  Solid = transformed.  START to quit."

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
