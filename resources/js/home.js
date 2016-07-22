
$(document).ready(function() {
	$("#quit").on("click", function() {
		$("html").load("/rsrc/html/quit.html", function() { $.get("/action/quit"); });
		return false;
	});
	$("div.category-container a.category-title").on("click", function() {
		var target = $(this).parent().find("table.volume-list");
		var url = "/load/";
		if ($(this).parent().hasClass("loaded")) url = "/unload/";
		url += $(this).data("catname");
		$.get(url, function(data) {
			target.html(data);
			target.parent().toggleClass("loaded unloaded");
		});
		return false;
	});
	$(".search-input").val("");
	$(".search-input").each(function() { autocomplete($(this), $(this).parent().data("volid"), true); });
	$(".search").on('submit', function() {
		var query = $(this).find(".search-input").val();
		window.open("/search/" + $(this).data("volid") + "/" + encodeURIComponent(query));
		$(this).find(".search-input").val("");
		return false;
	});
});

