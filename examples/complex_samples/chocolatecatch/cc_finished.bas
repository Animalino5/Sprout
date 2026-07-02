'  CATCH THE CHOCOLATE
'  Sprout example
'
' Controls:
'   Touch — move the hand
'   A — start / restart
'   START — quit
'
' How to play:
'   Bounce the falling chocolate with your hand.
'   Aim it into the mouth at the top of the screen.
'   Each chocolate eaten = 1 point.
'   Drop 3 chocolates = game over!
'   Every 5 points = difficulty increases (faster chocolate).

'  Load assets 
mouth = LOADIMAGE("mouth.png")
catchhand = LOADIMAGE("hand.png")
titlelogo = LOADIMAGE("logo.png")
chocolate = LOADIMAGE("choco.png")
bgm = LOADMUSIC("bgm.pcm")
eat = LOADSOUND("eatsound.wav")
miss = LOADSOUND("miss.wav")

'  Config 
PLAYMUSIC bgm
VOLMUSIC 35

'  Game state 
' 0=menu, 1=playing, 2=gameover
state = 0
choco_score = 0
high_score = 0
lives = 3
combo = 0
best_combo = 0
difficulty = 1
frame = 0
mouth_cd = 0

'  Load high score 
IF EXISTS("choco_hi.dat") THEN
    OPENR "choco_hi.dat"
    high_score = READNUM()
    CLOSE
ENDIF

'  Chocolate physics
choco_xpos = 250.0
choco_ypos = 100.0
choco_xvel = 3.0
choco_yvel = 0.0
choco_spin = 0

'  Mouth position
mouth_xpos = 128
mouth_ypos = 0

'  Touch / hand position 
tx = 130
ty = 180

'  Particle arrays for effects 
DIM pt_x(24)
DIM pt_y(24)
DIM pt_vx(24)
DIM pt_vy(24)
DIM pt_life(24)
DIM pt_color(24)

' 
'  FUNCTIONS
' 

FUNCTION SPAWN_PARTICLE(x, y, vx, vy, life, col)
    FOR i = 0 TO 23
        IF pt_life(i) <= 0 THEN
            pt_x(i) = x
            pt_y(i) = y
            pt_vx(i) = vx
            pt_vy(i) = vy
            pt_life(i) = life
            pt_color(i) = col
            RETURN 0
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION UPDATE_PARTICLES()
    FOR i = 0 TO 23
        IF pt_life(i) > 0 THEN
            pt_x(i) = pt_x(i) + pt_vx(i)
            pt_y(i) = pt_y(i) + pt_vy(i)
            pt_vy(i) = pt_vy(i) + 0.2
            pt_life(i) = pt_life(i) - 1
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION DRAW_PARTICLES()
    FOR i = 0 TO 23
        IF pt_life(i) > 0 THEN
            COLOR pt_color(i)
            r = 2
            IF pt_life(i) < 10 THEN r = 1
            CIRCLE pt_x(i), pt_y(i), r, FILL
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION RESET_GAME()
    choco_score = 0
    lives = 3
    combo = 0
    best_combo = 0
    difficulty = 1
    choco_xpos = 250.0
    choco_ypos = 100.0
    choco_xvel = 3.0
    choco_yvel = 0.0
    choco_spin = 0
    mouth_xpos = 128
    mouth_cd = 0
    FOR i = 0 TO 23
        pt_life(i) = 0
    NEXT
    RETURN 0
END FUNCTION

FUNCTION RESET_CHOCOLATE()
    choco_ypos = 100.0
    choco_xpos = 50.0 + RND(220)
    choco_yvel = 0.0
    choco_xvel = (RNDF() - 0.5) * 6.0 * difficulty
    IF ABS(choco_xvel) < 2 THEN choco_xvel = 3.0 * difficulty
    choco_spin = 0
    RETURN 0
END FUNCTION

