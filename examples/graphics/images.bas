' images.bas: Loading and Drawing Images
' Demonstrates: LOADIMAGE, DRAWIMAGE, DRAWIMAGEEX, IMAGEW, IMAGEH
'
'
player = LOADIMAGE("player.png")
coin = LOADIMAGE("coin.png")

x = 100
y = 100
angle = 0
scale = 100

WHILE TRUE
    CLS RGB(30, 30, 60)

    ' Move with D-pad
    IF BUTTON(LEFT) THEN x = x - 3
    IF BUTTON(RIGHT) THEN x = x + 3
    IF BUTTON(UP) THEN y = y - 3
    IF BUTTON(DOWN) THEN y = y + 3

    ' Scale with L/R buttons
    IF BUTTON(L) THEN scale = scale - 2
    IF BUTTON(R) THEN scale = scale + 2
    IF scale < 20 THEN scale = 20
    IF scale > 300 THEN scale = 300

    ' Rotate continuously
    angle = (angle + 2) MOD 360

    ' Draw a static coin
    DRAWIMAGE coin, 50, 50

    ' Draw player normal
    DRAWIMAGE player, x, y

    ' Draw player scaled and rotated (right side)
    DRAWIMAGEEX player, 280, 100, scale, angle

    ' Show image dimensions
    COLOR RGB(255, 255, 255)
    TEXT 5, 5, "Image size: " + STR$(IMAGEW(player)) + "x" + STR$(IMAGEH(player))
    TEXT 5, 20, "Scale: " + STR$(scale) + "%  Angle: " + STR$(angle)

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
