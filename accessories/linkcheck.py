#! /usr/bin/python

import sys
import os
import urllib.parse
import bs4
import queue

fileq = queue.Queue()
fileq.put(os.path.realpath(sys.argv[1]))
done = set()
while not fileq.empty():
	path = fileq.get()
	if path in done: continue
	done.add(path)
	#sys.stderr.write("\r" + str(len(done))); sys.stderr.flush()
	os.chdir(os.path.dirname(path))
	html = bs4.BeautifulSoup(open(os.path.basename(path), "rb").read().decode("UTF-8", "ignore"), "lxml")
	for ref in [ urllib.parse.urlparse(tag["href"]) for tag in html("a", href = True) ]:
		if ref.scheme != "" or ref.netloc != "" or ref.path == "": continue
		if not os.path.isfile(ref.path): print(path + " -> " + ref.path)
		else: fileq.put(os.path.realpath(ref.path))
	for ref in [ urllib.parse.urlparse(tag["src"]) for tag in html("img", src = True) ]:
		if ref.scheme != "" or ref.netloc != "" or ref.path == "": continue
		if not os.path.isfile(ref.path): print(path + " -> " + ref.path)
