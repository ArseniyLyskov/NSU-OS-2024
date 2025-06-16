import os
import sys

if len(sys.argv) != 2:
    print("Usage: python make1000subfolders.py <base_path>")
    sys.exit(1)

base_path = sys.argv[1]

if not os.path.exists(base_path):
    os.makedirs(base_path)

os.chdir(base_path)

for i in range(1000):
    folder_name = "subfolder%04d" % i
    os.mkdir(folder_name)
    os.chdir(folder_name)

    with open("file1", "w") as f:
        f.write("This is file1 in %s\n" % folder_name)

    with open("file2", "w") as f:
        f.write("This is file2 in %s\n" % folder_name)
