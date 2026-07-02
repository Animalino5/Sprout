#!/usr/bin/env python3
"""Generate a simple test sprite PNG for Sprout."""
from PIL import Image

# Create a 32x32 player sprite — a simple character
img = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
pixels = img.load()

# Body (blue)
for y in range(8, 24):
    for x in range(8, 24):
        pixels[x, y] = (50, 100, 200, 255)

# Head (skin tone)
for y in range(4, 12):
    for x in range(10, 22):
        pixels[x, y] = (255, 200, 150, 255)

# Eyes (black)
pixels[13, 7] = (0, 0, 0, 255)
pixels[18, 7] = (0, 0, 0, 255)

# Mouth (dark)
for x in range(14, 18):
    pixels[x, 10] = (100, 50, 50, 255)

# Save
import os
os.makedirs('/home/z/my-project/sprout/tests/samples/assets', exist_ok=True)
img.save('/home/z/my-project/sprout/tests/samples/assets/player.png')
print("Created player.png (32x32)")

# Also create a coin sprite
img2 = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
pixels2 = img2.load()
for y in range(16):
    for x in range(16):
        dx = x - 8
        dy = y - 8
        if dx*dx + dy*dy <= 36:
            pixels2[x, y] = (255, 200, 0, 255)
        elif dx*dx + dy*dy <= 49:
            pixels2[x, y] = (200, 150, 0, 255)
img2.save('/home/z/my-project/sprout/tests/samples/assets/coin.png')
print("Created coin.png (16x16)")
