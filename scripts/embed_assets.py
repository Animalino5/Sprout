#!/usr/bin/env python3
"""
embed_assets.py — Convert files in assets/ to embedded C byte arrays.

Usage: python3 embed_assets.py <assets_dir> <output_dir>

Generates two files:
  - <output_dir>/embedded_assets.c  (byte arrays + lookup table)
  - <output_dir>/embedded_assets.h  (declarations)

The embedded data is linked directly into the .dol, so users don't need
to copy anything to SD card. At runtime, functions check the embedded
table first, then fall back to SD card.

File names are normalized (lowercase, basename only) for lookup.
"""
import os
import sys
import struct

def make_c_identifier(name):
    """Convert a filename to a valid C identifier."""
    result = ""
    for c in name:
        if c.isalnum():
            result += c
        else:
            result += "_"
    return "asset_" + result

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <assets_dir> <output_dir>")
        sys.exit(1)
    
    assets_dir = sys.argv[1]
    output_dir = sys.argv[2]
    
    if not os.path.isdir(assets_dir):
        print(f"  No assets/ folder found — generating empty embedded table")
        # Still generate the files so the build doesn't break
        os.makedirs(output_dir, exist_ok=True)
        with open(os.path.join(output_dir, "embedded_assets.h"), "w") as f:
            f.write("#ifndef EMBEDDED_ASSETS_H\n#define EMBEDDED_ASSETS_H\n")
            f.write("#include <stdint.h>\n")
            f.write("typedef struct { const char* name; const uint8_t* data; uint32_t size; } EmbeddedAsset;\n")
            f.write("extern EmbeddedAsset g_embedded_assets[];\n")
            f.write("extern const int g_embedded_asset_count;\n")
            f.write("const uint8_t* embedded_find(const char* name, uint32_t* out_size);\n")
            f.write("#endif\n")
        with open(os.path.join(output_dir, "embedded_assets.c"), "w") as f:
            f.write('#include "embedded_assets.h"\n\n')
            f.write("EmbeddedAsset g_embedded_assets[] = {{NULL, NULL, 0}};\n")
            f.write("const int g_embedded_asset_count = 0;\n\n")
            f.write("const uint8_t* embedded_find(const char* name, uint32_t* out_size) {\n")
            f.write("    (void)name; (void)out_size; return NULL;\n")
            f.write("}\n")
        return 0
    
    os.makedirs(output_dir, exist_ok=True)
    
    # Collect all files
    files = []
    for fname in sorted(os.listdir(assets_dir)):
        fpath = os.path.join(assets_dir, fname)
        if os.path.isfile(fpath):
            files.append((fname, fpath))
    
    if not files:
        print("  assets/ folder is empty — generating empty embedded table")
        files = []
    
    # Generate header
    with open(os.path.join(output_dir, "embedded_assets.h"), "w") as f:
        f.write("#ifndef EMBEDDED_ASSETS_H\n")
        f.write("#define EMBEDDED_ASSETS_H\n")
        f.write("#include <stdint.h>\n\n")
        f.write("typedef struct {\n")
        f.write("    const char* name;\n")
        f.write("    const uint8_t* data;\n")
        f.write("    uint32_t size;\n")
        f.write("} EmbeddedAsset;\n\n")
        f.write("extern EmbeddedAsset g_embedded_assets[];\n")
        f.write("extern const int g_embedded_asset_count;\n\n")
        f.write("/* Find an embedded asset by filename (case-insensitive, basename only).\n")
        f.write(" * Returns pointer to data and sets *out_size, or returns NULL if not found. */\n")
        f.write("const uint8_t* embedded_find(const char* name, uint32_t* out_size);\n\n")
        f.write("#endif\n")
    
    # Generate source
    with open(os.path.join(output_dir, "embedded_assets.c"), "w") as f:
        f.write('#include "embedded_assets.h"\n')
        f.write('#include <string.h>\n')
        f.write('#include <strings.h>\n\n')
        
        # Write each file as a byte array
        for fname, fpath in files:
            with open(fpath, "rb") as af:
                data = af.read()
            
            ident = make_c_identifier(fname)
            size = len(data)
            
            print(f"  Embedding {fname} ({size} bytes)")
            
            f.write(f"/* {fname} — {size} bytes */\n")
            f.write(f"static const unsigned char {ident}[] = {{\n")
            
            # Write 12 bytes per line
            for i in range(0, size, 12):
                chunk = data[i:i+12]
                hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
                f.write(f"    {hex_bytes},\n")
            
            f.write("};\n\n")
        
        # Write lookup table
        f.write("EmbeddedAsset g_embedded_assets[] = {\n")
        for fname, fpath in files:
            ident = make_c_identifier(fname)
            with open(fpath, "rb") as af:
                size = len(af.read())
            f.write(f'    {{ "{fname}", {ident}, {size} }},\n')
        f.write("    { NULL, NULL, 0 }\n")
        f.write("};\n\n")
        
        # Count
        f.write(f"const int g_embedded_asset_count = {len(files)};\n\n")
        
        # Lookup function
        f.write("const uint8_t* embedded_find(const char* name, uint32_t* out_size) {\n")
        f.write("    if (!name) return NULL;\n")
        f.write("    /* Get basename (after last /) */\n")
        f.write("    const char* base = name;\n")
        f.write("    const char* slash = strrchr(name, '/');\n")
        f.write("    if (slash) base = slash + 1;\n")
        f.write("    /* Also strip sd:/ and romfs:/ prefixes */\n")
        f.write("    if (strncmp(base, \"sd:/\", 4) == 0) base += 4;\n")
        f.write("    if (strncmp(base, \"romfs:/\", 7) == 0) base += 7;\n")
        f.write("    \n")
        f.write("    for (int i = 0; i < g_embedded_asset_count; i++) {\n")
        f.write("        if (strcasecmp(g_embedded_assets[i].name, base) == 0) {\n")
        f.write("            if (out_size) *out_size = g_embedded_assets[i].size;\n")
        f.write("            return g_embedded_assets[i].data;\n")
        f.write("        }\n")
        f.write("    }\n")
        f.write("    return NULL;\n")
        f.write("}\n")
    
    print(f"  Embedded {len(files)} file(s) into embedded_assets.c")
    return 0

if __name__ == "__main__":
    main()
