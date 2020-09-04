
$(document).ready(function() {
	var volid = $("body").data("volid");
	function absurl(url) // What a great way to get the absolute URL...
	{
		var ret = new Image();
		ret.src = url;
		return ret.src;
	}
	$("#page").on('load', function() {
		var page = $("#page").contents();
		//page.find("head").append('<style>body { position: relative; top: 32px; }</style>'); //('<link rel="stylesheet" href="/rsrc/client.css">');
		document.title = page.find("title").text();
		$(window).trigger('hashchange');
		$("#page")[0].contentWindow.focus();
		page.find("a").on('click', function(e) {
			$(this).attr("href", absurl($(this).attr("href")));
			$(this).attr("target", "_top");
			return true;
		});
		page.find("form").on('submit', function() {
			$(this).attr("action", absurl($(this).attr("action")));
			$(this).attr("target", "_top");
			return true;
		});
	});
	autocomplete($("#search-input"), volid);
	$("#search").on('submit', function() {
		var query = $("#search-input").val();
		window.location = "/search/" + volid + "/" + encodeURIComponent(query);
		$("#search-input").val("");
		return false;
	});
	$(window).bind('hashchange', function() {
		$("#page").get(0).contentDocument.location.hash = window.location.hash;
		window.frames[0].scrollBy(0, -36);
	});
	$("#close-button").on('click', function() {
		$(this).prop("href", $(this).prop("href").split("#")[0] + window.location.hash);
		return true;
	})
});

