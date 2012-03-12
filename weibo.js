// $Id$

$(function() {
	
	function c() {
		var _c = 140;
		var b = $('#weibo_tweet_form_type_tweet').val() || '';
		if (b.length < _c) {
			_c -= b.length;
		} else if (b.length > _c) {
			_c -= b.length;
		} else {
			_c = 0;
		}
		$('#weibo_tweet_text_count').text(_c);
	};
	
	c();
	
	$('#weibo_tweet_form_type_tweet').bind('keyup', c);
	$('#weibo_tweet_form_type_tweet').bind('mouseup', c);
	
});
