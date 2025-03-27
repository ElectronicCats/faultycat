import os

def generate_header_file(source_file, header_file):
    with open(source_file, 'r') as src:
        lines = src.readlines()

    with open(header_file, 'w') as hdr:
        for line in lines:
            if line.startswith("int main("):  # Detect the start of the main function
                break
            hdr.write(line)

if __name__ == "__main__":
    source_file = "blueTag/src/blueTag.c"
    header_file = "blueTag.h"

    if not os.path.exists(source_file):
        print(f"Error: {source_file} not found in the current directory.")
    else:
        generate_header_file(source_file, header_file)
        print(f"{header_file} has been generated successfully.")