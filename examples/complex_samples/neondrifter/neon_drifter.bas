'  NEON DRIFTER 
'  Sprout example
' 
'
' Controls:
'   D-pad — move
'   A — shoot
'   B — dash (brief invincibility)
'   START — pause / quit
'
' Assets needed in assets/ folder:
'   player.png   — sprite sheet (2 frames of 16x16, total 32x16)
'   shoot.wav    — shoot sound effect
'   hit.wav      — explosion/hit sound
'   bgm.pcm      — background music (raw stereo PCM)

'  Load assets 
player_sheet = LOADSHEET("player.png", 16, 16)
shoot_snd = LOADSOUND("shoot.wav")
hit_snd = LOADSOUND("hit.wav")
bgm = LOADMUSIC("bgm.pcm")

'  Load high score from save file 
high_score = 0
IF EXISTS("neon_hi.dat") THEN
    OPENR "neon_hi.dat"
    high_score = READNUM()
    CLOSE
ENDIF

'  Start music 
PLAYMUSIC bgm
VOLMUSIC 80

'  Game state 
state = 0          ' 0=menu, 1=playing, 2=gameover, 3=paused
score = 0
wave = 1
wave_timer = 0
enemies_to_spawn = 0
spawn_timer = 0
shake = 0
frame = 0

'  Player 
px = 200
py = 120
phealth = 100
pdash_timer = 0
pdash_cd = 0
pshoot_cd = 0
pinvincible = 0

'  Entity arrays 
DIM bx(32)
DIM by(32)
DIM bvx(32)
DIM bvy(32)
DIM blife(32)
DIM btype(32)

DIM ex(24)
DIM ey(24)
DIM ehealth(24)
DIM etype(24)
DIM ealive(24)
DIM etimer(24)

DIM pt_x(48)
DIM pt_y(48)
DIM pt_vx(48)
DIM pt_vy(48)
DIM pt_life(48)
DIM pt_color(48)

' 
'  FUNCTIONS (now with array parameters!)
' 

FUNCTION DIST2(x1, y1, x2, y2)
    dx = x2 - x1
    dy = y2 - y1
    RETURN SQR(dx*dx + dy*dy)
END FUNCTION

' Find a free slot — now uses array parameter! No more inlining!
FUNCTION FIND_FREE(arr(), max_count)
    FOR i = 0 TO max_count - 1
        IF arr(i) = 0 THEN RETURN i
    NEXT
    RETURN -1
END FUNCTION

FUNCTION SPAWN_BULLET(x, y, vx, vy, life, kind)
    idx = FIND_FREE(blife, 32)
    IF idx < 0 THEN RETURN 0
    bx(idx) = x
    by(idx) = y
    bvx(idx) = vx
    bvy(idx) = vy
    blife(idx) = life
    btype(idx) = kind
    RETURN 1
END FUNCTION

FUNCTION SPAWN_ENEMY(x, y, kind)
    idx = FIND_FREE(ealive, 24)
    IF idx < 0 THEN RETURN 0
    ex(idx) = x
    ey(idx) = y
    ealive(idx) = 1
    etype(idx) = kind
    etimer(idx) = 0
    IF kind = 0 THEN ehealth(idx) = 1
    IF kind = 1 THEN ehealth(idx) = 2
    IF kind = 2 THEN ehealth(idx) = 5
    IF kind = 3 THEN ehealth(idx) = 30
    RETURN 1
END FUNCTION

FUNCTION SPAWN_PARTICLE(x, y, vx, vy, life, col)
    idx = FIND_FREE(pt_life, 48)
    IF idx < 0 THEN RETURN 0
    pt_x(idx) = x
    pt_y(idx) = y
    pt_vx(idx) = vx
    pt_vy(idx) = vy
    pt_life(idx) = life
    pt_color(idx) = col
    RETURN 1
END FUNCTION

FUNCTION EXPLODE(x, y, col, count)
    FOR i = 0 TO count - 1
        ang = RNDF() * 6.283
        spd = 1 + RNDF() * 3
        SPAWN_PARTICLE(x, y, COS(ang) * spd, SIN(ang) * spd, 20 + RND(20), col)
    NEXT
    RETURN 0
