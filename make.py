#!/usr/bin/env python

import sys
sys.path.insert(0, "pybake")
import pybake3 as pybake
pkg_config = pybake.pkg_config

conf, opts, targs = pybake.parse_args()
rules = pybake.Rules()

gasim = pybake.cxproject(sources=["main.c"])
gasim.cxflags = ['-Wall', '-std=gnu99', '-g'] + pkg_config(['--cflags'], ['sdl'])
gasim.linkopts = pkg_config(['--libs'], ['sdl'])
gasim.add_to(rules)

pybake.make(rules, opts, targs)
