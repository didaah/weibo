<?php
// $Id$

/**
 * 用户的微博页面
 */
function _weibo_user_center($center) {
  if ($center->type == 'weibo_default') { 

    if (empty($center->user->ext->weibo)) {
      $center->body = system_no_content(t('weibo', '你还没有绑定新浪微博帐号，<a href="!url">立即绑定</a>',
      array('!url' => url('weibo/validate'))));
    } else {
      $args = arg();
      switch ($args[3]) {
        case 'remove':

          if (!empty($center->user->ext->weibo[$args[4]])) {
            if (db_exec('DELETE FROM {user_ext} WHERE aid = ?', array($args[4]))) {
              dd_set_message(t('weibo', '已解除绑定'));
              
              dd_log('weibo', t('sina_open', '解除微博帐号 !name 的绑定',
              array('!name' => $center->user->ext->weibo[$args[4]])));

              user_clear_session($center->user->uid); 
            } else {
              dd_set_message(t('weibo', '解除失败，请重试'));
            }
          }

          dd_goto('user/center/weibo_default');
        break;
        case 'refresh':

          if (!empty($center->user->ext->weibo[$args[4]])) {
            // 获取最新微博信息
            $api = new weibo(0, $args[4]);
            if ($weibo = $api->get_user_info()) {
              // 更新信息
              weibo_set_user_data($args[4], array('info' => $weibo));
              dd_set_message(t('weibo', '更新成功'));
            }
          }

          dd_goto('user/center/weibo_default');
        break;
      }

      $table = array();

      foreach ($center->user->ext->weibo as $o) {
        $view = img($o->data['info']->profile_image_url);
        $view .= '<p><a href="http://www.weibo.com/' . $o->data['info']->domain;
        $view .= '" target="_blank" title="访问微博">' . $o->ext_name . '</a></p>';
        $table[] = array(
          $view,
          $o->data['info']->friends_count, // 关注
          $o->data['info']->followers_count, // 粉丝
          $o->data['info']->statuses_count, // 微博
          $o->data['info']->favourites_count, // 收藏
          l(t('weibo', '刷新'), 'user/center/weibo_default/refresh/' . $o->aid)
        );
      }
      
      $center->body = '<h2>我的微博：</h2>'; 
      
      $center->body .= theme('table', array('微博', '关注数', '粉丝数', '微博数', '收藏数', '操作'), 
      $table, array('class' => 'center_table'));
      $center->body .= '<p class="right">' . l('绑定新的微博帐号', 'weibo/validate') . '</p>';

      $center->body .= '<div id="weibo_user_tweet">' . dd_get_form('weibo_tweet_form') . '</div>';
    }
  
  }

  dd_add_css(dd_get_path('module', 'weibo') . '/weibo.css');

  return $output;
}


