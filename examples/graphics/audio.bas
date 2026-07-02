' audio.bas: Sound Effects and Music
' Demonstrates: LOADSOUND, PLAYSOUND, STOPSOUND, LOADMUSIC, PLAYMUSIC, VOLMUSIC
'
' Convert music: ffmpeg -i song.wav -f s16le -ar 44100 -ac 2 bgm.pcm

shoot = LOADSOUND("shoot.wav")
bgm = LOADMUSIC("bgm.pcm")

PLAYMUSIC bgm
vol = 50
VOLMUSIC vol

WHILE TRUE
    CLS RGB(30, 30, 50)

    COLOR RGB(255, 255, 255)
    TEXTSIZE 2
    TEXT 80, 20, "AUDIO DEMO"
    TEXTSIZE 1

    TEXT 10, 60, "A — Play shoot sound"
    TEXT 10, 80, "B — Stop all sounds"
    TEXT 10, 100, "UP/DOWN — Music volume"
    TEXT 10, 120, "LEFT — Pause music"
    TEXT 10, 140, "RIGHT — Resume music"
    TEXT 10, 160, "START — Quit"

    ' Controls
    IF BUTTONDOWN(A) THEN PLAYSOUND shoot
    IF BUTTONDOWN(B) THEN STOPSOUND shoot

    IF BUTTON(UP) THEN vol = vol + 2
    IF BUTTON(DOWN) THEN vol = vol - 2
    IF vol < 0 THEN vol = 0
    IF vol > 100 THEN vol = 100
    VOLMUSIC vol

    IF BUTTONDOWN(LEFT) THEN PAUSEMUSIC
    IF BUTTONDOWN(RIGHT) THEN RESUMEMUSIC

    ' Display
    COLOR RGB(100, 255, 100)
    TEXT 10, 190, "Volume: " + STR$(vol) + "%"

    ' Volume bar
    RECT 10, 205, 100, 8, FILL
    COLOR RGB(100, 200, 100)
    RECT 10, 205, vol, 8, FILL

    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
