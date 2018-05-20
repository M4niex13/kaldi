#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys, io

infile = io.TextIOWrapper(sys.stdin.buffer, encoding='utf-8')
output = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
for line in infile:
    output.write(' '.join([ "|"+word for word in line.split()]) + '\n')