END FUNCTION

FUNCTION ANGLE_TO(x1, y1, x2, y2)
    RETURN ATAN2(y2 - y1, x2 - x1)
END FUNCTION

' 
'  GAME LOGIC
' 

FUNCTION RESET_GAME()
    px = 200
    py = 120
    phealth = 100
    pdash_timer = 0
    pdash_cd = 0
    pshoot_cd = 0
    pinvincible = 0
    score = 0
    wave = 1
    wave_timer = 60
    enemies_to_spawn = 0
    spawn_timer = 0
    shake = 0
    FOR i = 0 TO 31
        blife(i) = 0
    NEXT
    FOR i = 0 TO 23
        ealive(i) = 0
    NEXT
    FOR i = 0 TO 47
        pt_life(i) = 0
    NEXT
    RETURN 0
END FUNCTION

FUNCTION START_WAVE()
    IF wave MOD 5 = 0 THEN
        SPAWN_ENEMY(200, 40, 3)
    ELSE
        enemies_to_spawn = 3 + wave
        spawn_timer = 0
    ENDIF
    RETURN 0
END FUNCTION

FUNCTION UPDATE_PLAYER()
    move_x = 0
    move_y = 0
    IF BUTTON(LEFT) THEN move_x = -1
    IF BUTTON(RIGHT) THEN move_x = 1
    IF BUTTON(UP) THEN move_y = -1
    IF BUTTON(DOWN) THEN move_y = 1

    IF move_x <> 0 AND move_y <> 0 THEN
        move_x = move_x * 7 / 10
        move_y = move_y * 7 / 10
    ENDIF

    IF pdash_cd > 0 THEN pdash_cd = pdash_cd - 1
    IF pdash_timer > 0 THEN
        pdash_timer = pdash_timer - 1
        pinvincible = 5
    ENDIF
    IF BUTTONDOWN(B) AND pdash_cd = 0 THEN
        pdash_timer = 8
        pdash_cd = 40
        pinvincible = 8
        IF move_x = 0 AND move_y = 0 THEN move_x = 1
        EXPLODE(px, py, RGB(100, 200, 255), 6)
    ENDIF

    speed = 2
    IF pdash_timer > 0 THEN speed = 6
    px = px + move_x * speed
    py = py + move_y * speed

    IF px < 8 THEN px = 8
    IF px > 392 THEN px = 392
    IF py < 8 THEN py = 8
    IF py > 232 THEN py = 232

    IF pshoot_cd > 0 THEN pshoot_cd = pshoot_cd - 1
    IF BUTTON(A) AND pshoot_cd = 0 THEN
        pshoot_cd = 8
        target_x = px
        target_y = py - 100
        best_dist = 99999
        FOR i = 0 TO 23
            IF ealive(i) = 1 THEN
                d = DIST2(px, py, ex(i), ey(i))
                IF d < best_dist THEN
                    best_dist = d
                    target_x = ex(i)
                    target_y = ey(i)
                ENDIF
            ENDIF
        NEXT
        ang = ANGLE_TO(px, py, target_x, target_y)
        SPAWN_BULLET(px, py, COS(ang) * 6, SIN(ang) * 6, 60, 0)
        PLAYSOUND shoot_snd
    ENDIF

    IF pinvincible > 0 THEN pinvincible = pinvincible - 1
    RETURN 0
END FUNCTION

