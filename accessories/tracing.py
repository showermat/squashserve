import datetime
import threading

enabled = False
out = None
start = 0
rankcnt = 0
lock = threading.Lock()

def elapsed():
	global start
	return datetime.datetime.now().timestamp() - start

class Tracer:
	def write(self, str):
		global out, lock
		with lock: out.write("\n" + str)
	def point(self, pos, color):
		self.write("<circle cx=\"%d\" cy=\"%d\" r=\"%d\" fill=\"%s\" stroke=\"none\" />" % (pos, self.rank * 20 + 16, 1, color))
	def line(self, start, end, color):
		self.write("<line x1=\"%d\" x2=\"%d\" y1=\"%d\" y2=\"%d\" fill=\"none\" stroke=\"%s\" />" % (start, end, self.rank * 20 + 16, self.rank * 20 + 16, color))
	def rect(self, start, width):
		self.write("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" />" % (start, self.rank * 20, width, 18))
	def text(self, start, body):
		self.write("<text x=\"%d\" y=\"%d\" fill=\"black\" stroke=\"none\">%s</text>" % (start, self.rank * 20 + 10, body))
	def __init__(self):
		global rankcnt, lock
		if not enabled: return
		with lock:
			self.rank = rankcnt
			rankcnt += 1
		self.point(elapsed(), "blue")
		self.images = {}
		self.spanstart = 0
		self.processstart = 0
		self.cleanstart = 0
		self.page = ""
	def start(self, page):
		if not enabled: return
		self.spanstart = elapsed()
		self.page = page
	def processing(self):
		if not enabled: return
		self.processstart = elapsed()
	def image(self, src):
		if not enabled: return
		self.images[src] = elapsed()
	def cleaning(self):
		if not enabled: return
		self.cleanstart = elapsed()
	def finish(self):
		if not enabled: return
		dur = elapsed() - self.spanstart
		if dur > 2:
			self.rect(self.spanstart, dur)
			if dur > 10:
				self.line(self.processstart, min(self.images.values()) if len(self.images) > 0 else self.cleanstart, "green")
				for (k, v) in self.images.items(): self.point(v, "orange")
				#self.line(self.cleanstart, self.spanstart + dur, "orange")
				if dur > 40 and self.page is not None: self.text(self.spanstart + 2, self.page)
		self.images = {}
	def exit(self):
		if not enabled: return
		self.point(elapsed(), "red")

def init(outf):
	global out, start
	if not enabled: return
	out = open(outf, "w")
	start = elapsed()
	out.write('<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n')
	out.write('<svg xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:svg="http://www.w3.org/2000/svg">')
	out.write('<rect x="0" y="0" width="100%" height="100%" fill="white" />')
	out.write('<g fill="white" stroke="black" stroke-width="1">')

def cleanup():
	if not enabled: return
	out.write("\n</g></svg>")
	out.close()
