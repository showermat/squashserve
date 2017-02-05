
function search_setup()
{
	$(".search-input").val("");
	$(".search-input").each(function() { autocomplete($(this), $(this).parent().data("volid"), true); });
	$(".search").on('submit', function() {
		var query = $(this).find(".search-input").val();
		window.open("/search/" + $(this).data("volid") + "/" + encodeURIComponent(query));
		clearform($(this).data("volid"));
		return false;
	});
}

$(document).ready(function() {
	$("#quit").on("click", function() {
		$("html").load("/rsrc/html/quit.html", function() { $.get("/action/quit"); });
		return false;
	});
	$("div.category-container a.category-title").on("click", function() {
		var anchor = $(this);
		anchor.find(".category-loading").show();
		var target = anchor.parent().find("table.volume-list");
		var url = "/load/";
		if (anchor.parent().hasClass("loaded")) url = "/unload/";
		url += anchor.data("catname");
		$.get(url, function(data) {
			target.html(data);
			target.parent().toggleClass("loaded unloaded");
			search_setup();
			anchor.find(".category-loading").hide();
		});
		return false;
	});
	search_setup();
});