FUNCTION SPAWN_CHOCO_PARTICLES(x, y, col, count)
    FOR i = 0 TO count - 1
        ang = RNDF() * 6.283
        spd = 1 + RNDF() * 3
        SPAWN_PARTICLE(x, y, COS(ang) * spd, SIN(ang) * spd - 2, 20 + RND(15), col)
    NEXT
    RETURN 0
END FUNCTION

' 
'  MAIN LOOP
' 

WHILE TRUE
    frame = frame + 1

    '  Update 
    IF state = 0 THEN
        ' Menu
        IF BUTTONDOWN(A) THEN
            RESET_GAME()
            state = 1
        ENDIF

    ELSEIF state = 1 THEN
        ' Playing
        IF BUTTONDOWN(START) THEN END

        ' Input — touch controls the hand
        IF TOUCHDOWN() THEN
            GETTOUCH tx, ty
        ENDIF

        ' Physics
        choco_yvel = choco_yvel + 0.4 + difficulty * 0.05
        IF choco_yvel > 12 THEN choco_yvel = 12.0
        choco_ypos = choco_ypos + choco_yvel
        choco_xpos = choco_xpos + choco_xvel
        choco_spin = choco_spin + choco_xvel * 2

        ' Wall bouncing
        IF choco_xpos < 0 THEN
            choco_xpos = 0.0
            choco_xvel = ABS(choco_xvel)
        ENDIF
        IF choco_xpos > 320 - IMAGEW(chocolate) THEN
            choco_xpos = 320 - IMAGEW(chocolate)
            choco_xvel = -ABS(choco_xvel)
        ENDIF

        ' Hand collision — bounce up
        IF choco_xpos + IMAGEW(chocolate) > tx AND choco_xpos < tx + IMAGEW(catchhand) AND choco_ypos + IMAGEH(chocolate) > ty AND choco_ypos < ty + IMAGEH(catchhand) THEN
            IF choco_yvel > 0 THEN
                choco_yvel = -8.0 - difficulty * 0.3
                choco_xvel = choco_xvel * 0.85 + (RNDF() - 0.5) * 2.0
                choco_ypos = ty - IMAGEH(chocolate) - 1.0
                ' Small particle burst on bounce
                SPAWN_CHOCO_PARTICLES(choco_xpos + IMAGEW(chocolate) / 2, choco_ypos + IMAGEH(chocolate), RGB(200, 150, 50), 3)
            ENDIF
        ENDIF

        ' Mouth collision — score!
        IF mouth_cd > 0 THEN mouth_cd = mouth_cd - 1
        IF mouth_cd = 0 THEN
            IF choco_xpos + IMAGEW(chocolate) > mouth_xpos AND choco_xpos < mouth_xpos + IMAGEW(mouth) AND choco_ypos + IMAGEH(chocolate) > mouth_ypos AND choco_ypos < mouth_ypos + IMAGEH(mouth) THEN
                choco_score = choco_score + 1
                combo = combo + 1
                IF combo > best_combo THEN best_combo = combo
                PLAYSOUND eat
                mouth_cd = 20
                ' Celebration particles
                SPAWN_CHOCO_PARTICLES(mouth_xpos + 32, mouth_ypos + 32, RGB(255, 200, 0), 8)
                ' Move mouth to a new position
                mouth_xpos = 20 + RND(220)
                ' Reset chocolate
                RESET_CHOCOLATE()
                ' Increase difficulty every 5 points
                IF choco_score MOD 5 = 0 THEN
                    difficulty = difficulty + 1
                ENDIF
            ENDIF
        ENDIF

        ' Missed — fell off bottom
        IF choco_ypos > 240 THEN
            lives = lives - 1
            combo = 0
            PLAYSOUND miss
            ' Sad particles
            SPAWN_CHOCO_PARTICLES(160, 220, RGB(100, 100, 100), 5)
            IF lives <= 0 THEN
                state = 2
                IF choco_score > high_score THEN
                    high_score = choco_score
                    OPENW "choco_hi.dat"
                    WRITE high_score
                    CLOSE
                ENDIF
            ELSE
                RESET_CHOCOLATE()
            ENDIF
        ENDIF

        UPDATE_PARTICLES()

    ELSEIF state = 2 THEN
        ' Game over
        IF BUTTONDOWN(A) THEN
            RESET_GAME()
            state = 1
        ENDIF
        UPDATE_PARTICLES()
    ENDIF

    '  Draw top screen: score and stats 
    SETSCREEN 0
    CLS RGB(235, 119, 229)

    IF state = 0 THEN
        ' Menu
        DRAWIMAGE titlelogo, 0, 0
    ELSEIF state = 1 THEN
        ' Score display
        TEXTSIZE 2
        COLOR RGB(100, 50, 100)
        TEXT 110, 20, "CHOCO CATCH"
        TEXTSIZE 3
        COLOR RGB(255, 255, 255)
        TEXT 155, 70, STR$(choco_score)
        TEXTSIZE 1
        COLOR RGB(100, 50, 100)
        TEXT 140, 110, "chocolates eaten"

        ' Lives
        COLOR RGB(200, 50, 50)
        TEXT 10, 140, "Lives: " + STR$(lives)

        ' Combo
        IF combo > 1 THEN
            COLOR RGB(255, 200, 0)
            TEXTSIZE 2
            TEXT 180, 140, "COMBO x" + STR$(combo)
            TEXTSIZE 1
        ENDIF

        ' Difficulty
        COLOR RGB(150, 100, 150)
        TEXT 10, 160, "Level: " + STR$(difficulty)
        TEXT 10, 175, "Best combo: " + STR$(best_combo)
        TEXT 10, 195, "High: " + STR$(high_score)

        ' Tips
        COLOR RGB(200, 150, 200)
        TEXT 200, 200, "Bounce it"
        TEXT 200, 215, "into mouth!"
    ELSEIF state = 2 THEN
        ' Game over
        TEXTSIZE 2
        COLOR RGB(200, 50, 50)
        TEXT 100, 60, "GAME OVER"
        TEXTSIZE 1
        COLOR RGB(255, 255, 255)
        TEXT 115, 100, "Score: " + STR$(choco_score)
        TEXT 105, 120, "Best combo: " + STR$(best_combo)
        TEXT 110, 140, "High score: " + STR$(high_score)
        IF choco_score = high_score AND choco_score > 0 THEN
            TEXTSIZE 2
            COLOR RGB(255, 200, 0)
            TEXT 95, 170, "NEW HIGH!"
            TEXTSIZE 1
        ENDIF
        COLOR RGB(200, 200, 200)
        TEXT 95, 210, "Press A to restart"
    ENDIF

    '  Draw bottom screen: game 
    SETSCREEN 1
    CLS RGB(255, 181, 251)

    IF state = 1 THEN
        ' Game area
        DRAW_PARTICLES()
        ' Mouth at top (moves after each catch!)
        DRAWIMAGE mouth, mouth_xpos, mouth_ypos
        ' Chocolate with spin
        DRAWIMAGEEX chocolate, choco_xpos, choco_ypos, 100, choco_spin
        ' Hand (on top)
        DRAWIMAGE catchhand, tx, ty

        ' Lives indicator at bottom
        COLOR RGB(200, 50, 50)
        FOR l = 0 TO lives - 1
            CIRCLE 10 + l * 20, 230, 6, FILL
        NEXT

    ELSEIF state = 0 THEN
        ' Menu on bottom screen too
        COLOR RGB(255, 255, 255)
        TEXTSIZE 1
        TEXT 55, 120, "Bounce chocolate into mouth!"
        TEXT 60, 145, "Don't drop 3!"
        TEXT 50, 175, "Touch to move hand"
        COLOR RGB(255, 255, 100)
        TEXT 65, 200, "Press A to start"
    ENDIF

    WAITFRAME
WEND
