' variables.bas: Variables, Math, and Strings
' Demonstrates: implicit declaration, type inference, math, string concat

CLS RGB(20, 20, 50)
COLOR RGB(255, 255, 255)
y = 10

' Numbers
score = 100
pi = 3.14159
name$ = "Player 1"

TEXT 10, y, "Name: " + name$ : y = y + 12
TEXT 10, y, "Score: " + STR$(score) : y = y + 12
TEXT 10, y, "Pi: " + STR$(pi) : y = y + 16

' Math
result = 2 + 3 * 4
TEXT 10, y, "2 + 3 * 4 = " + STR$(result) : y = y + 12

hypotenuse = SQR(9 * 9 + 12 * 12)
TEXT 10, y, "Hypotenuse of 9,12 = " + STR$(hypotenuse) : y = y + 12

dice = RND(6) + 1
TEXT 10, y, "Dice roll: " + STR$(dice) : y = y + 16

' String operations
greeting$ = "Hello, " + name$ + "!"
TEXT 10, y, greeting$ : y = y + 12
TEXT 10, y, "Length: " + STR$(LEN(greeting$)) : y = y + 12
TEXT 10, y, "Upper: " + UCASE$(greeting$) : y = y + 16

' String to number and back
TEXT 10, y, "STR$(42) = " + STR$(42) : y = y + 12
TEXT 10, y, "VAL('123') = " + STR$(VAL("123")) : y = y + 16

COLOR RGB(255, 255, 100)
TEXT 10, y, "Press START to exit."

WHILE TRUE
    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
