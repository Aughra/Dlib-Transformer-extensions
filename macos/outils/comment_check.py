#!/usr/bin/env python3
"""Checks that only comments changed between git HEAD and the working tree.

usage: comment_check.py [--rev REV] file...
Exit status: number of files whose code (comments removed) differs.
"""
import os
import re
import subprocess
import sys

C_LIKE = {'.cpp', '.h', '.hpp', '.c', '.inc', '.mm', '.metal', '.js', '.css', '.in', '.mjs'}
HASH = {'.py', '.sh', '.cmake', '.txt'}


def strip_c(s, js):
    out = []
    i, n = 0, len(s)
    last = ''  # last significant char of code, for JS regex literals
    while i < n:
        c = s[i]
        if s.startswith('//', i) and not s.endswith(':', 0, i):  # not http://
            j = s.find('\n', i)
            i = n if j < 0 else j
            continue
        if s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
            continue
        m = re.match(r'R"([^(\s]{0,16})\(', s[i:]) if not js else None
        if m:
            end = ')' + m.group(1) + '"'
            j = s.find(end, i + m.end())
            j = n if j < 0 else j + len(end)
            out.append(s[i:j]); i = j; last = '"'
            continue
        if c in '"\'' or (js and c == '`'):
            j = i + 1
            while j < n and s[j] != c:
                if s[j] == '\\':
                    j += 1
                elif s[j] == '\n' and c != '`':
                    break
                j += 1
            out.append(s[i:j + 1]); i = j + 1; last = c
            continue
        if js and c == '/' and (last == '' or last in '(,=:[!&|?{};+-*%<>~^' or re.search(r'\b(return|typeof|case|in|of)\s*$', ''.join(out[-3:]))):
            j = i + 1
            in_class = False
            while j < n and s[j] != '\n':
                if s[j] == '\\':
                    j += 2
                    continue
                if s[j] == '[':
                    in_class = True
                elif s[j] == ']':
                    in_class = False
                elif s[j] == '/' and not in_class:
                    break
                j += 1
            out.append(s[i:j + 1]); i = j + 1; last = '/'
            continue
        out.append(c)
        if not c.isspace():
            last = c
        i += 1
    return ''.join(out)


def strip_hash(s, kind):
    lines = []
    for line in s.split('\n'):
        q = None
        cut = len(line)
        k = 0
        while k < len(line):
            ch = line[k]
            if q:
                if ch == '\\':
                    k += 2
                    continue
                if ch == q:
                    q = None
            elif ch in '"\'':
                q = ch
            elif ch == '#' and (k == 0 or line[k - 1].isspace() or kind != 'sh'):
                if kind == 'sh' and k > 0 and line[k - 1] in '${':
                    k += 1
                    continue
                if line.startswith('#!', 0) and k == 0:
                    break
                cut = k
                break
            k += 1
        lines.append(line[:cut].rstrip())
    return '\n'.join(l for l in lines if l.strip())


def normalize(text, path):
    ext = os.path.splitext(path)[1]
    base = os.path.basename(path)
    if base == 'CMakeLists.txt' or ext == '.cmake':
        text = re.sub(r'#\[(=*)\[.*?\]\1\]', ' ', text, flags=re.S)
        return re.sub(r'\s+', ' ', strip_hash(text, 'cmake'))
    if ext == '.py':
        return strip_hash(text, 'py')
    if ext == '.sh':
        return re.sub(r'[ \t]+', ' ', strip_hash(text, 'sh'))
    if ext == '.html':
        text = re.sub(r'<!--.*?-->', ' ', text, flags=re.S)
        return re.sub(r'\s+', ' ', text)
    if ext in C_LIKE:
        return re.sub(r'\s+', ' ', strip_c(text, ext in ('.js', '.mjs')))
    return re.sub(r'\s+', ' ', text)


def main():
    args = sys.argv[1:]
    rev = 'HEAD'
    if args[:1] == ['--rev']:
        rev, args = args[1], args[2:]
    bad = 0
    for path in args:
        rel = subprocess.run(['git', 'ls-files', '--full-name', path], capture_output=True, text=True).stdout.strip()
        old = subprocess.run(['git', 'show', rev + ':' + rel], capture_output=True, text=True).stdout
        new = open(path, encoding='utf-8').read()
        a, b = normalize(old, path), normalize(new, path)
        if a != b:
            bad += 1
            k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
            print('CODE CHANGED: %s\n  avant: %r\n  après: %r' % (path, a[max(0, k - 80):k + 80], b[max(0, k - 80):k + 80]))
    print('%d fichier(s) vérifié(s), %d avec du code modifié' % (len(args), bad))
    sys.exit(bad)


if __name__ == '__main__':
    main()
