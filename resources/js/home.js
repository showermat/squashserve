
$(document).ready(function() {
	$("#quit").on("click", function() {
		$("html").load("/rsrc/html/quit.html", function() { $.get("/action/quit"); });
		return false;
	});
	$(".search-input").val("");
	$(".search-input").each(function() { autocomplete($(this), $(this).parent().data("volid")); });
	$(".search").on('submit', function() {
		var query = $(this).find(".search-input").val();
		window.open("/search/" + $(this).data("volid") + "/" + encodeURIComponent(query));
		$(this).find(".search-input").val("");
		return false;
	});
});
