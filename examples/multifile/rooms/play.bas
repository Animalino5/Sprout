' This is the gameplay room!
'
ROOM "play"
    ' Reset player to center and start the timer.
    px = 200
    py = 120
    play_start = MSECONDS()
UPDATE
    CLS RGB(10, 30, 10)

    ' Move the dot
    IF BUTTON(LEFT)  THEN px = px - 3
    IF BUTTON(RIGHT) THEN px = px + 3
    IF BUTTON(UP)    THEN py = py - 3
    IF BUTTON(DOWN)  THEN py = py + 3

    ' Keep it on screen
    IF px < 5 THEN px = 5
    IF px > 395 THEN px = 395
    IF py < 5 THEN py = 5
    IF py > 235 THEN py = 235

    ' Hit the edge = game over
    IF px <= 8 OR px >= 392 OR py <= 8 OR py >= 232 THEN
        play_time = MSECONDS() - play_start
        GOTOROOM "gameover"
    ENDIF

    ' Draw the player dot and a border (the "danger zone")
    COLOR RGB(255, 80, 80)
    RECT 5, 5, 390, 230           ' border
    COLOR RGB(100, 255, 100)
    CIRCLE px, py, 6, FILL

    ' HUD
    COLOR RGB(255, 255, 255)
    TEXT 5, 5, "Stay inside!  Edge = game over."
END UPDATE
END ROOM

