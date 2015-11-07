
$(document).ready(function() {
	$("#quit").on("click", function() {
		$("html").load("/rsrc/html/quit.html", function() { $.get("/action/quit"); });
		return false;
	});
	$(".search").val("");
	$(".search").on('submit', function() {
		var query = $(this).children(".search-input").val();
		window.open("/search/" + $(this).data("volid") + "/" + encodeURIComponent(query));
		$(this).children(".search-input").val("");
		return false;
	});
});