FUNCTION UPDATE_BULLETS()
    FOR i = 0 TO 31
        IF blife(i) > 0 THEN
            bx(i) = bx(i) + bvx(i)
            by(i) = by(i) + bvy(i)
            blife(i) = blife(i) - 1
            IF bx(i) < 0 OR bx(i) > 400 OR by(i) < 0 OR by(i) > 240 THEN
                blife(i) = 0
            ENDIF
            IF btype(i) = 0 THEN
                FOR j = 0 TO 23
                    IF ealive(j) = 1 THEN
                        IF DIST2(bx(i), by(i), ex(j), ey(j)) < 10 THEN
                            ehealth(j) = ehealth(j) - 1
                            blife(i) = 0
                            IF ehealth(j) <= 0 THEN
                                ealive(j) = 0
                                PLAYSOUND hit_snd
                                IF etype(j) = 3 THEN
                                    score = score + 500
                                    EXPLODE(ex(j), ey(j), RGB(255, 100, 255), 24)
                                    shake = 12
                                ELSEIF etype(j) = 2 THEN
                                    score = score + 50
                                    EXPLODE(ex(j), ey(j), RGB(255, 200, 0), 16)
                                    shake = 6
                                ELSEIF etype(j) = 1 THEN
                                    score = score + 20
                                    EXPLODE(ex(j), ey(j), RGB(255, 150, 0), 10)
                                    shake = 3
                                ELSE
                                    score = score + 10
                                    EXPLODE(ex(j), ey(j), RGB(255, 80, 80), 8)
                                    shake = 2
                                ENDIF
                            ELSE
                                EXPLODE(bx(i), by(i), RGB(255, 255, 100), 4)
                            ENDIF
                            j = 24
                        ENDIF
                    ENDIF
                NEXT
            ELSE
                IF pinvincible = 0 THEN
                    IF DIST2(bx(i), by(i), px, py) < 8 THEN
                        phealth = phealth - 10
                        blife(i) = 0
                        pinvincible = 30
                        shake = 8
                        EXPLODE(px, py, RGB(255, 50, 50), 10)
                        IF phealth <= 0 THEN
                            state = 2
                            EXPLODE(px, py, RGB(255, 100, 100), 30)
                            shake = 20
                            IF score > high_score THEN
                                high_score = score
                                OPENW "neon_hi.dat"
                                WRITE high_score
                                CLOSE
                            ENDIF
                        ENDIF
                    ENDIF
                ENDIF
            ENDIF
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION UPDATE_ENEMIES()
    FOR i = 0 TO 23
        IF ealive(i) = 1 THEN
            etimer(i) = etimer(i) + 1
            IF etype(i) = 0 THEN
                ang = ANGLE_TO(ex(i), ey(i), px, py)
                ex(i) = ex(i) + COS(ang) * 2
                ey(i) = ey(i) + SIN(ang) * 2
                IF pinvincible = 0 AND DIST2(ex(i), ey(i), px, py) < 12 THEN
                    phealth = phealth - 5
                    pinvincible = 30
                    shake = 5
                    EXPLODE(px, py, RGB(255, 50, 50), 6)
                    IF phealth <= 0 THEN
                        state = 2
                        IF score > high_score THEN
                            high_score = score
                            OPENW "neon_hi.dat"
                            WRITE high_score
                            CLOSE
                        ENDIF
                    ENDIF
                ENDIF
            ELSEIF etype(i) = 1 THEN
                d = DIST2(ex(i), ey(i), px, py)
                ang = ANGLE_TO(ex(i), ey(i), px, py)
                IF d < 80 THEN
                    ex(i) = ex(i) - COS(ang) * 1
                    ey(i) = ey(i) - SIN(ang) * 1
                ELSEIF d > 140 THEN
                    ex(i) = ex(i) + COS(ang) * 1
                    ey(i) = ey(i) + SIN(ang) * 1
                ELSE
                    ex(i) = ex(i) + COS(ang + 2) * 1
                    ey(i) = ey(i) + SIN(ang + 2) * 1
                ENDIF
                IF etimer(i) MOD 60 = 0 THEN
                    SPAWN_BULLET(ex(i), ey(i), COS(ang) * 3, SIN(ang) * 3, 120, 1)
                ENDIF
            ELSEIF etype(i) = 2 THEN
                ang = ANGLE_TO(ex(i), ey(i), px, py)
                ex(i) = ex(i) + COS(ang) * 1
                ey(i) = ey(i) + SIN(ang) * 1
                IF pinvincible = 0 AND DIST2(ex(i), ey(i), px, py) < 14 THEN
                    phealth = phealth - 8
                    pinvincible = 30
                    shake = 6
                ENDIF
            ELSEIF etype(i) = 3 THEN
                ex(i) = 200 + COS(etimer(i) * 2) * 120
                ey(i) = 50 + SIN(etimer(i) * 3) * 20
                IF etimer(i) MOD 40 = 0 THEN
                    base_ang = ANGLE_TO(ex(i), ey(i), px, py)
                    FOR k = -2 TO 2
                        a = base_ang + k * 2 / 10
                        SPAWN_BULLET(ex(i), ey(i), COS(a) * 2, SIN(a) * 2, 200, 1)
                    NEXT
                ENDIF
            ENDIF
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION UPDATE_PARTICLES()
    FOR i = 0 TO 47
        IF pt_life(i) > 0 THEN
            pt_x(i) = pt_x(i) + pt_vx(i)
            pt_y(i) = pt_y(i) + pt_vy(i)
            pt_vx(i) = pt_vx(i) * 9 / 10
            pt_vy(i) = pt_vy(i) * 9 / 10
            pt_life(i) = pt_life(i) - 1
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION UPDATE_WAVES()
    IF enemies_to_spawn > 0 THEN
        spawn_timer = spawn_timer - 1
        IF spawn_timer <= 0 THEN
            side = RND(4)
            IF side = 0 THEN
                sx = RND(400)
                sy = -10
            ELSEIF side = 1 THEN
                sx = 410
                sy = RND(240)
            ELSEIF side = 2 THEN
                sx = RND(400)
                sy = 250
            ELSE
                sx = -10
                sy = RND(240)
            ENDIF
            kind = 0
            r = RND(100)
            IF wave >= 2 AND r < 30 THEN kind = 1
            IF wave >= 4 AND r < 15 THEN kind = 2
            SPAWN_ENEMY(sx, sy, kind)
            enemies_to_spawn = enemies_to_spawn - 1
            spawn_timer = 30 - wave
            IF spawn_timer < 10 THEN spawn_timer = 10
        ENDIF
    ELSE
        any_alive = 0
        FOR i = 0 TO 23
            IF ealive(i) = 1 THEN any_alive = 1
        NEXT
        IF any_alive = 0 THEN
            wave_timer = wave_timer - 1
            IF wave_timer <= 0 THEN
                wave = wave + 1
                wave_timer = 90
                START_WAVE()
            ENDIF
        ENDIF
    ENDIF
    RETURN 0
