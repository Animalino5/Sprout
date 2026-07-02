' 06_spritesheets.bas — Animated Sprite Sheets
' Demonstrates: LOADSHEET, DRAWSHEET (manual), DRAWSHEET (auto-animate with FPS)
'
' Load a 4-frame animation sheet

walk = LOADSHEET("walk.png", 16, 16)

' Manual frame counter
manual_frame = 0
manual_timer = 0

WHILE TRUE
    CLS RGB(20, 20, 40)

    ' Manual animation (count frames yourself)
    manual_timer = manual_timer + 1
    IF manual_timer >= 8 THEN
        manual_timer = 0
        manual_frame = (manual_frame + 1) MOD 4
    ENDIF
    DRAWSHEET walk, 50, 100, manual_frame

    COLOR RGB(255, 255, 255)
    TEXT 30, 80, "Manual (frame " + STR$(manual_frame) + ")"

    ' Auto-animation at 8 FPS
    DRAWSHEET walk, 180, 100, 0, 8

    TEXT 160, 80, "Auto 8 FPS"

    ' Auto-animation at 16 FPS (faster)
    DRAWSHEET walk, 310, 100, 0, 16

    TEXT 290, 80, "Auto 16 FPS"

    ' Show all 4 frames at once
    COLOR RGB(255, 255, 255)
    TEXT 10, 150, "All frames:"
    DRAWSHEET walk, 100, 160, 0
    DRAWSHEET walk, 130, 160, 1
    DRAWSHEET walk, 160, 160, 2
    DRAWSHEET walk, 190, 160, 3

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
