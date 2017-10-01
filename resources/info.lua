params = {}

metanames = {"type"}

types = {
	css = "text/css",
	eot = "application/vnd.ms-fontobject",
	html = "text/html",
	js = "application/x-javascript",
	lua = "text/plain",
	otf = "application/vnd.ms-opentype",
	png = "image/png",
	svg = "image/svg+xml",
	ttf = "application/x-font-ttf",
}

setmetatable(types, { __index = function () return "application/octet-stream" end })

function ext(path)
	local match = path:match(".*%.(.-)$")
	if match then return match else return path end
end

function meta(path)
	return {type = types[ext(path)]}
end
