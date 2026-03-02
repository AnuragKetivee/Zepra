import re
import os

source_file = '/home/swana/Documents/zeprabrowser/resources/icons.jsx'
output_dir = '/home/swana/Documents/zeprabrowser/resources/web'

def extract_icons():
    with open(source_file, 'r') as f:
        content = f.readlines()

    count = 0
    # Regex to capture name in quotes and the svg string
    # Matches: 'neolyx-home': '<svg ...>',
    pattern = re.compile(r"^\s*'([\w-]+)':\s*'(<svg.+>)'")

    for line in content:
        match = pattern.search(line)
        if match:
            name = match.group(1)
            svg_content = match.group(2)
            
            # Write to file
            filename = os.path.join(output_dir, f"{name}.svg")
            with open(filename, 'w') as out:
                out.write(svg_content)
            
            print(f"Extracted: {name}.svg")
            count += 1
            
    print(f"Done. Extracted {count} icons.")

if __name__ == "__main__":
    extract_icons()
