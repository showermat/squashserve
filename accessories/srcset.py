#! /usr/bin/python

import sys
import os
import os.path
import bs4
import urllib.parse
import requests

def srcset_fetch(fh):
	html = bs4.BeautifulSoup(fh, "lxml")
	for img in html.find_all("img"):
		if "srcset" in img.attrs:
			for srcdesc in img["srcset"].split(","):
				src = srcdesc.strip().split(" ")[0]
				if src.startswith("//"): src = "http:" + src
				if not (src.startswith("http://") or src.startswith("ftp://")):
					print(">>>> UNHANDLED LINK " + src)
					continue
				dest = os.path.join(basedir, urllib.parse.urlparse(src).path.lstrip("/"))
				if not os.path.exists(dest):
					print("%s => %s" % (src, dest))
					ofh = open(dest, "wb")
					ofh.write(requests.get(src).content)
					ofh.close()

def srcset_remove(fh):
	html = bs4.BeautifulSoup(fh, "lxml")
	for img in html.find_all("img"):
		if "srcset" in img.attrs:
			del img["srcset"]
	fh.seek(0)
	fh.truncate()
	fh.write(str(html))

basedir = sys.argv[1]
for root, dirs, files in os.walk(basedir):
	for f in files:
		if f.endswith(".html"):
			fh = open(os.path.join(root, f), "r+")
			print(fh.name)
			#srcset_fetch(fh)
			srcset_remove(fh)
			fh.close()
