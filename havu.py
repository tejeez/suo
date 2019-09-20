#!/usr/bin/env python3
import subprocess, sys
for l in sys.stdin:
	if len(l)>10:
		try:
			#print("asdf")
			cp = subprocess.run(["wget","-O/dev/null","-q","http://scout.polygame.fi/api/msg?msg=" + l.strip()], timeout=2)
			#print(cp)
		except Exception as e:
			print(e)
