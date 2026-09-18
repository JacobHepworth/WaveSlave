import os
import re

replacements = {
    r'0xFF4FA0F0': '0xFFFFFFFF', # Accent blue -> White
    r'0xFF2A2A3A': '0xFF333333',
    r'0xFF2E2E3E': '0xFF444444',
    r'0xFF1E1E2E': '0xFF222222',
    r'0xFF1A1A2E': '0xFF1A1A1A',
    r'0xFF0D0D1A': '0xFF0A0A0A',
    r'0xFF1E1E34': '0xFF252525',
    r'0xFF282848': '0xFF404040',
    r'0xFF18182A': '0xFF151515',
    r'0xFF404060': '0xFF555555',
    r'0xFF1E1E38': '0xFF252525',
    r'0xFF161626': '0xFF151515',
    r'0xFF16162A': '0xFF151515',
    r'0xFF282828': '0xFF181818', # Adjust some other grays if needed
    r'0xFF2A2A4A': '0xFF303030'
}

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    new_content = content
    for pattern, replacement in replacements.items():
        new_content = re.sub(pattern, replacement, new_content, flags=re.IGNORECASE)
        
    if new_content != content:
        with open(filepath, 'w') as f:
            f.write(new_content)
        print(f"Updated {filepath}")

for root, dirs, files in os.walk('Source'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.h'):
            process_file(os.path.join(root, file))
