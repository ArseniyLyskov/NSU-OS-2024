import os
import hashlib
import sys

def file_hash(filename):
    h = hashlib.md5()
    with open(filename, 'rb') as f:
        while True:
            chunk = f.read(8192)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def compare_dirs(dir1, dir2):
    base_path = os.getcwd()

    os.chdir(dir1)
    tree1 = build_tree('.')
    os.chdir(base_path)

    os.chdir(dir2)
    tree2 = build_tree('.')
    os.chdir(base_path)

    if set(tree1.keys()) != set(tree2.keys()):
        print("Different")
        return

    for path in tree1:
        if tree1[path] != tree2[path]:
            print("Different")
            return

    print("Same")

def build_tree(root):
    tree = {}
    for dirpath, dirnames, filenames in os.walk(root):
        for fname in filenames:
            rel_path = os.path.join(dirpath, fname)
            try:
                tree[rel_path] = file_hash(rel_path)
            except:
                tree[rel_path] = None
    return tree

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python deep_diff.py <dir1> <dir2>")
        sys.exit(1)

    compare_dirs(sys.argv[1], sys.argv[2])