END FUNCTION

' 
'  DRAWING
' 

FUNCTION DRAW_PLAYER()
    IF pdash_timer > 0 THEN
        COLOR RGB(100, 200, 255)
    ELSEIF pinvincible > 0 THEN
        IF pinvincible MOD 4 < 2 THEN
            COLOR RGB(255, 255, 255)
        ELSE
            COLOR RGB(255, 100, 100)
        ENDIF
    ELSE
        COLOR RGB(100, 255, 150)
    ENDIF
    ' Use animated sprite sheet if available, else fall back to circle
    IF player_sheet >= 0 THEN
        DRAWSHEET player_sheet, px - 8, py - 8, 0, 4
    ELSE
        CIRCLE px, py, 7, FILL
        COLOR RGB(255, 255, 255)
        CIRCLE px, py, 3, FILL
    ENDIF
    RETURN 0
END FUNCTION

FUNCTION DRAW_BULLETS()
    FOR i = 0 TO 31
        IF blife(i) > 0 THEN
            IF btype(i) = 0 THEN
                ' Player bullets — green triangles
                COLOR RGB(150, 255, 150)
                TRIANGLE bx(i) - 3, by(i) + 3, bx(i) + 3, by(i) + 3, bx(i), by(i) - 3, FILL
            ELSE
                ' Enemy bullets — red circles
                COLOR RGB(255, 100, 100)
                CIRCLE bx(i), by(i), 3, FILL
            ENDIF
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION DRAW_ENEMIES()
    FOR i = 0 TO 23
        IF ealive(i) = 1 THEN
            IF etype(i) = 0 THEN
                ' Chaser — red circle
                COLOR RGB(255, 80, 80)
                CIRCLE ex(i), ey(i), 7, FILL
            ELSEIF etype(i) = 1 THEN
                ' Shooter — orange diamond (rotated rect)
                COLOR RGB(255, 150, 0)
                RECT ex(i) - 6, ey(i) - 6, 12, 12, FILL
            ELSEIF etype(i) = 2 THEN
                ' Tank — yellow big square
                COLOR RGB(255, 200, 0)
                RECT ex(i) - 10, ey(i) - 10, 20, 20, FILL
                COLOR RGB(255, 255, 255)
                FOR h = 0 TO ehealth(i) - 1
                    RECT ex(i) - 10 + h * 4, ey(i) - 14, 3, 2, FILL
                NEXT
            ELSEIF etype(i) = 3 THEN
                ' Boss — magenta ellipse
                COLOR RGB(255, 100, 255)
                ELLIPSE ex(i), ey(i), 40, 30, FILL
                COLOR RGB(255, 255, 255)
                ELLIPSE ex(i), ey(i), 20, 15, FILL
                ' Boss health bar
                RECT 50, 10, 300, 8, FILL
                COLOR RGB(255, 50, 50)
                RECT 50, 10, 300 * ehealth(i) / 30, 8, FILL
            ENDIF
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION DRAW_PARTICLES()
    FOR i = 0 TO 47
        IF pt_life(i) > 0 THEN
            COLOR pt_color(i)
            r = 2
            IF pt_life(i) < 10 THEN r = 1
            CIRCLE pt_x(i), pt_y(i), r, FILL
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION DRAW_TOP_HUD()
    COLOR RGB(255, 255, 255)
    TEXT 5, 5, "SCORE " + STR$(score)
    TEXT 200, 5, "WAVE " + STR$(wave)

    ' Health bar
    RECT 5, 225, 100, 8, FILL
    IF phealth > 0 THEN
        IF phealth > 50 THEN
            COLOR RGB(100, 255, 100)
        ELSEIF phealth > 25 THEN
            COLOR RGB(255, 200, 0)
        ELSE
            COLOR RGB(255, 50, 50)
        ENDIF
        RECT 5, 225, phealth, 8, FILL
    ENDIF

    ' Dash cooldown
    IF pdash_cd > 0 THEN
        COLOR RGB(100, 100, 100)
        TEXT 120, 222, "DASH " + STR$(pdash_cd)
    ELSE
        COLOR RGB(100, 200, 255)
        TEXT 120, 222, "DASH READY"
    ENDIF
    RETURN 0
