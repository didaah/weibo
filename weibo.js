// $Id$

$(function() {
	
	function c() {
		var _c = 140;
		var b = $('#weibo_tweet_form_type_tweet').val() || '';

    for (var i=0; i < b.length; i++) {
      if (b.charCodeAt(i) >= 10000) {
        _c -= 1;
      } else {
        _c -= 0.5;
      }
    }
		$('#weibo_tweet_text_count').text(Math.floor(_c));
	};
	
	c();
	
	$('#weibo_tweet_form_type_tweet').bind('keyup', c);
	$('#weibo_tweet_form_type_tweet').bind('mouseup', c);
	
});
