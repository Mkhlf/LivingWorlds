#!/usr/bin/env python3
import os
import base64
import re
from pathlib import Path

# Paths
DOCS_DIR = Path("docs")
INPUT_HTML = DOCS_DIR / "Living_Worlds_Presentation.html"
OUTPUT_HTML = DOCS_DIR / "Living_Worlds_Presentation_Portable.html"

def get_mime_type(path):
    ext = path.suffix.lower()
    if ext == '.png': return 'image/png'
    if ext == '.jpg' or ext == '.jpeg': return 'image/jpeg'
    if ext == '.gif': return 'image/gif'
    if ext == '.svg': return 'image/svg+xml'
    return 'application/octet-stream'

def embed_images():
    if not INPUT_HTML.exists():
        print(f"Error: {INPUT_HTML} not found")
        return

    print(f"Reading {INPUT_HTML}...")
    with open(INPUT_HTML, 'r', encoding='utf-8') as f:
        content = f.read()

    # Regex to find img src="..."
    # Matches src="/absolute/path..."
    # We only want to replace local paths, not http://
    
    def replace_match(match):
        src = match.group(1)
        if src.startswith('http') or src.startswith('data:'):
            return match.group(0) # Skip remote or already embedded
        
        path = Path(src)
        if path.exists():
            print(f"Embedding {path.name}...")
            try:
                with open(path, 'rb') as img_f:
                    data = base64.b64encode(img_f.read()).decode('utf-8')
                    mime = get_mime_type(path)
                    return f'src="data:{mime};base64,{data}"'
            except Exception as e:
                print(f"Failed to read {path}: {e}")
                return match.group(0)
        else:
            print(f"Warning: Image file not found: {src}")
            return match.group(0)

    # Regex for src="path"
    # Note: simplified regex, assumes standard formatting from Marp
    new_content = re.sub(r'src="([^"]+)"', replace_match, content)

    print(f"Writing {OUTPUT_HTML}...")
    with open(OUTPUT_HTML, 'w', encoding='utf-8') as f:
        f.write(new_content)
    
    print("Done! Portable HTML created.")

if __name__ == "__main__":
    embed_images()