END FUNCTION

FUNCTION DRAW_BOTTOM_SCREEN()
    '  Bottom screen: stats and info 
    SETSCREEN 1
    CLS RGB(15, 15, 30)

    ' Title
    TEXTSIZE 2
    COLOR RGB(100, 255, 200)
    TEXT 80, 10, "NEON DRIFTER"
    TEXTSIZE 1

    ' Stats
    COLOR RGB(255, 255, 255)
    TEXT 10, 40, "Score: " + STR$(score)
    TEXT 10, 55, "High:  " + STR$(high_score)
    TEXT 10, 70, "Wave:  " + STR$(wave)
    TEXT 10, 85, "HP:    " + STR$(phealth)

    ' Enemy count
    enemy_count = 0
    FOR i = 0 TO 23
        IF ealive(i) = 1 THEN enemy_count = enemy_count + 1
    NEXT
    TEXT 10, 100, "Enemies: " + STR$(enemy_count)

    ' Controls
    COLOR RGB(150, 150, 150)
    TEXT 10, 130, "D-pad: Move"
    TEXT 10, 145, "A: Shoot"
    TEXT 10, 160, "B: Dash"
    TEXT 10, 175, "START: Pause"

    ' Music indicator
    COLOR RGB(100, 200, 255)
    TEXT 10, 200, "Music: Playing"
    TEXT 10, 215, "Vol: 40%"

    ' Switch back to top screen
    SETSCREEN 0
    RETURN 0
END FUNCTION

