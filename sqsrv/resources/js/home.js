$(document).ready(function() {
	$("#quit").on("click", function() {
		$("html").load("/rsrc/html/quit.html", function() { $.get("/action/quit"); });
		return false;
	});
	$("div.category-container a.category-title").on("click", function() {
		var anchor = $(this);
		anchor.find(".category-loading").show();
		var target = anchor.parent().find("table.volume-list");
		var url = "/load";
		if (anchor.parent().hasClass("loaded")) url = "/unload";
		$.get(url, {cat : anchor.data("catname")}, function(data) {
			target.html(data);
			target.parent().toggleClass("loaded unloaded");
			search_setup("search", ".search-input", ".search", false);
			anchor.find(".category-loading").hide();
		});
		return false;
	});
	search_setup("search", ".search-input", ".search", false);
});
