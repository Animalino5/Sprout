' lib/entities.bas: Entity arrays and spawning for the multi-file example

DIM dot_x(32)
DIM dot_y(32)
DIM dot_life(32)
DIM dot_color(32)
score = 0

FUNCTION RESET_ENTITIES()
    FOR i = 0 TO 31
        dot_life(i) = 0
    NEXT
    score = 0
    RETURN 0
END FUNCTION

FUNCTION SPAWN_DOT(x, y)
    FOR i = 0 TO 31
        IF dot_life(i) <= 0 THEN
            dot_x(i) = x
            dot_y(i) = y
            dot_life(i) = 300
            dot_color(i) = RGB(100 + RND(155), 100 + RND(155), 100 + RND(155))
            RETURN 0
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION UPDATE_DOTS()
    FOR i = 0 TO 31
        IF dot_life(i) > 0 THEN
            dot_life(i) = dot_life(i) - 1
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION DRAW_DOTS()
    FOR i = 0 TO 31
        IF dot_life(i) > 0 THEN
            COLOR dot_color(i)
            r = 4
            IF dot_life(i) < 60 THEN r = 3
            IF dot_life(i) < 30 THEN r = 2
            CIRCLE dot_x(i), dot_y(i), r, FILL
        ENDIF
    NEXT
    RETURN 0
END FUNCTION

FUNCTION CHECK_COLLECTION(px, py)
    count = 0
    FOR i = 0 TO 31
        IF dot_life(i) > 0 THEN
            IF DISTANCE(px, py, dot_x(i), dot_y(i)) < 15 THEN
                dot_life(i) = 0
                count = count + 1
            ENDIF
        ENDIF
    NEXT
    RETURN count
END FUNCTION

FUNCTION COUNT_DOTS()
    count = 0
    FOR i = 0 TO 31
        IF dot_life(i) > 0 THEN count = count + 1
    NEXT
    RETURN count
END FUNCTION