FUNCTION DRAW_MENU()
    TEXTSIZE 3
    COLOR RGB(100, 255, 200)
    TEXT 80, 60, "NEON"
    TEXT 80, 90, "DRIFTER"
    TEXTSIZE 1
    COLOR RGB(200, 200, 200)
    TEXT 110, 130, "A Sprout showcase"
    COLOR RGB(255, 255, 100)
    TEXT 100, 155, "Press A to start"
    COLOR RGB(150, 150, 150)
    TEXT 80, 175, "D-pad: move  A: shoot  B: dash"
    RETURN 0
END FUNCTION

FUNCTION DRAW_GAMEOVER()
    COLOR RGB(0, 0, 0)
    RECT 0, 0, 400, 240, FILL
    TEXTSIZE 2
    COLOR RGB(255, 80, 80)
    TEXT 120, 70, "GAME OVER"
    TEXTSIZE 1
    COLOR RGB(255, 255, 255)
    TEXT 120, 105, "Score: " + STR$(score)
    TEXT 115, 120, "Waves: " + STR$(wave)
    IF score = high_score AND score > 0 THEN
        TEXTSIZE 2
        COLOR RGB(255, 200, 0)
        TEXT 100, 140, "NEW HIGH!"
        TEXTSIZE 1
    ENDIF
    COLOR RGB(200, 200, 200)
    TEXT 100, 175, "Press A to restart"
    TEXT 115, 195, "Press B for menu"
    RETURN 0
END FUNCTION

FUNCTION DRAW_PAUSE()
    COLOR RGB(0, 0, 0)
    RECT 0, 0, 400, 240, FILL
    TEXTSIZE 2
    COLOR RGB(255, 255, 255)
    TEXT 150, 90, "PAUSED"
    TEXTSIZE 1
    TEXT 110, 130, "Press START to resume"
    TEXT 130, 150, "Press B to quit"
    RETURN 0
END FUNCTION

' 
'  MAIN LOOP
' 

WHILE TRUE
    frame = frame + 1

    '  Update 
    IF state = 0 THEN
        IF BUTTONDOWN(A) THEN
            RESET_GAME()
            state = 1
            START_WAVE()
        ENDIF
    ELSEIF state = 1 THEN
        IF BUTTONDOWN(START) THEN
            state = 3
            PAUSEMUSIC
        ELSE
            UPDATE_PLAYER()
            UPDATE_BULLETS()
            UPDATE_ENEMIES()
            UPDATE_PARTICLES()
            UPDATE_WAVES()
            IF shake > 0 THEN shake = shake - 1
        ENDIF
    ELSEIF state = 2 THEN
        IF BUTTONDOWN(A) THEN
            RESET_GAME()
            state = 1
            START_WAVE()
        ELSEIF BUTTONDOWN(B) THEN
            state = 0
        ENDIF
        UPDATE_PARTICLES()
        IF shake > 0 THEN shake = shake - 1
    ELSEIF state = 3 THEN
        IF BUTTONDOWN(START) THEN
            state = 1
            RESUMEMUSIC
        ELSEIF BUTTONDOWN(B) THEN
            state = 0
        ENDIF
    ENDIF

    '  Draw top screen 
    SETSCREEN 0
    CLS RGB(10, 10, 25)

    ' Apply screen shake via TRANSLATE
    IF shake > 0 THEN
        shake_x = (RNDF() - 0.5) * shake
        shake_y = (RNDF() - 0.5) * shake
        TRANSLATE shake_x, shake_y
    ENDIF

    IF state = 0 THEN
        DRAW_MENU()
    ELSEIF state = 1 OR state = 3 THEN
        DRAW_PARTICLES()
        DRAW_BULLETS()
        DRAW_ENEMIES()
        DRAW_PLAYER()
        DRAW_TOP_HUD()
        IF state = 3 THEN DRAW_PAUSE()
    ELSEIF state = 2 THEN
        DRAW_PARTICLES()
        DRAW_BULLETS()
        DRAW_ENEMIES()
        DRAW_GAMEOVER()
    ENDIF

    ' Reset translate
    TRANSLATE 0, 0

    '  Draw bottom screen (only during gameplay) 
    IF state = 1 OR state = 3 THEN
        DRAW_BOTTOM_SCREEN()
    ENDIF

    WAITFRAME
WEND
